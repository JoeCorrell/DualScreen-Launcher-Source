package com.dualscreen.launcher

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class GamePayloadPackageTest {
    private val game = "assassins-creed-ii"
    private fun hash(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun descriptor(status: String = "ready") = JSONObject().put("schema", 1).put("launcherApi", 1)
        .put("gameId", game).put("version", "test-1").put("status", status)
        .put("name", "$game-test-1.zip").put("bytes", 100).put("sha256", "a".repeat(64))
        .put("url", "${GamePayloadPackage.RELEASE}/$game-test-1.zip")
    private fun rejects(action: () -> Unit) {
        try { action(); fail("Expected rejection") } catch (_: IllegalArgumentException) { }
    }

    @Test fun descriptorRejectsDiagnosticWrongGameAndForeignUrls() {
        assertEquals("test-1", GamePayloadPackage.parse(descriptor().toString(), game).version)
        rejects { GamePayloadPackage.parse(descriptor("development").toString(), game) }
        rejects { GamePayloadPackage.parse(descriptor().put("gameId", "fallout3").toString(), game) }
        rejects { GamePayloadPackage.parse(descriptor().put("url", "https://example.com/mod.zip").toString(), game) }
        rejects { GamePayloadPackage.parse(descriptor().put("bytes", GamePayloadPackage.MAX_BYTES + 1).toString(), game) }
        rejects { GamePayloadPackage.parse(descriptor().put("launcherApi", 2).toString(), game) }
    }

    @Test fun developmentAndRevokedPackagesAreRejected() {
        val development = descriptor().put("status", "development").toString()
        rejects { GamePayloadPackage.parse(development, game) }
        rejects { GamePayloadPackage.parse(descriptor().put("status", "revoked").toString(), game) }
    }

    @Test fun unsafePathsAreRejected() {
        listOf("../dinput8.dll", "/dinput8.dll", "payload/../x", "C:/x", "payload\\x", "x//y", "x./y", "x /y").forEach {
            rejects { GamePayloadPackage.safePath(it) }
        }
    }

    @Test fun verifiedPackageExtractsAndRejectsTamperingOrExtraFiles() {
        val root = Files.createTempDirectory("ac2-package-test").toFile()
        try {
            val payload = "Test-only package bytes".toByteArray()
            val manifest = JSONObject().put("version", "test-1").put("files", JSONArray().put(
                JSONObject().put("source", "dinput8.dll").put("target", "dinput8.dll").put("sha256", hash(payload)))).toString()
            fun makeZip(name: String, extra: Boolean = false, corrupt: Boolean = false): File {
                val zip = File(root, name)
                ZipOutputStream(zip.outputStream()).use { output ->
                    fun entry(path: String, data: ByteArray) { output.putNextEntry(ZipEntry(path)); output.write(data); output.closeEntry() }
                    entry("manifest.json", manifest.toByteArray())
                    entry("payload/dinput8.dll", if (corrupt) "Corrupted".toByteArray() else payload)
                    if (extra) entry("unlisted.dll", payload)
                }
                return zip
            }
            fun packageFor(file: File) = GamePayloadPackage(game, "test-1", "$game-test-1.zip", file.length(), GamePayloadPackage.digest(file))
            val zip = makeZip("valid.zip")
            packageFor(zip).extract(zip, File(root, "valid"))
            assertArrayEquals(payload, File(root, "valid/payload/dinput8.dll").readBytes())
            val extra = makeZip("extra.zip", extra = true)
            rejects { packageFor(extra).extract(extra, File(root, "extra")) }
            assertFalse(File(root, "extra").exists())
            val bad = makeZip("bad.zip", corrupt = true)
            rejects { packageFor(bad).extract(bad, File(root, "bad")) }
            assertFalse(File(root, "bad").exists())
            val before = packageFor(zip); zip.appendText("tamper")
            rejects { before.extract(zip, File(root, "tamper")) }
        } finally { root.deleteRecursively() }
    }
}
