package com.dualscreen.launcher

import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Removes only the startup experiment installed by our AC II helper. */
internal object Ac2WowBox64Configuration {
    internal val startupSection = "# AC II startup compatibility v1\n" +
        "[AssassinsCreedIIGame.exe]\nBOX64_DYNAREC_CALLRET=0\n" +
        "BOX64_DYNAREC_BIGBLOCK=1\nBOX64_DYNAREC_SAFEFLAGS=2\nBOX64_DYNAREC_WAIT=0\n"

    fun restorePerformance(containerRoot: File): Boolean {
        val profile = File(containerRoot, ".wine/drive_c/users/xuser/.box64rc")
        if (!profile.isFile) return false
        check(profile.canonicalPath.startsWith(containerRoot.canonicalPath + File.separator))
        val current = profile.readText()
        val updated = current.replace(startupSection.replace("\n", "\r\n"), "")
            .replace(startupSection, "")
        if (updated == current) return false
        val backup = File(profile.parentFile, ".box64rc.before-ac2-performance")
        if (!backup.exists()) profile.copyTo(backup)
        if (updated.isBlank()) {
            Files.delete(profile.toPath())
        } else {
            val temporary = File.createTempFile("ac2-box64-", ".tmp", profile.parentFile)
            try {
                temporary.writeText(updated)
                Files.move(temporary.toPath(), profile.toPath(), StandardCopyOption.REPLACE_EXISTING)
            } finally {
                temporary.delete()
            }
        }
        return true
    }
}
