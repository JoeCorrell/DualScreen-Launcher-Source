package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.json.JSONObject
import org.junit.Test
import java.io.File
import java.security.MessageDigest

class AssetIntegrityTest {
    private val assets = File("src/main/assets").canonicalFile

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
        assertThat(preset.getJSONObject("extraData").getString("displaySecondScreen")).isEqualTo("0")
        assertThat(preset.getJSONObject("extraData").getString("affinityCores")).isEmpty()
    }

    @Test
    fun capturedComponentArchivesMatchPinnedSizesAndHashes() {
        val manifest = JSONObject(File(assets, "components/manifest.json").readText())
            .getJSONArray("components")
        assertThat(manifest.length()).isEqualTo(5)
        for (index in 0 until manifest.length()) {
            val entry = manifest.getJSONObject(index)
            val file = File(assets, "components/${entry.getString("source")}").canonicalFile
            assertThat(file.path).startsWith(File(assets, "components").canonicalPath + File.separator)
            assertThat(file.isFile).isTrue()
            assertThat(file.length()).isEqualTo(entry.getLong("bytes"))
            assertThat(sha256(file)).isEqualTo(entry.getString("sha256"))
        }
    }

    @Test
    fun everyBundledModPayloadMatchesItsManifest() {
        GameCatalog.games.forEach { game ->
            val root = File(assets, "mods/${game.id}").canonicalFile
            val manifestFile = File(root, "manifest.json")
            val bytes = manifestFile.readBytes()
            assertThat(bytes.take(3)).isNotEqualTo(listOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte()))
            val manifest = JSONObject(bytes.toString(Charsets.UTF_8))
            val files = manifest.getJSONArray("files")
            assertThat(files.length()).isGreaterThan(0)
            for (index in 0 until files.length()) {
                val entry = files.getJSONObject(index)
                val source = File(root, "payload/${entry.getString("source")}").canonicalFile
                assertThat(source.path).startsWith(File(root, "payload").canonicalPath + File.separator)
                assertThat(source.isFile).isTrue()
                assertThat(sha256(source)).isEqualTo(entry.getString("sha256"))
                val target = entry.getString("target").replace('\\', '/')
                assertThat(target.startsWith("/")).isFalse()
                assertThat(target.split('/')).doesNotContain("..")
            }
        }
    }

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
}
