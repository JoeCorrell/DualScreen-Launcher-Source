package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Assert.assertThrows
import org.junit.Test

class PayloadBootstrapTest {
    @Test
    fun manifestParserAcceptsPinnedGithubReleasePacks() {
        val parsed = PayloadBootstrapManifest.parse(
            """
            {
              "schema": 1,
              "version": "v1",
              "repository": "https://github.com/JoeCorrell/DualScreen-Launcher-Payloads",
              "tag": "launcher-payloads-v1",
              "packs": [{
                "name": "runtime.zip",
                "root": "components",
                "bytes": 42,
                "sha256": "${"a".repeat(64)}",
                "url": "https://github.com/JoeCorrell/DualScreen-Launcher-Payloads/releases/download/launcher-payloads-v1/runtime.zip"
              }]
            }
            """.trimIndent(),
        )

        assertThat(parsed.version).isEqualTo("v1")
        assertThat(parsed.packs.single().bytes).isEqualTo(42)
    }

    @Test
    fun manifestParserRejectsUntrustedPayloadHost() {
        assertThrows(IllegalArgumentException::class.java) {
            PayloadBootstrapManifest.parse(
                """
                {
                  "schema": 1,
                  "version": "v1",
                  "repository": "https://github.com/JoeCorrell/DualScreen-Launcher-Payloads",
                  "tag": "launcher-payloads-v1",
                  "packs": [{
                    "name": "runtime.zip",
                    "root": "components",
                    "bytes": 42,
                    "sha256": "${"a".repeat(64)}",
                    "url": "https://example.invalid/runtime.zip"
                  }]
                }
                """.trimIndent(),
            )
        }
    }

    @Test
    fun versionComparisonHandlesPayloadDatesAndLauncherTags() {
        assertThat(isNewerVersion("2026.08.30.2", "2026.08.30.1")).isTrue()
        assertThat(isNewerVersion("v1.1.0", "1.0.9")).isTrue()
        assertThat(isNewerVersion("1.0.0", "v1.0.0")).isFalse()
        assertThat(isNewerVersion("1.0.0", "1.0.1")).isFalse()
    }

    @Test
    fun payloadPathsCannotEscapeTheStore() {
        assertThat(PayloadAssetStore.normalize("mods/fallout3/payload/dinput8.dll"))
            .isEqualTo("mods/fallout3/payload/dinput8.dll")
        assertThrows(IllegalArgumentException::class.java) {
            PayloadAssetStore.normalize("mods/../outside.dll")
        }
        assertThrows(IllegalArgumentException::class.java) {
            PayloadAssetStore.normalize("C:\\outside.dll")
        }
    }

    @Test
    fun setupProgressMovesContinuouslyThroughDownloadVerifyAndInstall() {
        val checkpoints = listOf(
            payloadOverallFraction(0, 100, 80, PayloadBootstrapPhase.DOWNLOAD, 0.0),
            payloadOverallFraction(0, 100, 80, PayloadBootstrapPhase.DOWNLOAD, 1.0),
            payloadOverallFraction(0, 100, 80, PayloadBootstrapPhase.VERIFY, 0.5),
            payloadOverallFraction(0, 100, 80, PayloadBootstrapPhase.INSTALL, 0.0),
            payloadOverallFraction(0, 100, 80, PayloadBootstrapPhase.INSTALL, 0.5),
            payloadOverallFraction(80, 100, 20, PayloadBootstrapPhase.DOWNLOAD, 0.0),
            payloadOverallFraction(80, 100, 20, PayloadBootstrapPhase.INSTALL, 1.0),
        )

        assertThat(checkpoints.zipWithNext().all { (first, second) -> second >= first }).isTrue()
        assertThat(checkpoints[4]).isGreaterThan(checkpoints[3])
        assertThat(checkpoints.last()).isEqualTo(1f)
    }
}
