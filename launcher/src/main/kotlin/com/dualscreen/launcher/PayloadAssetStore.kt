package com.dualscreen.launcher

import android.content.Context
import java.io.File
import java.io.FileNotFoundException
import java.io.InputStream

/** Resolves a payload from first-boot storage, with bundled assets as a migration fallback. */
internal object PayloadAssetStore {
    private const val DIRECTORY = "payload-assets/current"

    fun root(context: Context): File = File(context.filesDir, DIRECTORY)

    fun open(context: Context, relativePath: String): InputStream {
        val normalized = normalize(relativePath)
        val root = root(context).canonicalFile
        val downloaded = File(root, normalized).canonicalFile
        check(downloaded.path.startsWith(root.path + File.separator)) {
            "Payload path escapes the payload store"
        }
        if (downloaded.isFile) return downloaded.inputStream().buffered()
        return runCatching { context.assets.open(normalized) }.getOrElse {
            throw FileNotFoundException(
                "Required launcher payload is unavailable: $normalized. " +
                    "Return to the launcher and retry first-boot setup.",
            )
        }
    }

    internal fun normalize(relativePath: String): String {
        val normalized = relativePath.replace('\\', '/').trimStart('/')
        require(normalized.isNotBlank()) { "Payload path must not be blank" }
        require(relativePath.firstOrNull() != '/' && !Regex("^[A-Za-z]:").containsMatchIn(relativePath)) {
            "Payload path must be relative"
        }
        require(normalized.split('/').none { it.isBlank() || it == "." || it == ".." }) {
            "Payload path is unsafe"
        }
        return normalized
    }
}
