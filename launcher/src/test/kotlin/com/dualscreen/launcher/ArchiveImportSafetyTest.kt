package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import java.io.File
import java.nio.file.Files
import org.junit.Test

class ArchiveImportSafetyTest {
    @Test
    fun systemGpuDriverDoesNotRequireAProvisionedAdrenotoolsArchive() {
        assertThat(requiresAdrenotoolsProvisioning("System")).isFalse()
        assertThat(requiresAdrenotoolsProvisioning("system")).isFalse()
        assertThat(requiresAdrenotoolsProvisioning("turnip-25.1.0")).isTrue()
    }

    @Test
    fun gameArchivePathsAllowNestedWrappersAndWindowsSeparators() {
        assertThat(normalizeGameArchiveEntry("wrapper/one/two/Fallout3.exe"))
            .isEqualTo("wrapper/one/two/Fallout3.exe")
        assertThat(normalizeGameArchiveEntry("wrapper\\Data\\Fallout3.esm"))
            .isEqualTo("wrapper/Data/Fallout3.esm")
    }

    @Test
    fun gameArchivePathsRejectAbsoluteTraversalAndDrivePaths() {
        listOf("/Fallout3.exe", "../Fallout3.exe", "C:/Fallout3.exe", "a/../../b").forEach { path ->
            val failure = runCatching { normalizeGameArchiveEntry(path) }.exceptionOrNull()
            assertThat(failure).isInstanceOf(IllegalArgumentException::class.java)
        }
    }

    @Test
    fun fallout3OldBuildMessageExplainsTheRealCompatibilityFailure() {
        val game = checkNotNull(GameCatalog.byId("fallout3"))
        val message = unsupportedBuildSizeMessage(
            game = game,
            path = "Fallout3.exe",
            expected = 16_855_040L,
            observed = 15_044_024L,
        )
        assertThat(message).contains("1.7.0.3 was detected")
        assertThat(message).contains("requires Fallout 3 1.7.0.4")
        assertThat(message).contains("create a new ZIP")
    }

    @Test
    fun saveArchiveTargetRejectsPathsThatCanEscapeTheProfile() {
        val root = Files.createTempDirectory("save-import-safety").toFile()
        try {
            assertThat(safeZipTarget(root, "Saves/Save 1.fos"))
                .isEqualTo(File(root, "Saves/Save 1.fos").canonicalFile)
            listOf("/outside.fos", "../outside.fos", "C:/outside.fos").forEach { path ->
                val failure = runCatching { safeZipTarget(root, path) }.exceptionOrNull()
                assertThat(failure).isInstanceOf(IllegalArgumentException::class.java)
            }
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun savePayloadResolverFindsNestedBethesdaSavesDirectory() {
        val root = Files.createTempDirectory("save-import-layout").toFile()
        try {
            val saves = File(root, "export/Documents/My Games/Fallout3/Saves")
            assertThat(saves.mkdirs()).isTrue()
            File(saves, "Save 1.fos").writeText("save")
            assertThat(selectSavePayloadRoot(root, "Saves", stripSingleRoot = true))
                .isEqualTo(saves)
        } finally {
            root.deleteRecursively()
        }
    }
}
