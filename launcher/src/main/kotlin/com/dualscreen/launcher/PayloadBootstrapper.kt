package com.dualscreen.launcher

import android.content.Context
import android.util.Log
import com.wemu.engine.contents.GuestPayloads
import com.wemu.engine.core.Callback
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream

internal enum class PayloadBootstrapPhase { DOWNLOAD, VERIFY, INSTALL }

private const val DOWNLOAD_WEIGHT = 0.68
private const val VERIFY_WEIGHT = 0.08
private const val INSTALL_WEIGHT = 0.24

internal fun payloadOverallFraction(
    completedPackBytes: Long,
    totalPackBytes: Long,
    currentPackBytes: Long,
    phase: PayloadBootstrapPhase,
    phaseFraction: Double,
): Float {
    require(totalPackBytes > 0 && completedPackBytes >= 0 && currentPackBytes > 0)
    val boundedPhase = phaseFraction.coerceIn(0.0, 1.0)
    val packFraction = when (phase) {
        PayloadBootstrapPhase.DOWNLOAD -> boundedPhase * DOWNLOAD_WEIGHT
        PayloadBootstrapPhase.VERIFY -> DOWNLOAD_WEIGHT + boundedPhase * VERIFY_WEIGHT
        PayloadBootstrapPhase.INSTALL ->
            DOWNLOAD_WEIGHT + VERIFY_WEIGHT + boundedPhase * INSTALL_WEIGHT
    }
    return ((completedPackBytes.toDouble() + currentPackBytes * packFraction) / totalPackBytes)
        .toFloat()
        .coerceIn(0f, 1f)
}

internal sealed interface PayloadBootstrapState {
    data object Checking : PayloadBootstrapState
    data class Progress(
        val detail: String,
        val fraction: Float,
        val currentPack: Int,
        val totalPacks: Int,
        val phase: PayloadBootstrapPhase,
        val phaseCompletedBytes: Long,
        val phaseTotalBytes: Long,
    ) : PayloadBootstrapState
    data class Ready(val version: String) : PayloadBootstrapState
    data class Failed(val message: String) : PayloadBootstrapState
}

internal data class PayloadPack(
    val name: String,
    val bytes: Long,
    val sha256: String,
    val url: String,
)

internal data class PayloadBootstrapManifest(
    val version: String,
    val packs: List<PayloadPack>,
) {
    companion object {
        fun parse(text: String): PayloadBootstrapManifest {
            val json = JSONObject(text.trimStart('\uFEFF'))
            require(json.getInt("schema") == 1) { "Unsupported payload manifest schema" }
            val version = json.getString("version").trim()
            require(version.isNotBlank()) { "Payload manifest has no version" }
            val array = json.getJSONArray("packs")
            require(array.length() > 0) { "Payload manifest has no packs" }
            val packs = buildList {
                for (index in 0 until array.length()) {
                    val entry = array.getJSONObject(index)
                    val pack = PayloadPack(
                        name = entry.getString("name"),
                        bytes = entry.getLong("bytes"),
                        sha256 = entry.getString("sha256").lowercase(),
                        url = entry.getString("url"),
                    )
                    require(pack.name.matches(Regex("[A-Za-z0-9._-]+"))) { "Unsafe payload name" }
                    require(pack.bytes > 0) { "Payload size must be positive" }
                    require(pack.sha256.matches(Regex("[0-9a-f]{64}"))) { "Invalid payload checksum" }
                    require(pack.url.startsWith("https://github.com/")) { "Untrusted payload host" }
                    add(pack)
                }
            }
            require(packs.map(PayloadPack::name).distinct().size == packs.size) {
                "Payload names must be unique"
            }
            return PayloadBootstrapManifest(version, packs)
        }
    }
}

internal class PayloadBootstrapper(private val context: Context) {
    private val mutex = Mutex()
    private val mutableState = MutableStateFlow<PayloadBootstrapState>(PayloadBootstrapState.Checking)
    val state: StateFlow<PayloadBootstrapState> = mutableState.asStateFlow()

    suspend fun ensureReady(force: Boolean = false) = mutex.withLock {
        mutableState.value = PayloadBootstrapState.Checking
        val manifest = runCatching { readManifest() }.getOrElse { failure ->
            mutableState.value = PayloadBootstrapState.Failed(
                "The launcher payload manifest is invalid: ${failure.message}",
            )
            return@withLock
        }
        val current = PayloadAssetStore.root(context)
        if (!force && File(current, VERSION_FILE).readTextOrNull() == manifest.version) {
            mutableState.value = PayloadBootstrapState.Ready(manifest.version)
            return@withLock
        }

        val base = checkNotNull(current.parentFile)
        val staging = File(base, "staging-${manifest.version}-${UUID.randomUUID()}")
        val rollback = File(base, "rollback")
        withContext(Dispatchers.IO) {
            if (!base.isDirectory) check(base.mkdirs()) { "Could not create payload storage" }
            if (rollback.exists()) rollback.deleteRecursively()
            check(staging.mkdirs()) { "Could not create payload staging" }
        }

        try {
            val totalPackBytes = manifest.packs.sumOf(PayloadPack::bytes).coerceAtLeast(1L)
            var completedPackBytes = 0L
            var lastPublishedFraction = 0f
            var lastPhase: PayloadBootstrapPhase? = null
            var lastPhasePercent = -1

            fun publishProgress(
                index: Int,
                pack: PayloadPack,
                phase: PayloadBootstrapPhase,
                phaseCompletedBytes: Long,
                phaseTotalBytes: Long,
            ) {
                val safeTotal = phaseTotalBytes.coerceAtLeast(1L)
                val safeCompleted = phaseCompletedBytes.coerceIn(0L, safeTotal)
                val phaseFraction = safeCompleted.toDouble() / safeTotal.toDouble()
                val candidate = payloadOverallFraction(
                    completedPackBytes = completedPackBytes,
                    totalPackBytes = totalPackBytes,
                    currentPackBytes = pack.bytes,
                    phase = phase,
                    phaseFraction = phaseFraction,
                )
                val phasePercent = (phaseFraction * 100.0).toInt().coerceIn(0, 100)

                synchronized(mutableState) {
                    val bounded = maxOf(lastPublishedFraction, candidate)
                    if (lastPhase == phase && lastPhasePercent == phasePercent && bounded < 1f) return
                    lastPublishedFraction = bounded
                    lastPhase = phase
                    lastPhasePercent = phasePercent
                    val name = friendlyName(pack.name)
                    mutableState.value = PayloadBootstrapState.Progress(
                        detail = when (phase) {
                            PayloadBootstrapPhase.DOWNLOAD -> "Downloading $name"
                            PayloadBootstrapPhase.VERIFY -> "Verifying $name"
                            PayloadBootstrapPhase.INSTALL -> "Installing $name"
                        },
                        fraction = bounded,
                        currentPack = index,
                        totalPacks = manifest.packs.size,
                        phase = phase,
                        phaseCompletedBytes = safeCompleted,
                        phaseTotalBytes = safeTotal,
                    )
                }
            }

            manifest.packs.forEachIndexed { index, pack ->
                GuestPayloads.registerPayload("launcher/${pack.name}", pack.url)
                publishProgress(index, pack, PayloadBootstrapPhase.DOWNLOAD, 0L, pack.bytes)
                val archive = withContext(Dispatchers.IO) {
                    GuestPayloads.fetch(
                        context,
                        "launcher/${pack.name}",
                        Callback { percent ->
                            publishProgress(
                                index,
                                pack,
                                PayloadBootstrapPhase.DOWNLOAD,
                                pack.bytes * percent.coerceIn(0, 100) / 100L,
                                pack.bytes,
                            )
                        },
                    )
                } ?: error(GuestPayloads.lastFailure() ?: "Download failed")

                publishProgress(index, pack, PayloadBootstrapPhase.VERIFY, 0L, pack.bytes)
                withContext(Dispatchers.IO) {
                    if (archive.length() != pack.bytes || sha256(archive) { verified, total ->
                            publishProgress(
                                index,
                                pack,
                                PayloadBootstrapPhase.VERIFY,
                                verified,
                                total,
                            )
                        } != pack.sha256
                    ) {
                        GuestPayloads.release(archive)
                        error("${pack.name} failed integrity verification")
                    }
                }
                val expandedBytes = withContext(Dispatchers.IO) { expandedSize(archive) }
                publishProgress(index, pack, PayloadBootstrapPhase.INSTALL, 0L, expandedBytes)
                withContext(Dispatchers.IO) {
                    extractZipSafely(archive, staging) { installed, total ->
                        publishProgress(
                            index,
                            pack,
                            PayloadBootstrapPhase.INSTALL,
                            installed,
                            total,
                        )
                    }
                    GuestPayloads.release(archive)
                }
                completedPackBytes += pack.bytes
            }

            withContext(Dispatchers.IO) {
                writeVersionAtomically(staging, manifest.version)
                if (current.exists()) check(current.renameTo(rollback)) { "Could not preserve old payloads" }
                if (!staging.renameTo(current)) {
                    if (rollback.exists()) rollback.renameTo(current)
                    error("Could not publish downloaded payloads")
                }
                if (rollback.exists()) rollback.deleteRecursively()
            }
            mutableState.value = PayloadBootstrapState.Ready(manifest.version)
        } catch (failure: Throwable) {
            Log.e(TAG, "Payload bootstrap failed", failure)
            withContext(Dispatchers.IO) { if (staging.exists()) staging.deleteRecursively() }
            mutableState.value = PayloadBootstrapState.Failed(
                failure.message?.takeIf(String::isNotBlank) ?: "Payload setup failed",
            )
        }
    }

    private fun readManifest(): PayloadBootstrapManifest = context.assets
        .open(MANIFEST_ASSET)
        .bufferedReader()
        .use { PayloadBootstrapManifest.parse(it.readText()) }

    private fun expandedSize(archive: File): Long = ZipFile(archive).use { zip ->
        zip.entries().asSequence()
            .filterNot { it.isDirectory }
            .sumOf { it.size.coerceAtLeast(0L) }
            .coerceAtLeast(1L)
    }

    private fun extractZipSafely(
        archive: File,
        destination: File,
        onProgress: (completedBytes: Long, totalBytes: Long) -> Unit,
    ) {
        val canonicalRoot = destination.canonicalFile
        val totalBytes = expandedSize(archive)
        var completedBytes = 0L
        var lastPercent = -1
        fun report(force: Boolean = false) {
            val percent = (completedBytes.coerceAtMost(totalBytes) * 100L / totalBytes).toInt()
            if (force || percent != lastPercent) {
                lastPercent = percent
                onProgress(completedBytes.coerceAtMost(totalBytes), totalBytes)
            }
        }
        report(force = true)
        ZipInputStream(archive.inputStream().buffered(BUFFER_SIZE)).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val normalized = PayloadAssetStore.normalize(entry.name)
                val target = File(canonicalRoot, normalized).canonicalFile
                check(target.path.startsWith(canonicalRoot.path + File.separator)) {
                    "Payload archive contains an unsafe path"
                }
                if (entry.isDirectory) {
                    check(target.isDirectory || target.mkdirs()) { "Could not create payload directory" }
                } else {
                    val parent = checkNotNull(target.parentFile)
                    check(parent.isDirectory || parent.mkdirs()) { "Could not create payload directory" }
                    FileOutputStream(target).use { output ->
                        val buffer = ByteArray(BUFFER_SIZE)
                        while (true) {
                            val count = zip.read(buffer)
                            if (count < 0) break
                            output.write(buffer, 0, count)
                            completedBytes += count
                            report()
                        }
                    }
                }
                zip.closeEntry()
            }
        }
        completedBytes = totalBytes
        report(force = true)
    }

    private fun writeVersionAtomically(root: File, version: String) {
        val target = File(root, VERSION_FILE)
        val temporary = File(root, "$VERSION_FILE.tmp")
        FileOutputStream(temporary).use { output ->
            output.write(version.toByteArray(Charsets.UTF_8))
            output.fd.sync()
        }
        check(temporary.renameTo(target)) { "Could not publish payload version" }
    }

    private fun sha256(file: File, onProgress: (completedBytes: Long, totalBytes: Long) -> Unit): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val totalBytes = file.length().coerceAtLeast(1L)
        var completedBytes = 0L
        var lastPercent = -1
        file.inputStream().buffered(BUFFER_SIZE).use { input ->
            val buffer = ByteArray(BUFFER_SIZE)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
                completedBytes += count
                val percent = (completedBytes.coerceAtMost(totalBytes) * 100L / totalBytes).toInt()
                if (percent != lastPercent) {
                    lastPercent = percent
                    onProgress(completedBytes.coerceAtMost(totalBytes), totalBytes)
                }
            }
        }
        onProgress(totalBytes, totalBytes)
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun File.readTextOrNull(): String? = runCatching {
        takeIf(File::isFile)?.readText()?.trim()
    }.getOrNull()

    private fun friendlyName(name: String): String = when {
        name.startsWith("runtime-components") -> "tested Windows runtime"
        name.startsWith("game-mods") -> "dual-screen game mods"
        else -> name
    }

    private companion object {
        const val TAG = "PayloadBootstrap"
        const val MANIFEST_ASSET = "payload-bootstrap.json"
        const val VERSION_FILE = ".version"
        const val BUFFER_SIZE = 256 * 1024
    }
}
