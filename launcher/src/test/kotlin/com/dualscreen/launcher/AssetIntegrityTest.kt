package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.json.JSONObject
import org.junit.Test
import java.io.File

class AssetIntegrityTest {
    private val assets = File("src/main/assets").canonicalFile
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
    fun stardewShipsOnlyVersionedNativeDeltas() {
        val root = File(assets, "mods/stardew-valley")
        val manifest = JSONObject(File(root, "manifest.json").readText())
        assertThat(manifest.getJSONArray("files").length()).isEqualTo(0)
        val patches = manifest.getJSONArray("patches")
        assertThat(patches.length()).isEqualTo(2)
        assertThat((0 until patches.length()).map { patches.getJSONObject(it).getString("target") })
            .containsExactly("Stardew Valley.dll", "Stardew Valley.dll")
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
}
