package com.dualscreen.launcher

import com.wemu.engine.container.Container
import com.wemu.engine.container.Shortcut
import java.io.File
import java.security.MessageDigest

/**
 * Gates War for Cybertron behind its original, verified Windows redistributables.
 *
 * The redistributables live in the launcher-owned mod payload rather than
 * relying on the user's game copy. A small guest batch installs PhysX and the
 * Visual C++ runtime, verifies the DirectX/XAudio components provisioned by
 * WEMU's native ARM64EC-compatible component archives, writes a versioned
 * receipt, then starts TWFC.exe. A stale receipt or a missing installed DLL
 * selects the bootstrap again.
 */
internal object WfcPrerequisiteConfiguration {
    const val RECEIPT_TEXT = "WFC_PREREQUISITES_V2"

    private const val PREREQUISITE_DIRECTORY = "SecondScreenPrerequisites"
    private const val BOOTSTRAP_NAME = "WfcPrerequisiteBootstrap.cmd"
    private const val EXPECTED_REDIST_FILES = 149
    private const val EXPECTED_REDIST_BYTES = 148_785_768L

    private val installerRules = listOf(
        InstallerRule(
            relativePath = "PhysX_10.01.29_9.10.0129_SystemSoftware.exe",
            sha256 = "057c7bdb7a15dc7e8fb9ef5c8029dec15dd3719d234988e6e4528c3f04fbc33a",
        ),
        InstallerRule(
            relativePath = "vcredist_x86_2005sp1/vcredist_x86.exe",
            sha256 = "d6832398e3bc9156a660745f427dc1c2392ce4e9a872e04f41f62d0c6bae07a8",
        ),
        InstallerRule(
            relativePath = "DirectX/DXSETUP.exe",
            sha256 = "cdfdc371a373cd0f0daa00db46bae7e19258dd8ef7e521e57be96cbacdbb242f",
        ),
    )

    data class Preparation(
        val installed: Boolean,
        val receipt: File,
        val failure: File,
        val bootstrap: File,
        val guestBootstrapPath: String,
    )

    fun prepare(
        containerRoot: File,
        twfcExecutable: File,
        guestTwfcPath: String,
    ): Preparation {
        require(twfcExecutable.isFile && twfcExecutable.name.equals("TWFC.exe", ignoreCase = true)) {
            "The Transformers executable is unavailable"
        }
        val binaries = checkNotNull(twfcExecutable.parentFile) {
            "The Transformers Binaries directory is unavailable"
        }
        val prerequisiteRoot = File(binaries, PREREQUISITE_DIRECTORY)
        val bootstrap = File(prerequisiteRoot, BOOTSTRAP_NAME)
        val redist = File(prerequisiteRoot, "redist")

        val receipt = receiptFile(containerRoot)
        val failure = failureFile(containerRoot)
        val installed = prerequisitesAreCurrent(containerRoot)
        // The redistributable payload is only an installation source. Once the
        // versioned receipt and the installed guest components are present, a
        // later mod-only payload update must not gate game launch on that source
        // being copied to shared storage again.
        if (!installed) verifyBundledPayload(bootstrap, redist)
        if (!installed && failure.exists()) {
            check(failure.delete()) { "Could not clear the previous prerequisite failure state" }
        }
        val guestDirectory = guestTwfcPath.trim().trim('"')
            .substringBeforeLast('\\', missingDelimiterValue = "")
        require(guestDirectory.isNotBlank()) { "The Transformers guest path is invalid" }
        return Preparation(
            installed = installed,
            receipt = receipt,
            failure = failure,
            bootstrap = bootstrap,
            guestBootstrapPath = "$guestDirectory\\$PREREQUISITE_DIRECTORY\\$BOOTSTRAP_NAME",
        )
    }

    fun createBootstrapShortcut(
        container: Container,
        directShortcut: Shortcut,
        preparation: Preparation,
    ): Shortcut {
        check(!preparation.installed) { "The prerequisite bootstrap is not required" }
        val shortcutFile = File(
            checkNotNull(directShortcut.file.parentFile) { "The launch directory is unavailable" },
            "transformers-wfc-prerequisites.desktop",
        )
        shortcutFile.writeText(
            """
            [Desktop Entry]
            Name=Transformers WFC Prerequisites
            Exec=C:\windows\system32\cmd.exe
            Type=Application
            StartupNotify=True
            """.trimIndent() + "\n",
        )
        return Shortcut(container, shortcutFile).also { bootstrapShortcut ->
            COPYABLE_SHORTCUT_EXTRAS.forEach { key ->
                directShortcut.getExtra(key).takeIf(String::isNotBlank)?.let { value ->
                    bootstrapShortcut.putExtra(key, value)
                }
            }
            bootstrapShortcut.putExtra(
                "execArgs",
                bootstrapExecArgs(preparation.guestBootstrapPath),
            )
            bootstrapShortcut.saveData()
        }
    }

    internal fun receiptFile(containerRoot: File): File = File(
        containerRoot,
        ".wine/drive_c/users/xuser/AppData/Local/DualScreen Launcher/TransformersWFC/" +
            "prerequisites-v2.complete",
    )

    internal fun failureFile(containerRoot: File): File = File(
        receiptFile(containerRoot).parentFile,
        "prerequisites-v2.failed",
    )

    internal fun prerequisitesAreCurrent(containerRoot: File): Boolean =
        receiptFile(containerRoot).readTextOrEmpty().trim() == RECEIPT_TEXT &&
            installedFilesArePresent(containerRoot)

    internal fun bootstrapExecArgs(guestBootstrapPath: String): String =
        "/d /s /c call \"${guestBootstrapPath.trim().trim('"')}\""

    internal fun installedFilesArePresent(containerRoot: File): Boolean {
        val driveC = File(containerRoot, ".wine/drive_c")
        val windows = File(driveC, "windows")
        val directX = listOf("d3dx9_42.dll", "xactengine3_0.dll", "xaudio2_7.dll").all { name ->
            listOf(File(windows, "system32/$name"), File(windows, "syswow64/$name"))
                .all(File::isFile)
        }
        val visualC = containsNamedFile(windows, "msvcr80.dll", maxDepth = 4)
        val physX = listOf(
            File(driveC, "Program Files/NVIDIA Corporation/PhysX/Common/PhysXLoader.dll"),
            File(driveC, "Program Files (x86)/NVIDIA Corporation/PhysX/Common/PhysXLoader.dll"),
        ).any(File::isFile)
        return directX && visualC && physX
    }

    private fun verifyBundledPayload(bootstrap: File, redist: File) {
        check(bootstrap.isFile) { "The Transformers prerequisite bootstrap is missing" }
        check(bootstrap.readText().contains(RECEIPT_TEXT)) {
            "The Transformers prerequisite bootstrap has the wrong receipt revision"
        }
        val files = redist.walkTopDown().filter(File::isFile).toList()
        // Android's emulated-storage/FUSE layer can briefly retain the old
        // stat size after an atomic replacement even though opening the file
        // returns all of its new contents. Count the bytes that are actually
        // readable so a valid payload is not rejected on stale metadata.
        check(files.size == EXPECTED_REDIST_FILES && files.sumOf(::readableBytes) == EXPECTED_REDIST_BYTES) {
            "The Transformers prerequisite payload is incomplete"
        }
        installerRules.forEach { rule ->
            val file = File(redist, rule.relativePath)
            check(file.isFile && sha256(file) == rule.sha256) {
                "The verified ${rule.relativePath} installer is unavailable"
            }
        }
    }

    private fun readableBytes(file: File): Long {
        var total = 0L
        file.inputStream().buffered(128 * 1024).use { input ->
            val buffer = ByteArray(128 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                total += count
            }
        }
        return total
    }

    private fun containsNamedFile(root: File, name: String, maxDepth: Int): Boolean {
        if (!root.isDirectory) return false
        return root.walkTopDown()
            .maxDepth(maxDepth)
            .any { it.isFile && it.name.equals(name, ignoreCase = true) }
    }

    private fun File.readTextOrEmpty(): String = runCatching { readText() }.getOrDefault("")

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered(128 * 1024).use { input ->
            val buffer = ByteArray(128 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private data class InstallerRule(
        val relativePath: String,
        val sha256: String,
    )

    private val COPYABLE_SHORTCUT_EXTRAS = listOf(
        "envVars",
        "screenSize",
        "displayRenderer",
        "rendererDriverId",
        "inputType",
        "emulator",
        "box64Version",
        "box64Preset",
    )
}
