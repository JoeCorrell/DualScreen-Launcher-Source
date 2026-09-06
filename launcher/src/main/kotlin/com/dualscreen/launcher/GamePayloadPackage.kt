package com.dualscreen.launcher

import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.Locale
import java.util.zip.ZipFile

/** A independently versioned PC mod package. Executables remain outside the APK. */
internal data class GamePayloadPackage(
    val gameId: String,
    val version: String,
    val name: String,
    val bytes: Long,
    val sha256: String,
) {
    val url: String get() = "$RELEASE/$name"

    companion object {
        const val RELEASE = "https://github.com/JoeCorrell/DualScreen-Launcher-Payloads/releases/download/launcher-payloads-v1"
        const val MAX_BYTES = 256L * 1024 * 1024
        private val hashPattern = Regex("[0-9a-f]{64}")

        fun parse(text: String, gameId: String, allowDevelopment: Boolean = false): GamePayloadPackage {
            require(text.length <= 64 * 1024) { "Game payload descriptor is too large" }
            val json = JSONObject(text)
            require(json.getInt("schema") == 1 && json.getInt("launcherApi") == 1) { "Unsupported game payload API" }
            require(json.getString("gameId") == gameId) { "Game payload identity mismatch" }
            val status = json.getString("status")
            require(status == "ready" || (allowDevelopment && status == "development")) { "This mod has not passed game validation yet" }
            val version = json.getString("version")
            require(version.matches(Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,95}"))) { "Invalid game payload version" }
            val name = json.getString("name")
            require(name == "$gameId-$version.zip") { "Unexpected game payload archive name" }
            val bytes = json.getLong("bytes")
            require(bytes in 1..MAX_BYTES) { "Game payload exceeds download limit" }
            val hash = json.getString("sha256").lowercase(Locale.ROOT)
            require(hashPattern.matches(hash)) { "Invalid game payload digest" }
            val result = GamePayloadPackage(gameId, version, name, bytes, hash)
            require(json.getString("url") == result.url) { "Untrusted game payload URL" }
            return result
        }

        fun safePath(value: String): String {
            require(value.isNotEmpty() && value.length <= 240 && !value.contains('\\') &&
                !value.contains(':') && !value.startsWith('/') && value.none { it.code < 32 }) { "Unsafe mod path" }
            require(value.split('/').all { it.isNotBlank() && it != "." && it != ".." &&
                !it.endsWith('.') && !it.endsWith(' ') }) { "Unsafe mod path segment" }
            return value
        }

        fun digest(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { input ->
                val block = ByteArray(64 * 1024)
                while (true) { val n = input.read(block); if (n < 0) break; digest.update(block, 0, n) }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }
    }

    /** Strict allowlist: every extracted byte belongs to the hashed installer manifest. */
    fun extract(archive: File, destination: File) {
        require(archive.length() == bytes && digest(archive) == sha256) { "Game payload archive verification failed" }
        require(!destination.exists()) { "Payload staging already exists" }
        check(destination.mkdirs())
        try {
            ZipFile(archive).use { zip ->
                val entries = zip.entries().asSequence().toList()
                require(entries.size in 2..4097 && entries.none { it.isDirectory }) { "Invalid game payload entries" }
                require(entries.map { safePath(it.name).lowercase(Locale.ROOT) }.distinct().size == entries.size) { "Duplicate game payload paths" }
                val manifestEntry = zip.getEntry("manifest.json") ?: error("Missing mod manifest")
                require(manifestEntry.size in 1..(512 * 1024)) { "Invalid mod manifest size" }
                val manifestText = zip.getInputStream(manifestEntry).use { it.readBytesLimited(512 * 1024) }.toString(Charsets.UTF_8)
                val manifest = JSONObject(manifestText)
                require(manifest.getString("version") == version) { "Mod version mismatch" }
                require(!manifest.has("patches") && !manifest.has("remove")) { "This package API supports file overlays only" }
                val files = manifest.getJSONArray("files")
                require(files.length() in 1..4096 && entries.size == files.length() + 1) { "Unexpected files in mod package" }
                val sources = mutableSetOf<String>()
                val targets = mutableSetOf<String>()
                var expanded = manifestEntry.size
                for (i in 0 until files.length()) {
                    val file = files.getJSONObject(i)
                    val source = safePath(file.getString("source"))
                    val target = safePath(file.getString("target"))
                    require(sources.add(source.lowercase(Locale.ROOT)) && targets.add(target.lowercase(Locale.ROOT))) { "Duplicate mod file" }
                    val expected = file.getString("sha256").lowercase(Locale.ROOT)
                    require(expected.matches(Regex("[0-9a-f]{64}"))) { "Invalid mod file digest" }
                    val entry = zip.getEntry("payload/$source") ?: error("Missing mod file: $source")
                    require(entry.size in 1..MAX_BYTES && expanded <= MAX_BYTES - entry.size) { "Game payload expansion limit exceeded" }
                    expanded += entry.size
                    val output = File(destination, "payload/$source")
                    check(output.parentFile!!.isDirectory || output.parentFile!!.mkdirs())
                    zip.getInputStream(entry).use { input -> output.outputStream().use { sink ->
                        val buffer = ByteArray(64 * 1024); var written = 0L
                        while (true) {
                            val count = input.read(buffer); if (count < 0) break
                            written += count; require(written <= entry.size) { "Incorrect expanded mod size" }
                            sink.write(buffer, 0, count)
                        }
                        require(written == entry.size) { "Truncated mod file" }
                    } }
                    require(digest(output) == expected) { "Mod file verification failed: $source" }
                }
                File(destination, "manifest.json").writeText(manifestText)
            }
        } catch (failure: Throwable) { destination.deleteRecursively(); throw failure }
    }
}

internal fun java.io.InputStream.readBytesLimited(limit: Int): ByteArray {
    val result = java.io.ByteArrayOutputStream()
    val buffer = ByteArray(8192)
    while (true) {
        val n = read(buffer); if (n < 0) break
        require(result.size() <= limit - n) { "Response exceeds size limit" }
        result.write(buffer, 0, n)
    }
    return result.toByteArray()
}
