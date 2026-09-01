package com.dualscreen.launcher

import com.dualscreen.launcher.switch.SwitchCatalog
import com.dualscreen.launcher.switch.EdenInstallProgressCallback
import com.google.common.truth.Truth.assertThat
import org.junit.Test
import org.json.JSONObject
import java.io.File

class SwitchCatalogTest {
    @Test
    fun supportedTitlesAreUniqueAndCanonical() {
        assertThat(SwitchCatalog.games.map { it.id }).containsExactly(
            "mario-kart-8-deluxe",
            "breath-of-the-wild",
            "pokemon-legends-za",
            "skyward-sword-hd",
            "xenoblade-chronicles-x",
        ).inOrder()
        assertThat(SwitchCatalog.games.map { it.titleId }.distinct())
            .hasSize(SwitchCatalog.games.size)
        SwitchCatalog.games.forEach { game ->
            assertThat(game.titleId).matches("[0-9A-F]{16}")
            assertThat(game.titleIdValue and 0x1FFFL).isEqualTo(0L)
            assertThat(game.programId).isNotEmpty()
        }
    }

    @Test
    fun xenobladeUsesItsOwnLibraryArtwork() {
        val game = SwitchCatalog.byId("xenoblade-chronicles-x")
        assertThat(game).isNotNull()
        assertThat(game?.titleId).isEqualTo("0100453019AA8000")
        assertThat(game?.recommendedVersion).isEqualTo("1.0.2")
        assertThat(
            File("src/main/res/drawable-nodpi/art_switch_xenoblade_x.png").isFile,
        ).isTrue()
        assertThat(File("src/main/kotlin/com/dualscreen/launcher/LauncherPresentation.kt").readText())
            .contains("\"xenoblade-chronicles-x\" -> R.drawable.art_switch_xenoblade_x")
    }

    @Test
    fun switchGpuDriverChoicesUseAReadableTouchTarget() {
        val globalSettings = File(
            "src/main/kotlin/com/dualscreen/launcher/LauncherSettingsGuideUi.kt",
        ).readText()
        val edenSettings = File(
            "src/main/kotlin/com/dualscreen/launcher/SwitchSettingsUi.kt",
        ).readText()

        assertThat(globalSettings).contains("Modifier.width(224.dp).height(44.dp)")
        assertThat(edenSettings).contains("Modifier.width(224.dp).height(44.dp)")
    }

    @Test
    fun everySwitchTitleHasAnUpdateablePayloadManifest() {
        SwitchCatalog.games.forEach { game ->
            val manifestFile = File("src/main/assets/mods/switch/${game.id}/manifest.json")
            assertThat(manifestFile.isFile).isTrue()
            val manifest = JSONObject(manifestFile.readText(Charsets.UTF_8))
            assertThat(manifest.getInt("schema")).isEqualTo(1)
            assertThat(manifest.getString("titleId")).isEqualTo(game.titleId)
            assertThat(manifest.getString("version")).isNotEmpty()
            assertThat(manifest.getString("delivery")).isEqualTo("launcher-apk")
            assertThat(manifest.getString("minimumLauncherVersion")).isEqualTo("1.6.9")
            val files = manifest.getJSONArray("files")
            repeat(files.length()) { index ->
                val entry = files.getJSONObject(index)
                assertThat(entry.getString("source")).doesNotContain("..")
                assertThat(entry.getString("target")).doesNotContain("..")
                assertThat(entry.getString("sha256")).matches("[0-9a-f]{64}")
            }
        }
    }

    @Test
    fun switchPayloadSourcesStaySynchronizedWithBundledManifests() {
        SwitchCatalog.games.forEach { game ->
            val source = File("../switch/mods/${game.id}/manifest.json")
            val bundled = File("src/main/assets/mods/switch/${game.id}/manifest.json")

            assertThat(source.isFile).isTrue()
            assertThat(bundled.isFile).isTrue()
            assertThat(bundled.readBytes()).isEqualTo(source.readBytes())
        }
    }

    @Test
    fun releaseRulesPreserveEveryNativeResolvedClass() {
        val rules = File("../switch/eden-runtime/consumer-rules.pro").readText()
        assertThat(rules).contains("-keep class org.yuzu.yuzu_emu.** { *; }")
        assertThat(rules).contains("-keep class kotlin.Pair { *; }")
    }

    @Test
    fun untrustedNspParsingIsIsolatedFromLauncherProcess() {
        val manifest = File("src/main/AndroidManifest.xml").readText()
        val backend = File(
            "src/main/kotlin/com/dualscreen/launcher/switch/EdenSwitchBackend.kt",
        ).readText()
        val worker = File(
            "src/main/kotlin/com/dualscreen/launcher/switch/SwitchContentWorkerService.kt",
        ).readText()

        assertThat(manifest).contains("android:process=\":eden_content\"")
        assertThat(backend).doesNotContain("NativeLibrary.inspectContentPackage")
        assertThat(backend).doesNotContain("NativeLibrary.getPatchesForFile")
        assertThat(backend).doesNotContain("NativeLibrary.removeUpdate")
        assertThat(backend).doesNotContain("GameMetadata.getIsValid")
        assertThat(worker).contains("NativeLibrary.inspectContentPackage")
        assertThat(worker).contains("NativeLibrary.getPatchesForFile")
        assertThat(worker).contains("GameMetadata.getIsValid")
        assertThat(worker).contains("linkToDeath")
    }

    @Test
    fun edenInstallerProgressBridgeAcceptsNativeDoubleValues() {
        var maximum = -1L
        var progress = -1L
        val callback = EdenInstallProgressCallback { max, current ->
            maximum = max
            progress = current
        }

        assertThat(callback.invoke(120.0, 35.0)).isFalse()
        assertThat(maximum).isEqualTo(120L)
        assertThat(progress).isEqualTo(35L)

        val releaseRules = File("proguard-rules.pro").readText()
        assertThat(releaseRules).contains(
            "-keep class com.dualscreen.launcher.switch.EdenInstallProgressCallback { *; }",
        )
    }

    @Test
    fun installedContentIsRefreshedAcrossTheWorkerProcessBoundary() {
        val gameHelper = File(
            "../switch/eden-upstream/src/android/app/src/main/java/" +
                "org/yuzu/yuzu_emu/utils/GameHelper.kt",
        ).readText()
        val nativeLibrary = File(
            "../switch/eden-upstream/src/android/app/src/main/java/" +
                "org/yuzu/yuzu_emu/NativeLibrary.kt",
        ).readText()
        val worker = File(
            "src/main/kotlin/com/dualscreen/launcher/switch/SwitchContentWorkerService.kt",
        ).readText()

        assertThat(nativeLibrary).contains("external fun refreshInstalledContent()")
        assertThat(gameHelper).contains("NativeLibrary.refreshInstalledContent()")
        assertThat(worker).contains("Verified ${'$'}{expectedKind.name.lowercase()} after NAND install")
        assertThat(worker).contains("installedPatches.any { it.enabled }")
    }

    @Test
    fun botwPlayerModelsResolveFromTheEmbeddedEdenDirectory() {
        val renderer = File(
            "../switch/eden-upstream/src/android/app/src/main/java/" +
                "org/yuzu/yuzu_emu/activities/BotwPlayerRenderView.kt",
        ).readText()

        assertThat(renderer).contains("DirectoryInitialization.userDirectory")
        assertThat(renderer).contains("File(root, \"${'$'}playerCacheDirectory/${'$'}name\")")
        assertThat(renderer).contains("BOTW_PLAYER_CACHE_DIRECTORY")
        assertThat(renderer).contains("SSHD_PLAYER_CACHE_DIRECTORY")
        assertThat(renderer).doesNotContain(
            "val source = context.getExternalFilesDir(null)?.let",
        )
    }
}
