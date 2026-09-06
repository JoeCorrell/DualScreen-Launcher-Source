package com.dualscreen.launcher

import android.content.Context
import android.os.StatFs
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.UUID
import org.json.JSONObject

/** Download/check once before installation or launch, never in gameplay's frame path. */
internal object OnDemandGamePayloads {
    private val games = setOf("assassins-creed-ii")
    private val lock = Any()
    fun manages(gameId: String): Boolean = gameId in games

    private fun root(context: Context, gameId: String): File {
        require(manages(gameId))
        return File(context.filesDir, "game-payloads/$gameId")
    }

    fun open(context: Context, normalized: String): java.io.InputStream? {
        val parts = normalized.split('/', limit = 3)
        if (parts.size != 3 || parts[0] != "mods" || !manages(parts[1])) return null
        val directory = current(root(context, parts[1])) ?: error("Download the ${parts[1]} mod before installing or launching it")
        val path = GamePayloadPackage.safePath(parts[2])
        return File(directory, path).inputStream().buffered()
    }

    private fun current(root: File): File? {
        val hash = File(root, "current").takeIf { it.isFile && it.length() <= 64 }?.readText() ?: return null
        if (!hash.matches(Regex("[0-9a-f]{64}"))) return null
        return File(root, hash).takeIf { File(it, "manifest.json").isFile }
    }

    // Explicit, app-private device-test opt-in pins ONE verified development package.
    // Neither downloaded descriptors nor external storage can enable this override.
    private fun developmentEnabled(directory: File): Boolean = runCatching {
        val optIn = File(directory.parentFile, "development-package")
        optIn.isFile && optIn.length() == 64L && optIn.readText() == directory.name &&
            directory.name.matches(Regex("[0-9a-f]{64}"))
    }.getOrDefault(false)

    private fun verifiedCache(directory: File?, gameId: String): Boolean = runCatching {
        if (directory == null) return@runCatching false
        val descriptorFile = File(directory, "descriptor.json")
        val manifestFile = File(directory, "manifest.json")
        if (!descriptorFile.isFile || descriptorFile.length() > 64 * 1024 || manifestFile.length() > 512 * 1024)
            return@runCatching false
        val descriptor = GamePayloadPackage.parse(descriptorFile.readText(), gameId, developmentEnabled(directory))
        if (descriptor.sha256 != directory.name) return@runCatching false
        val manifest = JSONObject(manifestFile.readText())
        if (manifest.getString("version") != descriptor.version) return@runCatching false
        val files = manifest.getJSONArray("files")
        if (files.length() !in 1..4096) return@runCatching false
        var total = 0L
        for (i in 0 until files.length()) {
            val entry = files.getJSONObject(i)
            val source = GamePayloadPackage.safePath(entry.getString("source"))
            val file = File(directory, "payload/$source")
            if (!file.isFile || file.length() !in 1..GamePayloadPackage.MAX_BYTES) return@runCatching false
            total += file.length()
            if (total > GamePayloadPackage.MAX_BYTES || GamePayloadPackage.digest(file) != entry.getString("sha256").lowercase(java.util.Locale.ROOT))
                return@runCatching false
        }
        true
    }.getOrDefault(false)

    suspend fun ensure(context: Context, gameId: String) {
        if (!manages(gameId)) return
        withContext(Dispatchers.IO) { synchronized(lock) { ensureBlocking(context, gameId) } }
    }

    private fun connection(url: String): HttpURLConnection = (URL(url).openConnection() as HttpURLConnection).apply {
        connectTimeout = 15_000; readTimeout = 30_000
        instanceFollowRedirects = true
        setRequestProperty("Accept-Encoding", "identity")
        setRequestProperty("User-Agent", "DualScreen-Launcher-Payloads/1")
    }

    private fun ensureBlocking(context: Context, gameId: String) {
        val root = root(context, gameId)
        check(root.isDirectory || root.mkdirs()) { "Could not create game payload storage" }
        val existing = current(root)
        val cacheVerified = verifiedCache(existing, gameId)
        if (cacheVerified && existing != null && developmentEnabled(existing)) {
            Log.i("GamePayloads", "Using explicitly installed development payload: $gameId ${existing.name}")
            return
        }
        // An unreachable catalog permits the already verified cached payload. An
        // invalid/revoked descriptor is an error and must not silently activate it.
        val text = try {
            val connection = connection("${GamePayloadPackage.RELEASE}/$gameId-payload.json")
            try {
                check(connection.responseCode == 200) { "Game payload catalog returned HTTP ${connection.responseCode}" }
                connection.inputStream.use { it.readBytesLimited(64 * 1024).toString(Charsets.UTF_8) }
            } finally { connection.disconnect() }
        } catch (failure: java.io.IOException) {
            if (!cacheVerified) throw failure
            Log.w("GamePayloads", "Catalog unavailable; using cached $gameId payload", failure)
            return
        }
        val descriptor = GamePayloadPackage.parse(text, gameId)
        if (cacheVerified && existing?.name == descriptor.sha256) return
        val required = descriptor.bytes + GamePayloadPackage.MAX_BYTES + 32L * 1024 * 1024
        check(StatFs(root.path).availableBytes > required) { "Not enough space to install the game mod safely" }
        val archive = File(root, ".download-${UUID.randomUUID()}.zip")
        val staging = File(root, ".staging-${UUID.randomUUID()}")
        val pointer = File(root, ".current-${UUID.randomUUID()}")
        try {
            val connection = connection(descriptor.url)
            try {
                check(connection.responseCode == 200) { "Game payload download returned HTTP ${connection.responseCode}" }
                connection.inputStream.use { input -> archive.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024); var received = 0L
                    while (true) {
                        val count = input.read(buffer); if (count < 0) break
                        received += count; check(received <= descriptor.bytes) { "Game payload exceeds declared size" }
                        output.write(buffer, 0, count)
                    }
                    check(received == descriptor.bytes) { "Incomplete game payload download" }
                } }
            } finally { connection.disconnect() }
            descriptor.extract(archive, staging)
            File(staging, "descriptor.json").writeText(text)
            val published = File(root, descriptor.sha256)
            // Do not delete prior versions: a reader may still have that version open.
            if (published.exists()) {
                if (!verifiedCache(published, gameId)) {
                    check(published.renameTo(File(root, ".invalid-${UUID.randomUUID()}"))) { "Could not quarantine damaged game payload" }
                }
            }
            if (!published.exists()) check(staging.renameTo(published)) { "Could not publish verified game payload" }
            pointer.writeText(descriptor.sha256)
            Files.move(pointer.toPath(), File(root, "current").toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
            Log.i("GamePayloads", "Activated $gameId ${descriptor.version} (${descriptor.sha256})")
        } finally { archive.delete(); staging.deleteRecursively(); pointer.delete() }
    }
}
