package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test
import java.io.File
import java.nio.file.Files

class ExecutableDllOverrideConfigurationTest {
    @Test
    fun overrideIsExecutableScopedAndIdempotent() {
        val root = Files.createTempDirectory("wfc-override").toFile()
        try {
            val prefix = File(root, ".wine").apply { mkdirs() }
            val registry = File(prefix, "user.reg").apply {
                writeText("WINE REGISTRY Version 2\n\n[Software\\Existing] 1\n#time=1\n\"Keep\"=\"yes\"\n")
            }
            ExecutableDllOverrideConfiguration.ensure(
                root,
                "TWFC.exe",
                mapOf("dinput8" to "native,builtin"),
            )
            ExecutableDllOverrideConfiguration.ensure(
                root,
                "TWFC.exe",
                mapOf("dinput8" to "native,builtin"),
            )
            val text = registry.readText()
            assertThat(text).contains("Software\\\\Wine\\\\AppDefaults\\\\TWFC.exe\\\\DllOverrides")
            assertThat(text).contains("\"dinput8\"=\"native,builtin\"")
            assertThat(text).contains("\"Keep\"=\"yes\"")
            assertThat(text).doesNotContain("Software\\\\Wine\\\\DllOverrides")
        } finally {
            root.deleteRecursively()
        }
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsPathLikeExecutableNames() {
        val root = Files.createTempDirectory("wfc-override-invalid").toFile()
        try {
            File(root, ".wine").mkdirs()
            File(root, ".wine/user.reg").writeText("WINE REGISTRY Version 2\n")
            ExecutableDllOverrideConfiguration.ensure(
                root,
                "..\\TWFC.exe",
                mapOf("dinput8" to "native,builtin"),
            )
        } finally {
            root.deleteRecursively()
        }
    }
}
