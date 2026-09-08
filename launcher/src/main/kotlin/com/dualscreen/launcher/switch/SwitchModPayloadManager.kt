package com.dualscreen.launcher.switch

import android.content.Context
import com.dualscreen.launcher.BuildConfig
import com.dualscreen.launcher.PayloadAssetStore
import com.dualscreen.launcher.isNewerVersion
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.security.MessageDigest
import org.json.JSONObject
import org.yuzu.yuzu_emu.model.Game

/**
 * Installs verified, remotely updateable Switch mod files from the Launcher's shared payload pack.
 * The managed directory is isolated from user mods and is replaced atomically per title.
 */
internal class SwitchModPayloadManager(private val context: Context) {
    private val versions = context.getSharedPreferences("switch_mod_payloads", Context.MODE_PRIVATE)

    fun synchronize(game: SwitchGameDefinition, installation: SwitchGameInstallation): Boolean {
        val prefix = "mods/switch/${game.id}"
        val manifestBytes = runCatching {
            PayloadAssetStore.open(context, "$prefix/manifest.json").use { input ->
                input.readAtMost(MAX_MANIFEST_BYTES + 1)
            }
        }.getOrElse { return false }
        require(manifestBytes.size <= MAX_MANIFEST_BYTES) { "${game.title} mod manifest is too large." }
        val bundled = runCatching {
            context.assets.open("$prefix/manifest.json").use { input ->
                val bytes = input.readAtMost(MAX_MANIFEST_BYTES + 1)
                require(bytes.size <= MAX_MANIFEST_BYTES)
                JSONObject(bytes.toString(Charsets.UTF_8))
            }
        }.getOrNull()
        val json = selectSwitchModManifest(JSONObject(manifestBytes.toString(Charsets.UTF_8)), bundled)
        require(json.getInt("schema") == 1) { "Unsupported ${game.title} mod manifest." }
        require(json.getString("titleId").equals(game.titleId, ignoreCase = true)) {
            "${game.title} mod payload targets the wrong title ID."
        }
        val version = json.getString("version").also { value ->
            require(value.isNotBlank() && value.length <= 96) { "Invalid ${game.title} mod version." }
        }
        val files = json.getJSONArray("files")
        require(files.length() <= MAX_FILES) { "${game.title} mod payload contains too many files." }
        when (json.optString("delivery", "data")) {
            "external-module" -> {
                require(files.length() == 0) { "External packages cannot mix ordinary add-on files" }
                val source = safeRelative(json.getString("package"))
                val stream = try { PayloadAssetStore.open(context, source) } catch (missing: java.io.FileNotFoundException) {
                    if (org.yuzu.yuzu_emu.companion.ExternalCompanionPackages.installedVersion(context, game.titleId) != null) {
                        android.util.Log.w("SwitchModPayload", "${game.id}: update package unavailable; retaining verified installed module")
                        return false
                    }
                    error("Automatic ${game.title} mod setup is incomplete: $source is missing. Return to the launcher and retry payload setup.")
                }
                stream.use { org.yuzu.yuzu_emu.companion.ExternalCompanionPackages.install(context, game.titleId, it, retainNewer = true) }
                android.util.Log.i("SwitchModPayload", "${game.id}: signed package synchronized automatically from $source")
                return true
            }
            "data" -> Unit
            "launcher-apk" -> {
                val minimumLauncherVersion = json.getString("minimumLauncherVersion")
                require(minimumLauncherVersion.isNotBlank()) {
                    "${game.title} mod manifest has no launcher version."
                }
                // The APK updater owns native/emulator-code mod revisions. Do not record a remote
                // manifest as installed until the matching signed launcher APK is actually active.
                if (isNewerVersion(minimumLauncherVersion, BuildConfig.VERSION_NAME)) return false
                require(files.length() == 0) {
                    "Embedded ${game.title} mod manifests cannot install add-on files."
                }
            }
            else -> error("Unsupported ${game.title} mod delivery mode.")
        }
        if (versions.getString(game.id, null) == version) return false

        if (files.length() == 0) {
            check(versions.edit().putString(game.id, version).commit()) {
                "Could not record ${game.title} mod payload version."
            }
            return true
        }

        val model = Game(installation.detectedTitle, installation.baseUri, game.programId)
        val target = File(model.addonDir, MANAGED_MOD_NAME)
        val parent = requireNotNull(target.parentFile)
        require(parent.isDirectory || parent.mkdirs()) { "Could not create Eden's mod directory." }
        val staging = File(parent, ".$MANAGED_MOD_NAME.staging-${System.nanoTime()}")
        val backup = File(parent, ".$MANAGED_MOD_NAME.backup")
        require(staging.mkdirs()) { "Could not stage ${game.title} mod update." }
        try {
            repeat(files.length()) { index ->
                val entry = files.getJSONObject(index)
                val source = safeRelative(entry.getString("source"))
                val destination = safeRelative(entry.getString("target"))
                val expectedHash = entry.getString("sha256").lowercase()
                require(expectedHash.matches(Regex("[0-9a-f]{64}"))) { "Invalid mod file checksum." }
                val output = File(staging, destination).canonicalFile
                require(output.path.startsWith(staging.canonicalPath + File.separator)) {
                    "Unsafe Switch mod destination."
                }
                require(output.parentFile?.let { it.isDirectory || it.mkdirs() } != false) {
                    "Could not create Switch mod folder."
                }
                val digest = MessageDigest.getInstance("SHA-256")
                PayloadAssetStore.open(context, "$prefix/payload/$source").use { input ->
                    output.outputStream().buffered().use { destinationStream ->
                        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                        var total = 0L
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            total += count
                            require(total <= MAX_FILE_BYTES) { "$source exceeds the Switch mod file limit." }
                            digest.update(buffer, 0, count)
                            destinationStream.write(buffer, 0, count)
                        }
                    }
                }
                require(digest.digest().joinToString("") { "%02x".format(it) } == expectedHash) {
                    "$source failed Switch mod verification."
                }
            }

            if (backup.exists()) check(backup.deleteRecursively()) { "Could not clear old mod backup." }
            if (target.exists()) check(target.renameTo(backup)) { "Could not preserve the current Switch mod." }
            try {
                check(staging.renameTo(target)) { "Could not publish the Switch mod update." }
                check(versions.edit().putString(game.id, version).commit()) {
                    "Could not record the Switch mod update."
                }
                if (backup.exists()) check(backup.deleteRecursively()) { "Could not clear the mod backup." }
            } catch (failure: Throwable) {
                if (target.exists()) target.deleteRecursively()
                if (backup.exists()) backup.renameTo(target)
                throw failure
            }
        } finally {
            if (staging.exists()) staging.deleteRecursively()
        }
        return true
    }

    private fun safeRelative(value: String): String {
        val normalized = value.replace('\\', '/').trimStart('/')
        require(normalized.isNotBlank() && normalized.length <= 240) { "Invalid Switch mod path." }
        require(value.firstOrNull() != '/' && !Regex("^[A-Za-z]:").containsMatchIn(value)) {
            "Switch mod paths must be relative."
        }
        require(normalized.split('/').none { it.isBlank() || it == "." || it == ".." }) {
            "Unsafe Switch mod path."
        }
        return normalized
    }

    private companion object {
        const val MANAGED_MOD_NAME = "Dualscreen Launcher"
        const val MAX_MANIFEST_BYTES = 256 * 1024
        const val MAX_FILES = 10_000
        const val MAX_FILE_BYTES = 2_000_000_000L
    }
}

private fun InputStream.readAtMost(limit: Int): ByteArray {
    require(limit > 0)
    val output = ByteArrayOutputStream(minOf(limit, DEFAULT_BUFFER_SIZE))
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var remaining = limit
    while (remaining > 0) {
        val count = read(buffer, 0, minOf(buffer.size, remaining))
        if (count < 0) break
        output.write(buffer, 0, count)
        remaining -= count
    }
    return output.toByteArray()
}
