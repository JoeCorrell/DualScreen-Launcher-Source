package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class WfcPrerequisiteConfigurationTest {
    @get:Rule
    val temporary = TemporaryFolder()

    @Test
    fun receiptAloneNeverAllowsLaunch() {
        val container = temporary.newFolder("container")
        val receipt = WfcPrerequisiteConfiguration.receiptFile(container)
        receipt.parentFile!!.mkdirs()
        receipt.writeText(WfcPrerequisiteConfiguration.RECEIPT_TEXT)

        assertThat(WfcPrerequisiteConfiguration.prerequisitesAreCurrent(container)).isFalse()
    }

    @Test
    fun exactReceiptAndAllInstalledComponentsAllowDirectLaunch() {
        val container = temporary.newFolder("ready-container")
        writeInstalledFiles(container)
        val receipt = WfcPrerequisiteConfiguration.receiptFile(container)
        receipt.parentFile!!.mkdirs()
        receipt.writeText(WfcPrerequisiteConfiguration.RECEIPT_TEXT + "\n")

        assertThat(WfcPrerequisiteConfiguration.installedFilesArePresent(container)).isTrue()
        assertThat(WfcPrerequisiteConfiguration.prerequisitesAreCurrent(container)).isTrue()
    }

    @Test
    fun installedComponentsDoNotRequireRedistributableSourceAgain() {
        val container = temporary.newFolder("ready-without-source-container")
        writeInstalledFiles(container)
        val receipt = WfcPrerequisiteConfiguration.receiptFile(container)
        receipt.parentFile!!.mkdirs()
        receipt.writeText(WfcPrerequisiteConfiguration.RECEIPT_TEXT)
        val executable = temporary.root.resolve("game/Binaries/TWFC.exe")
        executable.parentFile!!.mkdirs()
        executable.writeBytes(byteArrayOf(1))

        val preparation = WfcPrerequisiteConfiguration.prepare(
            containerRoot = container,
            twfcExecutable = executable,
            guestTwfcPath = "F:\\Games\\Transformers WFC\\Binaries\\TWFC.exe",
        )

        assertThat(preparation.installed).isTrue()
        assertThat(preparation.bootstrap.isFile).isFalse()
    }

    @Test
    fun staleReceiptOrRemovedComponentForcesBootstrapAgain() {
        val container = temporary.newFolder("stale-container")
        val files = writeInstalledFiles(container)
        val receipt = WfcPrerequisiteConfiguration.receiptFile(container)
        receipt.parentFile!!.mkdirs()
        receipt.writeText("WFC_PREREQUISITES_V0\n")
        assertThat(WfcPrerequisiteConfiguration.prerequisitesAreCurrent(container)).isFalse()

        receipt.writeText(WfcPrerequisiteConfiguration.RECEIPT_TEXT)
        assertThat(files.physX.delete()).isTrue()
        assertThat(WfcPrerequisiteConfiguration.prerequisitesAreCurrent(container)).isFalse()

        files.physX.writeBytes(byteArrayOf(3))
        assertThat(files.xAudio.delete()).isTrue()
        assertThat(WfcPrerequisiteConfiguration.prerequisitesAreCurrent(container)).isFalse()
    }

    @Test
    fun bootstrapCommandQuotesPathsWithSpaces() {
        assertThat(
            WfcPrerequisiteConfiguration.bootstrapExecArgs(
                "F:\\Games\\Transformers WFC\\Binaries\\SecondScreenPrerequisites\\" +
                    "WfcPrerequisiteBootstrap.cmd",
            ),
        ).isEqualTo(
            "/d /s /c call \"F:\\Games\\Transformers WFC\\Binaries\\" +
                "SecondScreenPrerequisites\\WfcPrerequisiteBootstrap.cmd\"",
        )
    }

    private fun writeInstalledFiles(container: java.io.File): InstalledFiles {
        val system32 = container.resolve(".wine/drive_c/windows/system32").apply { mkdirs() }
        val syswow64 = container.resolve(".wine/drive_c/windows/syswow64").apply { mkdirs() }
        listOf("d3dx9_42.dll", "xactengine3_0.dll", "xaudio2_7.dll").forEach { name ->
            system32.resolve(name).writeBytes(byteArrayOf(1))
            syswow64.resolve(name).writeBytes(byteArrayOf(1))
        }
        val xAudio = system32.resolve("xaudio2_7.dll").apply { writeBytes(byteArrayOf(1)) }
        val winsxs = container.resolve(
            ".wine/drive_c/windows/winsxs/x86_microsoft.vc80.crt_test",
        ).apply { mkdirs() }
        winsxs.resolve("MSVCR80.DLL").writeBytes(byteArrayOf(2))
        val physX = container.resolve(
            ".wine/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Common/PhysXLoader.dll",
        )
        physX.parentFile!!.mkdirs()
        physX.writeBytes(byteArrayOf(3))
        return InstalledFiles(physX, xAudio)
    }

    private data class InstalledFiles(
        val physX: java.io.File,
        val xAudio: java.io.File,
    )
}
