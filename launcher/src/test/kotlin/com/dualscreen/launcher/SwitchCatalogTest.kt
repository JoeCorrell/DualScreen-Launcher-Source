package com.dualscreen.launcher

import com.dualscreen.launcher.switch.SwitchCatalog
import com.dualscreen.launcher.switch.EdenInstallProgressCallback
import com.google.common.truth.Truth.assertThat
import org.junit.Test
import org.json.JSONObject
import java.io.File

class SwitchCatalogTest {
    @Test
    fun everySwitchGameHasItsRecordedRequiredVersion() {
        assertThat(SwitchCatalog.games.associate { it.id to it.requiredVersion }).containsExactly(
            "mario-kart-8-deluxe", "3.0.5",
            "breath-of-the-wild", "1.6.0",
            "pokemon-legends-za", "v393216",
            "skyward-sword-hd", "1.0.1",
            "xenoblade-chronicles-x", "1.0.2",
            "links-awakening", "1.0.0",
            "super-mario-odyssey", "1.0.0",
        )
        SwitchCatalog.games.forEach { game ->
            assertThat(game.requiredVersionDescription).contains(game.requiredVersion)
        }
    }

    @Test
    fun versionInstructionsDistinguishBaseGamesFromUpdateIdentifiers() {
        for (id in listOf("links-awakening", "super-mario-odyssey")) {
            assertThat(SwitchCatalog.byId(id)?.requiredVersionDescription)
                .isEqualTo("1.0.0 (base game, no update)")
        }
        assertThat(SwitchCatalog.byId("pokemon-legends-za")?.requiredVersionDescription)
            .isEqualTo("Update v393216")
        assertThat(SwitchCatalog.byId("breath-of-the-wild")?.requiredVersionDescription)
            .isEqualTo("1.6.0")
    }

    @Test
    fun switchVersionRequirementsAreVisibleInLibraryAndDetails() {
        val source = File("src/main/kotlin/com/dualscreen/launcher/SwitchGamesUi.kt").readText()
        assertThat(source).contains("REQUIRES \${game.requiredVersion}")
        assertThat(source).contains("REQUIRED VERSION  \${game.requiredVersionDescription}")
        assertThat(source).contains("MetricBlock(\"REQUIRED VERSION\", game.requiredVersion")
        assertThat(source).doesNotContain("MetricBlock(\"TESTED\"")
        // Requirements supplement, rather than replace, the detected installed versions.
        assertThat(source).contains("it.baseVersion")
        assertThat(source).contains("installed?.installedUpdate?")
    }

    @Test
    fun supportedTitlesAreUniqueAndCanonical() {
        assertThat(SwitchCatalog.games.map { it.id }).containsExactly(
            "mario-kart-8-deluxe",
            "breath-of-the-wild",
            "pokemon-legends-za",
            "skyward-sword-hd",
            "xenoblade-chronicles-x",
            "links-awakening",
            "super-mario-odyssey",
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
        assertThat(game?.requiredVersion).isEqualTo("1.0.2")
        assertThat(
            File("src/main/res/drawable-nodpi/art_switch_xenoblade_x.png").isFile,
        ).isTrue()
        assertThat(File("src/main/kotlin/com/dualscreen/launcher/LauncherPresentation.kt").readText())
            .contains("\"xenoblade-chronicles-x\" -> R.drawable.art_switch_xenoblade_x")
    }

    @Test
    fun odysseyUsesTheProvidedLibraryArtwork() {
        val artwork = File("src/main/res/drawable-nodpi/art_switch_mario_odyssey.png")
        assertThat(artwork.isFile).isTrue()
        val digest = java.security.MessageDigest.getInstance("SHA-256")
            .digest(artwork.readBytes()).joinToString("") { "%02x".format(it) }
        assertThat(digest).isEqualTo("db5ff648bbe9664baffc59898d9f451fb70bab88c8ae105a0df22cbf47862f6b")
        assertThat(File("src/main/kotlin/com/dualscreen/launcher/LauncherPresentation.kt").readText())
            .contains("\"super-mario-odyssey\" -> R.drawable.art_switch_mario_odyssey")
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
            if (game.id == "pokemon-legends-za" || game.id == "breath-of-the-wild") {
                assertThat(manifest.getString("delivery")).isEqualTo("external-module")
                assertThat(manifest.getString("package")).isEqualTo(
                    if (game.id == "breath-of-the-wild") "botw/package.dsmod" else "plza/package.dsmod")
            } else {
                assertThat(manifest.getString("delivery")).isEqualTo("launcher-apk")
                assertThat(manifest.getString("minimumLauncherVersion"))
                .isEqualTo(when (game.id) {
                    "super-mario-odyssey" -> "1.6.17"
                    "links-awakening" -> "1.6.16"
                    else -> "1.6.9"
                })
            }
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
    fun linksAwakeningDevelopmentProfileUsesVerifiedTitleAndVersion() {
        val game = requireNotNull(SwitchCatalog.byId("links-awakening"))
        assertThat(game.titleId).isEqualTo("01006BB00C6F0000")
        assertThat(game.requiredVersion).isEqualTo("1.0.0")
        assertThat(game.supportsDlc).isFalse()
        assertThat(game.summary).contains("in progress")
        assertThat(SwitchCatalog.byTitleId("01006bb00c6f0000")).isEqualTo(game)
    }

    @Test
    fun odysseyDevelopmentProfileUsesVerifiedBaseVersion() {
        val game = requireNotNull(SwitchCatalog.byId("super-mario-odyssey"))
        assertThat(game.titleId).isEqualTo("0100000000010000")
        assertThat(game.requiredVersion).isEqualTo("1.0.0")
        assertThat(game.supportsDlc).isFalse()
        assertThat(game.summary).contains("in progress")
        assertThat(SwitchCatalog.byTitleId(game.titleId)).isEqualTo(game)
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
        assertThat(renderer).contains("File(root, \"${'$'}playerCacheDirectory/${'$'}file\")")
        assertThat(renderer).contains("BOTW_PLAYER_CACHE_DIRECTORY")
        assertThat(renderer).contains("SSHD_PLAYER_CACHE_DIRECTORY")
        assertThat(renderer).contains("loadActorCandidates")
        assertThat(renderer).contains("keeping the current ${'$'}slot model")
        assertThat(renderer).contains("using ${'$'}{model.sourceActor}")
        assertThat(renderer).contains("defaultActors.getValue(slot)")
        assertThat(renderer).contains("hasValidatedBotwBodyPose")
        assertThat(renderer).contains("Ignoring unposed external player model")
        assertThat(renderer).contains("using a packaged fallback")
        assertThat(renderer).doesNotContain(
            "val source = context.getExternalFilesDir(null)?.let",
        )

        val runtimeBuild = File("../switch/eden-runtime/build.gradle.kts").readText()
        assertThat(runtimeBuild).contains("botwPlayerAssetsDir")
        assertThat(runtimeBuild).contains("verifyBotwBasePlayerAssets")
        assertThat(runtimeBuild).contains("Armor_Default_Upper.glb")
        assertThat(runtimeBuild).contains("include(\"Armor_*.glb\")")
        assertThat(runtimeBuild).contains("val missingArmor = requiredArmor.filterNot")
        assertThat(runtimeBuild).contains("without every indexed BOTW armor GLB")
        assertThat(runtimeBuild).contains("has no validated baked Nml_Wait pose")
        assertThat(runtimeBuild).contains("contains a non-embedded or non-PNG texture")
    }
}
