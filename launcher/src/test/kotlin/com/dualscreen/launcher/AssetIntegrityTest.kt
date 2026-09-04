package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.json.JSONObject
import org.junit.Test
import java.io.File
import java.security.MessageDigest

class AssetIntegrityTest {
    private val assets = File("src/main/assets").canonicalFile
    private val engineAssets = File("../engine/src/main/assets").canonicalFile
    private val main = File("src/main").canonicalFile

    @Test
    fun stardewLoopbackBridgeIsAllowedWithoutEnablingGlobalCleartext() {
        val manifest = File(main, "AndroidManifest.xml").readText()
        val networkPolicy = File(main, "res/xml/network_security_config.xml").readText()
        assertThat(manifest).contains("android:networkSecurityConfig=\"@xml/network_security_config\"")
        assertThat(networkPolicy).contains("<base-config cleartextTrafficPermitted=\"false\"")
        assertThat(networkPolicy).contains(">127.0.0.1</domain>")
        assertThat(networkPolicy).contains(">localhost</domain>")
    }

    @Test
    fun capturedGamesPresetMatchesThorSnapshot() {
        val preset = JSONObject(File(assets, "presets/games.json").readText())
        assertThat(preset.getString("name")).isEqualTo("Games")
        assertThat(preset.getString("wineVersion")).isEqualTo("Proton-11.0-5-arm64ec-6")
        assertThat(preset.getString("fexcoreVersion")).isEqualTo("FEXCore-2608-stable-0")
        assertThat(preset.getString("emulator")).isEqualTo("FEXCore")
        assertThat(preset.getString("box64Version")).isEqualTo("0.4.2")
        assertThat(preset.getString("rendererDriverId")).isEqualTo("turnip26.2.0")
        assertThat(preset.getString("displayRenderer")).isEqualTo("surfaceflinger")
        assertThat(preset.getJSONObject("extraData").getString("displaySecondScreen")).isEqualTo("1")
        assertThat(preset.getJSONObject("extraData").getString("affinityCores")).isEqualTo("3,4,5,6,7")
        assertThat(preset.getJSONObject("extraData").getString("fallout3Compatibility"))
            .isEqualTo("4")
    }

    @Test
    fun transformersPresetClonesTheKnownGoodProton9ContainerButUsesBox64() {
        val proton9 = JSONObject(File(assets, "presets/stardew-proton9.json").readText())
        val transformers = JSONObject(File(assets, "presets/transformers-wfc.json").readText())

        assertThat(transformers.getString("name")).isEqualTo("Transformers WFC")
        assertThat(transformers.getString("emulator")).isEqualTo("box64")
        assertThat(transformers.getString("box64Version")).isEqualTo("0.4.2")
        assertThat(transformers.getString("box64Preset")).isEqualTo("COMPATIBILITY")
        assertThat(transformers.getBoolean("syncCpuTopology")).isFalse()
        assertThat(transformers.getString("wineVersion"))
            .isEqualTo("Proton-10.0-arm64ec-0")
        assertThat(transformers.getString("rendererDriverId")).isEqualTo("turnip26.2.0")
        assertThat(transformers.getString("graphicsDriverConfig"))
            .contains("version=turnip26.2.0")
        assertThat(transformers.getJSONObject("extraData").getString("box64Version"))
            .isEqualTo("0.4.2")
        assertThat(transformers.getJSONObject("extraData").getString("wemuPinnedBox64Runtime"))
            .isEqualTo("transformers-wfc-0.4.2")

        // Identity and the authenticated Box64 package stay WFC-specific;
        // ARM64EC execution itself follows WEMU's FEXCore path. WFC additionally
        // exposes the second RandR monitor required by its native panel. Every
        // other compatibility/performance field remains the exact WEMU Proton 9
        // snapshot. In particular, fitGuestWindow stays disabled: scaling a
        // 640x480 fallback is not a substitute for resizing WFC's real client.
        proton9.put("id", transformers.getInt("id"))
        proton9.put("name", transformers.getString("name"))
        proton9.put("emulator", "box64")
        proton9.getJSONObject("extraData").put(
            "wemuPinnedBox64Runtime",
            "transformers-wfc-0.4.2",
        )
        proton9.getJSONObject("extraData").put("displaySecondScreen", "1")
        transformers.remove("syncCpuTopology")
        transformers.remove("name")
        proton9.remove("name")
        assertThat(transformers.toString()).isEqualTo(proton9.toString())
        assertThat(transformers.getJSONObject("extraData").getString("displaySecondScreen"))
            .isEqualTo("1")

        val archive = File(engineAssets, "box64/box64-0.4.2.tzst")
        assertThat(archive.length()).isEqualTo(3_970_492L)
        assertThat(sha256(archive)).isEqualTo(
            "a1dbfd7e3adeb7ad1d1aa17abbadcb77906049e96c501eff3ed890c1f979ed9f",
        )

        val wowArchive = File(engineAssets, "wowbox64/wowbox64-0.4.2.tzst")
        assertThat(wowArchive.length()).isEqualTo(1_015_193L)
        assertThat(sha256(wowArchive)).isEqualTo(
            "dc350a3aedc9ecd42fa95dc0fd64b8cfe299c313185aef8ca8e9cee793ac5960",
        )
    }

    @Test
    fun stardewPresetMatchesWemuProton9Snapshot() {
        val preset = JSONObject(File(assets, "presets/stardew-proton9.json").readText())
        assertThat(preset.getString("name")).isEqualTo("proton 9")
        // The WEMU container is named Proton 9, but its real controller-fix
        // component identifies itself as this Proton 10 ARM64EC build.
        assertThat(preset.getString("wineVersion")).isEqualTo("Proton-10.0-arm64ec-0")
        assertThat(preset.getString("fexcoreVersion")).isEqualTo("2601")
        assertThat(preset.getString("fexcorePreset")).isEqualTo("INTERMEDIATE")
        assertThat(preset.getString("box64Version")).isEqualTo("0.4.2")
        assertThat(preset.getString("box64Preset")).isEqualTo("COMPATIBILITY")
        assertThat(preset.getString("rendererDriverId")).isEqualTo("turnip26.2.0")
        assertThat(preset.getString("displayRenderer")).isEqualTo("vulkan")
        assertThat(preset.getString("rendererPresentMode")).isEqualTo("fifo")
        assertThat(preset.getString("drives"))
            .isEqualTo("F:/storage/emulated/0D:/storage/emulated/0/Download")
        assertThat(preset.getString("drives")).doesNotContain("\\")
        assertThat(preset.getString("dxwrapperConfig")).contains("version=2.3.1")
        assertThat(preset.getString("dxwrapperConfig")).contains("vkd3dVersion=None")
        // The restored Stardew UI is an Android-managed 1240x1080 panel. It
        // must not add that display to Wine's guest desktop or scale a framed
        // Wine window; the embedded runtime makes the 1280x720 game borderless.
        assertThat(preset.getJSONObject("extraData").getString("displaySecondScreen")).isEqualTo("0")
        assertThat(preset.getJSONObject("extraData").getString("fitGuestWindow")).isEqualTo("0")
        assertThat(preset.getJSONObject("extraData").getString("affinityCores")).isEmpty()
    }

    @Test
    fun capturedComponentManifestPinsEveryRemoteArchive() {
        val manifest = JSONObject(File(assets, "components/manifest.json").readText())
            .getJSONArray("components")
        assertThat(manifest.length()).isEqualTo(5)
        for (index in 0 until manifest.length()) {
            val entry = manifest.getJSONObject(index)
            assertThat(entry.getString("source")).matches("[A-Za-z0-9._-]+\\.tgz")
            assertThat(entry.getLong("bytes")).isGreaterThan(0)
            assertThat(entry.getString("sha256")).matches("[0-9a-f]{64}")
        }
    }

    @Test
    fun everyRemoteModPayloadIsPinnedAndTargetsAPathInsideTheGame() {
        GameCatalog.games.forEach { game ->
            val root = File(assets, "mods/${game.id}").canonicalFile
            val manifestFile = File(root, "manifest.json")
            val bytes = manifestFile.readBytes()
            assertThat(bytes.take(3)).isNotEqualTo(listOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte()))
            val manifest = JSONObject(bytes.toString(Charsets.UTF_8))
            val files = manifest.optJSONArray("files")
            val patches = manifest.optJSONArray("patches")
            assertThat((files?.length() ?: 0) + (patches?.length() ?: 0)).isGreaterThan(0)
            for (index in 0 until (files?.length() ?: 0)) {
                val entry = checkNotNull(files).getJSONObject(index)
                assertThat(entry.getString("source")).doesNotContain("..")
                assertThat(entry.getString("sha256")).matches("[0-9a-f]{64}")
                val target = entry.getString("target").replace('\\', '/')
                assertThat(target.startsWith("/")).isFalse()
                assertThat(target.split('/')).doesNotContain("..")
            }
            for (index in 0 until (patches?.length() ?: 0)) {
                val entry = checkNotNull(patches).getJSONObject(index)
                assertThat(entry.getString("source")).doesNotContain("..")
                assertThat(entry.getString("sha256")).matches("[0-9a-f]{64}")
                assertThat(entry.getString("inputSha256")).matches("[0-9a-f]{64}")
                assertThat(entry.getString("outputSha256")).matches("[0-9a-f]{64}")
                val target = entry.getString("target").replace('\\', '/')
                assertThat(target.startsWith("/")).isFalse()
                assertThat(target.split('/')).doesNotContain("..")
            }
            val removals = manifest.optJSONArray("remove")
            for (index in 0 until (removals?.length() ?: 0)) {
                val target = checkNotNull(removals).getString(index).replace('\\', '/')
                assertThat(target.startsWith("/")).isFalse()
                assertThat(target.split('/')).doesNotContain("..")
            }
        }
    }

    @Test
    fun isaacNativePatchAndAssetsCoverTheExactInstalledBuild() {
        val isaacAssets = File("../pc/mods/IsaacSecondScreen/assets").canonicalFile
        val index = JSONObject(File(isaacAssets, "index.json").readText())
        assertThat(index.getString("sourceBuild"))
            .isEqualTo("Binding of Isaac: Repentance+ v1.9.7.17.J460")
        val items = index.getJSONObject("items")
        assertThat(items.getJSONObject("collectibles").length()).isEqualTo(721)
        assertThat(items.getJSONObject("trinkets").length()).isEqualTo(188)
        assertThat(index.getJSONObject("players").length()).isEqualTo(41)
        assertThat(index.getJSONObject("pockets").length()).isEqualTo(98)
        assertThat(items.getJSONObject("collectibles").getJSONObject("118").getInt("quality"))
            .isEqualTo(4)

        listOf("collectibles", "trinkets").forEach { category ->
            val entries = items.getJSONObject(category)
            entries.keys().forEach { id ->
                val asset = entries.getJSONObject(id).getString("asset")
                    .removePrefix("isaac/")
                assertThat(File(isaacAssets, asset).isFile).isTrue()
            }
        }
        listOf(
            "game/font/upheaval.fnt",
            "game/gfx/ui/minimap_icons.png",
            "game/gfx/ui/ui_hearts.png",
            "game/gfx/items/pick ups/pickup_007_pill.png",
        ).forEach { relative -> assertThat(File(isaacAssets, relative).isFile).isTrue() }

        val mod = File(assets, "mods/isaac-repentance")
        val payload = File(mod, "payload").canonicalFile
        val manifest = JSONObject(File(mod, "manifest.json").readText()).getJSONArray("files")
        assertThat(manifest.length()).isEqualTo(1_207)
        val declared = linkedSetOf<String>()
        val targets = linkedSetOf<String>()
        for (entryIndex in 0 until manifest.length()) {
            val entry = manifest.getJSONObject(entryIndex)
            val sourceName = entry.getString("source").replace('\\', '/')
            val targetName = entry.getString("target").replace('\\', '/')
            assertThat(declared.add(sourceName.lowercase())).isTrue()
            assertThat(targets.add(targetName.lowercase())).isTrue()
            assertThat(targetName.startsWith('/')).isFalse()
            assertThat(targetName.split('/')).doesNotContain("..")
            val source = File(payload, sourceName).canonicalFile
            assertThat(source.path.startsWith(payload.path + File.separator)).isTrue()
            assertThat(source.isFile).isTrue()
            assertThat(sha256(source))
                .isEqualTo(entry.getString("sha256"))
        }
        val bundled = payload.walkTopDown()
            .filter(File::isFile)
            .map { it.relativeTo(payload).invariantSeparatorsPath.lowercase() }
            .toSet()
        assertThat(declared).containsExactlyElementsIn(bundled)
        assertThat(targets).containsAtLeast(
            "winmm.dll",
            "resources/scripts/isaac_second_screen_assets/game/gfx/ui/minimap_icons.png",
            "mods/dualscreenisaac/main.lua",
        )
        val mainLua = File(payload, "mod/main.lua").readText()
        assertThat(mainLua).contains("native winmm proxy")
        assertThat(mainLua).contains("luaopen_isaac_second_screen")
        assertThat(mainLua).doesNotContain("pcall(require, \"socket\")")
        assertThat(mainLua).doesNotContain("require, \"isaac_second_screen\"")
    }

    @Test
    fun transformersManifestMatchesEveryBundledPayloadFile() {
        val root = File(assets, "mods/transformers-wfc")
        val payload = File(root, "payload").canonicalFile
        val manifest = JSONObject(File(root, "manifest.json").readText())
        val files = manifest.getJSONArray("files")
        val declared = linkedSetOf<String>()

        for (index in 0 until files.length()) {
            val entry = files.getJSONObject(index)
            val sourceName = entry.getString("source").replace('\\', '/')
            assertThat(declared.add(sourceName.lowercase())).isTrue()
            val source = File(payload, sourceName).canonicalFile
            assertThat(source.path.startsWith(payload.path + File.separator)).isTrue()
            assertThat(source.isFile).isTrue()
            assertThat(sha256(source)).isEqualTo(entry.getString("sha256"))
        }

        val bundled = payload.walkTopDown()
            .filter(File::isFile)
            .map { it.relativeTo(payload).invariantSeparatorsPath.lowercase() }
            .toSet()
        assertThat(bundled).containsExactlyElementsIn(declared)
    }

    @Test
    fun stardewShipsOnlyVersionedNativeDeltas() {
        val root = File(assets, "mods/stardew-valley")
        val manifest = JSONObject(File(root, "manifest.json").readText())
        assertThat(manifest.getJSONArray("files").length()).isEqualTo(0)
        val patches = manifest.getJSONArray("patches")
        assertThat(patches.length()).isEqualTo(4)
        assertThat((0 until patches.length()).map { patches.getJSONObject(it).getString("target") })
            .containsExactly(
                "Stardew Valley.dll",
                "Stardew Valley.dll",
                "Stardew Valley.dll",
                "Stardew Valley.dll",
            )
        assertThat((0 until patches.length()).map {
            patches.getJSONObject(it).getString("inputSha256")
        }).containsAtLeast(
            "58fd97df1e77d03a4bf2196a69b7732eefbb1d1e1743535af1d2ab1162053539",
            "9b03cc5106f24e861e3bceb38e68ae6b286b77b83f4c804bd3818b16c1dbc172",
        )
        assertThat((0 until patches.length()).map {
            patches.getJSONObject(it).getString("outputSha256")
        }).containsExactly(
            "f3ebc7c7cda1199c3919fd2d1619722b3e045b2148aece7cf0bb9c8e4d47d466",
            "9bbcb7d5be1d059f863173802e68d9ce1a48a0800963a2cfbcc359f696035b9c",
            "f3ebc7c7cda1199c3919fd2d1619722b3e045b2148aece7cf0bb9c8e4d47d466",
            "9bbcb7d5be1d059f863173802e68d9ce1a48a0800963a2cfbcc359f696035b9c",
        )
        assertThat(root.walkTopDown().filter(File::isFile).map { it.name }.toList())
            .doesNotContain("StardewDualScreen.dll")
        assertThat((0 until manifest.getJSONArray("remove").length()).map {
            manifest.getJSONArray("remove").getString(it)
        }).containsExactly(
            "Mods/StardewDualScreen/StardewDualScreen.dll",
            "Mods/StardewDualScreen/manifest.json",
        )
    }

    @Test
    fun firstBootPayloadReleaseIsPinnedAndPublic() {
        val manifest = JSONObject(File(assets, "payload-bootstrap.json").readText())
        assertThat(manifest.getInt("schema")).isEqualTo(1)
        assertThat(manifest.getString("version")).isNotEmpty()
        assertThat(manifest.getString("repository"))
            .isEqualTo("https://github.com/JoeCorrell/DualScreen-Launcher-Payloads")
        val packs = manifest.getJSONArray("packs")
        assertThat(packs.length()).isEqualTo(2)
        for (index in 0 until packs.length()) {
            val pack = packs.getJSONObject(index)
            assertThat(pack.getLong("bytes")).isGreaterThan(0)
            assertThat(pack.getString("sha256")).matches("[0-9a-f]{64}")
            assertThat(pack.getString("url"))
                .startsWith("https://github.com/JoeCorrell/DualScreen-Launcher-Payloads/releases/download/")
        }
    }

    private fun sha256(file: File): String = MessageDigest.getInstance("SHA-256").let { digest ->
        file.inputStream().use { input ->
            val buffer = ByteArray(128 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        digest.digest().joinToString("") { "%02x".format(it) }
    }
}
