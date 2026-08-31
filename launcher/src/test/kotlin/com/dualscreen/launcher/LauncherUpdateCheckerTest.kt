package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class LauncherUpdateCheckerTest {
    @Test
    fun releaseParserSelectsTheSignedReleaseApk() {
        val release = LauncherRelease.parse(
            """
            {
              "tag_name": "v1.2.0",
              "html_url": "https://github.com/JoeCorrell/DualScreen-Launcher/releases/tag/v1.2.0",
              "assets": [{
                "name": "dualscreen-launcher-1.2.0.apk",
                "browser_download_url": "https://github.com/JoeCorrell/DualScreen-Launcher/releases/download/v1.2.0/dualscreen-launcher-1.2.0.apk"
              }]
            }
            """.trimIndent(),
        )

        assertThat(release.version).isEqualTo("1.2.0")
        assertThat(release.preferredDownloadUrl).endsWith("dualscreen-launcher-1.2.0.apk")
    }
}
