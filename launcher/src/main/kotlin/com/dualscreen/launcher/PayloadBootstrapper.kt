package com.dualscreen.launcher

import android.content.Context
import android.os.StatFs
import android.os.SystemClock
import android.util.Log
import com.wemu.engine.contents.GuestPayloads
import com.wemu.engine.core.Callback
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.ByteArrayOutputStream
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream

internal enum class PayloadBootstrapPhase { DOWNLOAD, VERIFY, INSTALL }

private const val DOWNLOAD_WEIGHT = 0.68
private const val VERIFY_WEIGHT = 0.08
private const val INSTALL_WEIGHT = 0.24
private const val TRUSTED_PAYLOAD_REPOSITORY =
    "https://github.com/JoeCorrell/DualScreen-Launcher-Payloads"
private const val REMOTE_MANIFEST_NAME = "payload-bootstrap.json"

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
    data class Ready(val version: String, val updateMessage: String? = null) : PayloadBootstrapState
    data class Failed(val message: String) : PayloadBootstrapState
}

internal data class PayloadPack(
    val name: String,
    val root: String,
    val bytes: Long,
    val sha256: String,
    val url: String,
)

internal data class PayloadBootstrapManifest(
    val version: String,
    val repository: String,
    val tag: String,
    val packs: List<PayloadPack>,
) {
    val updateUrl: String
        get() = "$repository/releases/download/$tag/$REMOTE_MANIFEST_NAME"

    companion object {
        fun parse(text: String): PayloadBootstrapManifest {
            val json = JSONObject(text.trimStart('\uFEFF'))
            require(json.getInt("schema") == 1) { "Unsupported payload manifest schema" }
            val version = json.getString("version").trim()
            require(version.isNotBlank()) { "Payload manifest has no version" }
            val repository = json.getString("repository").trim().trimEnd('/')
            require(repository == TRUSTED_PAYLOAD_REPOSITORY) { "Untrusted payload repository" }
            val tag = json.getString("tag").trim()
            require(tag.matches(Regex("[A-Za-z0-9._-]+"))) { "Invalid payload release tag" }
            val releasePrefix = "$repository/releases/download/$tag/"
            val array = json.getJSONArray("packs")
            require(array.length() > 0) { "Payload manifest has no packs" }
            val packs = buildList {
                for (index in 0 until array.length()) {
                    val entry = array.getJSONObject(index)
                    val pack = PayloadPack(
                        name = entry.getString("name"),
                        root = entry.getString("root").trim(),
                        bytes = entry.getLong("bytes"),
                        sha256 = entry.getString("sha256").lowercase(),
                        url = entry.getString("url"),
                    )
                    require(pack.name.matches(Regex("[A-Za-z0-9._-]+"))) { "Unsafe payload name" }
                    require(pack.root.matches(Regex("[A-Za-z0-9._-]+"))) { "Unsafe payload root" }
                    require(pack.bytes > 0) { "Payload size must be positive" }
                    require(pack.sha256.matches(Regex("[0-9a-f]{64}"))) { "Invalid payload checksum" }
                    require(pack.url == "$releasePrefix${pack.name}") { "Untrusted payload URL" }
                    add(pack)
                }
            }
            require(packs.map(PayloadPack::name).distinct().size == packs.size) {
                "Payload names must be unique"
            }
            require(packs.map(PayloadPack::root).distinct().size == packs.size) {
                "Payload roots must be unique"
            }
            return PayloadBootstrapManifest(version, repository, tag, packs)
        }
    }
}

internal fun isNewerVersion(candidate: String, current: String): Boolean {
    fun tokens(value: String): List<String> = Regex("[0-9]+|[A-Za-z]+")
        .findAll(value.trim().removePrefix("v").removePrefix("V"))
        .map { it.value.lowercase() }
        .toList()
    fun compareNumbers(left: String, right: String): Int {
        val a = left.trimStart('0').ifEmpty { "0" }
        val b = right.trimStart('0').ifEmpty { "0" }
        return if (a.length != b.length) a.length.compareTo(b.length) else a.compareTo(b)
    }

    val left = tokens(candidate)
    val right = tokens(current)
    val count = maxOf(left.size, right.size)
    for (index in 0 until count) {
        val a = left.getOrNull(index) ?: "0"
        val b = right.getOrNull(index) ?: "0"
        val comparison = when {
            a.all(Char::isDigit) && b.all(Char::isDigit) -> compareNumbers(a, b)
            a.all(Char::isDigit) -> 1
            b.all(Char::isDigit) -> -1
            else -> a.compareTo(b)
        }
        if (comparison != 0) return comparison > 0
    }
    return false
}

internal class PayloadBootstrapper(private val context: Context) {
    private val mutex = Mutex()
    private val mutableState = MutableStateFlow<PayloadBootstrapState>(PayloadBootstrapState.Checking)
    val state: StateFlow<PayloadBootstrapState> = mutableState.asStateFlow()
    private var lastRemoteCheckAtMs = 0L

    suspend fun ensureReady(force: Boolean = false) = mutex.withLock {
        val embedded = runCatching { readEmbeddedManifest() }.getOrElse { failure ->
            mutableState.value = PayloadBootstrapState.Failed(
                "The launcher payload manifest is invalid: ${failure.message}",
            )
            return@withLock
        }
        val current = PayloadAssetStore.root(context)
        val currentVersion = File(current, VERSION_FILE).readTextOrNull()
        if (currentVersion == null) {
            mutableState.value = PayloadBootstrapState.Checking
        } else {
            mutableState.value = PayloadBootstrapState.Ready(currentVersion)
        }

        var target = when {
            currentVersion == null -> embedded
            force -> embedded
            isNewerVersion(embedded.version, currentVersion) -> embedded
            else -> null
        }
        val now = SystemClock.elapsedRealtime()
        val shouldCheckRemote = force || now - lastRemoteCheckAtMs >= REMOTE_CHECK_INTERVAL_MS
        if (shouldCheckRemote) {
            lastRemoteCheckAtMs = now
            runCatching { withContext(Dispatchers.IO) { fetchRemoteManifest(embedded) } }
                .onSuccess { remote ->
                    val baseline = target?.version ?: currentVersion ?: embedded.version
                    if (isNewerVersion(remote.version, baseline)) target = remote
                }
                .onFailure { Log.w(TAG, "Could not check for payload updates", it) }
        }

        val selected = target
        if (selected == null) {
            mutableState.value = PayloadBootstrapState.Ready(checkNotNull(currentVersion))
            return@withLock
        }
        try {
            installManifest(selected, current, currentVersion, force)
            mutableState.value = PayloadBootstrapState.Ready(selected.version)
        } catch (failure: Throwable) {
            if (failure is CancellationException) throw failure
            Log.e(TAG, "Payload bootstrap failed", failure)
            val message = failure.message?.takeIf(String::isNotBlank) ?: "Payload setup failed"
            mutableState.value = if (currentVersion != null) {
                PayloadBootstrapState.Ready(currentVersion, "Payload update failed: $message")
            } else {
                PayloadBootstrapState.Failed(message)
            }
        }
    }

    private suspend fun installManifest(
        manifest: PayloadBootstrapManifest,
        current: File,
        currentVersion: String?,
        force: Boolean,
    ) {
        val base = checkNotNull(current.parentFile)
        withContext(Dispatchers.IO) {
            if (!base.isDirectory) check(base.mkdirs()) { "Could not create payload storage" }
            if (!current.isDirectory) check(current.mkdirs()) { "Could not create payload store" }
        }
        val packState = withContext(Dispatchers.IO) { readPackState(current) }.toMutableMap()
        val pending = manifest.packs.filterNot { pack ->
            !force && packIsCurrent(current, currentVersion, packState, pack)
        }
        if (pending.isEmpty()) {
            manifest.packs.forEach { pack -> packState[pack.root] = pack.sha256 }
            withContext(Dispatchers.IO) {
                writePackStateAtomically(current, packState)
                writeVersionAtomically(current, manifest.version)
            }
            Log.i(TAG, "Payload store is current at ${manifest.version}")
            return
        }

        try {
            val totalPackBytes = pending.sumOf(PayloadPack::bytes).coerceAtLeast(1L)
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
                        totalPacks = pending.size,
                        phase = phase,
                        phaseCompletedBytes = safeCompleted,
                        phaseTotalBytes = safeTotal,
                    )
                }
            }

            pending.forEachIndexed { index, pack ->
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
                val availableBytes = withContext(Dispatchers.IO) { StatFs(base.absolutePath).availableBytes }
                check(expandedBytes <= extractionByteBudget(availableBytes, STORAGE_RESERVE)) {
                    "Not enough free space to install ${friendlyName(pack.name)}"
                }
                publishProgress(index, pack, PayloadBootstrapPhase.INSTALL, 0L, expandedBytes)
                val staging = File(base, "staging-${pack.root}-${UUID.randomUUID()}")
                withContext(Dispatchers.IO) {
                    check(staging.mkdirs()) { "Could not create payload staging" }
                    extractZipSafely(archive, staging, pack.root) { installed, total ->
                        publishProgress(
                            index,
                            pack,
                            PayloadBootstrapPhase.INSTALL,
                            installed,
                            total,
                        )
                    }
                    publishPackRoot(current, staging, pack.root)
                    GuestPayloads.release(archive)
                }
                packState[pack.root] = pack.sha256
                withContext(Dispatchers.IO) { writePackStateAtomically(current, packState) }
                Log.i(TAG, "Published ${pack.root} payload ${pack.name} sha256=${pack.sha256}")
                completedPackBytes += pack.bytes
            }

            withContext(Dispatchers.IO) {
                manifest.packs.forEach { pack -> packState[pack.root] = pack.sha256 }
                writePackStateAtomically(current, packState)
                writeVersionAtomically(current, manifest.version)
            }
            Log.i(TAG, "Payload store updated to ${manifest.version}")
        } catch (failure: Throwable) {
            withContext(NonCancellable + Dispatchers.IO) {
                base.listFiles()
                    ?.filter { it.name.startsWith("staging-") }
                    ?.forEach { staging ->
                        if (!staging.deleteRecursively()) {
                            Log.w(TAG, "Could not clear interrupted payload staging: $staging")
                        }
                    }
            }
            throw failure
        }
    }

    private fun readEmbeddedManifest(): PayloadBootstrapManifest = context.assets
        .open(MANIFEST_ASSET)
        .bufferedReader()
        .use { PayloadBootstrapManifest.parse(it.readText()) }

    private fun fetchRemoteManifest(embedded: PayloadBootstrapManifest): PayloadBootstrapManifest {
        val connection = (URL(embedded.updateUrl).openConnection() as HttpURLConnection).apply {
            connectTimeout = REMOTE_CONNECT_TIMEOUT_MS
            readTimeout = REMOTE_READ_TIMEOUT_MS
            instanceFollowRedirects = true
            setRequestProperty("Accept", "application/json")
            setRequestProperty("User-Agent", "DualScreen-Launcher/${installedVersionName()}")
        }
        return try {
            check(connection.responseCode in 200..299) {
                "Payload update server returned HTTP ${connection.responseCode}"
            }
            val bytes = connection.inputStream.use { input -> readLimited(input = input) }
            check(bytes.size <= MAX_MANIFEST_BYTES) { "Remote payload manifest is too large" }
            PayloadBootstrapManifest.parse(bytes.toString(Charsets.UTF_8))
        } finally {
            connection.disconnect()
        }
    }

    private fun expandedSize(archive: File): Long = ZipFile(archive).use { zip ->
        var entries = 0
        var total = 0L
        val enumeration = zip.entries()
        while (enumeration.hasMoreElements()) {
            val entry = enumeration.nextElement()
            check(++entries <= MAX_PAYLOAD_ENTRIES) { "Payload archive contains too many files" }
            if (!entry.isDirectory) {
                check(entry.size >= 0L) { "Payload archive contains an entry with no declared size" }
                total = Math.addExact(total, entry.size)
                check(total <= MAX_PAYLOAD_EXPANDED_BYTES) { "Payload archive expands beyond the safety limit" }
            }
        }
        check(entries > 0) { "Payload archive is empty" }
        total.coerceAtLeast(1L)
    }

    private fun extractZipSafely(
        archive: File,
        destination: File,
        requiredRoot: String,
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
                check(normalized == requiredRoot || normalized.startsWith("$requiredRoot/")) {
                    "${archive.name} contains a file outside its $requiredRoot payload root"
                }
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
                            completedBytes = Math.addExact(completedBytes, count.toLong())
                            check(completedBytes <= totalBytes) {
                                "Payload archive expanded beyond its verified directory size"
                            }
                            report()
                        }
                    }
                }
                zip.closeEntry()
            }
        }
        check(completedBytes == totalBytes) {
            "Payload extraction was incomplete: expected $totalBytes bytes, wrote $completedBytes"
        }
        report(force = true)
    }

    private fun packIsCurrent(
        current: File,
        currentVersion: String?,
        packState: Map<String, String>,
        pack: PayloadPack,
    ): Boolean {
        if (packState[pack.root] == pack.sha256) return true
        val root = File(current, pack.root)
        val legacyInstallIsPresent = root.isDirectory && !root.list().isNullOrEmpty()
        return packState[pack.root] == null &&
            currentVersion != null &&
            pack.name.contains(currentVersion) &&
            legacyInstallIsPresent
    }

    private fun publishPackRoot(current: File, staging: File, rootName: String) {
        val stagedRoot = File(staging, rootName)
        check(stagedRoot.isDirectory) { "Payload pack did not contain its $rootName root" }
        val target = File(current, rootName)
        val rollback = File(checkNotNull(current.parentFile), "rollback-$rootName-${UUID.randomUUID()}")
        var published = false
        try {
            if (target.exists()) check(target.renameTo(rollback)) {
                "Could not preserve the current $rootName payload"
            }
            if (!stagedRoot.renameTo(target)) {
                error("Could not publish the $rootName payload update")
            }
            published = true
            if (rollback.exists() && !rollback.deleteRecursively()) {
                // The new verified pack is already live. Retrying removal on the next
                // bootstrap is safer than rolling a valid update back for cleanup alone.
                Log.w(TAG, "Could not remove the replaced $rootName payload")
            }
        } catch (failure: Throwable) {
            val restoreFailure = runCatching {
                if (published && target.exists()) {
                    check(target.deleteRecursively()) { "Could not remove incomplete $rootName payload" }
                }
                if (!target.exists() && rollback.exists()) {
                    check(rollback.renameTo(target)) { "Could not restore the previous $rootName payload" }
                }
            }.exceptionOrNull()
            if (restoreFailure != null) {
                failure.addSuppressed(restoreFailure)
                Log.e(TAG, "Payload rollback was incomplete for $rootName", restoreFailure)
            }
            throw failure
        } finally {
            if (staging.exists() && !staging.deleteRecursively()) {
                Log.w(TAG, "Could not clear payload staging: $staging")
            }
        }
    }

    private fun readPackState(root: File): Map<String, String> = runCatching {
        val json = JSONObject(File(root, PACK_STATE_FILE).readText())
        buildMap {
            json.keys().forEach { name ->
                val hash = json.getString(name).lowercase()
                if (name.matches(Regex("[A-Za-z0-9._-]+")) &&
                    hash.matches(Regex("[0-9a-f]{64}"))
                ) {
                    put(name, hash)
                }
            }
        }
    }.getOrDefault(emptyMap())

    private fun writePackStateAtomically(root: File, state: Map<String, String>) {
        val json = JSONObject()
        state.toSortedMap().forEach { (name, hash) -> json.put(name, hash) }
        writeTextAtomically(root, PACK_STATE_FILE, json.toString())
    }

    private fun writeVersionAtomically(root: File, version: String) {
        writeTextAtomically(root, VERSION_FILE, version)
    }

    private fun writeTextAtomically(root: File, name: String, value: String) {
        val target = File(root, name)
        val temporary = File(root, "$name.tmp")
        if (temporary.exists()) check(temporary.delete()) { "Could not replace $name staging" }
        FileOutputStream(temporary).use { output ->
            output.write(value.toByteArray(Charsets.UTF_8))
            output.fd.sync()
        }
        if (target.exists()) check(target.delete()) { "Could not replace $name" }
        check(temporary.renameTo(target)) { "Could not publish $name" }
    }

    private fun readLimited(input: java.io.InputStream): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(8 * 1024)
        var remaining = MAX_MANIFEST_BYTES + 1
        while (remaining > 0) {
            val count = input.read(buffer, 0, minOf(buffer.size, remaining))
            if (count < 0) break
            output.write(buffer, 0, count)
            remaining -= count
        }
        return output.toByteArray()
    }

    @Suppress("DEPRECATION")
    private fun installedVersionName(): String = runCatching {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName
    }.getOrNull()?.takeIf(String::isNotBlank) ?: "0"

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
        const val PACK_STATE_FILE = ".packs.json"
        const val BUFFER_SIZE = 256 * 1024
        const val MAX_MANIFEST_BYTES = 256 * 1024
        const val STORAGE_RESERVE = 512L * 1024L * 1024L
        const val MAX_PAYLOAD_ENTRIES = 250_000
        const val MAX_PAYLOAD_EXPANDED_BYTES = 16L * 1024L * 1024L * 1024L
        const val REMOTE_CONNECT_TIMEOUT_MS = 5_000
        const val REMOTE_READ_TIMEOUT_MS = 8_000
        const val REMOTE_CHECK_INTERVAL_MS = 60_000L
    }
}
