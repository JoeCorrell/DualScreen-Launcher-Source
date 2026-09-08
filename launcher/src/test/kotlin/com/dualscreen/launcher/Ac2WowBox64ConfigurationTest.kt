package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import java.io.File
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class Ac2WowBox64ConfigurationTest {
    @get:Rule val temporary = TemporaryFolder()
    private fun profile(root: File, text: String) =
        File(root, ".wine/drive_c/users/xuser/.box64rc").apply { parentFile.mkdirs(); writeText(text) }

    @Test fun removesOnlyOurSectionAndPreservesOtherGamesAndBackup() {
        val root = temporary.newFolder()
        val other = "[AnotherGame.exe]\r\nBOX64_DYNAREC_CALLRET=1\r\n"
        val original = other + Ac2WowBox64Configuration.startupSection.replace("\n", "\r\n")
        val file = profile(root, original)
        assertThat(Ac2WowBox64Configuration.restorePerformance(root)).isTrue()
        assertThat(file.readText()).isEqualTo(other)
        assertThat(File(file.parentFile, ".box64rc.before-ac2-performance").readText()).isEqualTo(original)
        assertThat(Ac2WowBox64Configuration.restorePerformance(root)).isFalse()
    }

    @Test fun removesExperimentOnlyFileAndDoesNotTouchCustomProfiles() {
        val root = temporary.newFolder()
        val file = profile(root, "\r\n" + Ac2WowBox64Configuration.startupSection)
        assertThat(Ac2WowBox64Configuration.restorePerformance(root)).isTrue()
        assertThat(file.exists()).isFalse()
        val custom = "[AssassinsCreedIIGame.exe]\nBOX64_DYNAREC_CALLRET=1\n"
        file.writeText(custom)
        assertThat(Ac2WowBox64Configuration.restorePerformance(root)).isFalse()
        assertThat(file.readText()).isEqualTo(custom)
    }
}
