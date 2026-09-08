package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class Crysis3ConfigurationTest {
    @Test fun readOnlyUiTrialHasExactPayloadHashesAndNoFinishedModClaim() {
        val root = java.io.File("src/main/assets/mods/crysis3")
        val manifest = org.json.JSONObject(java.io.File(root, "manifest.json").readText())
        assertThat(manifest.getString("version")).startsWith("crysis3-readonly-ui-trial-")
        val files = manifest.getJSONArray("files")
        val targets = mutableSetOf<String>()
        for (index in 0 until files.length()) {
            val entry = files.getJSONObject(index)
            val bytes = java.io.File(root, "payload/" + entry.getString("source")).readBytes()
            val digest = java.security.MessageDigest.getInstance("SHA-256")
                .digest(bytes).joinToString("") { "%02x".format(it) }
            assertThat(digest).isEqualTo(entry.getString("sha256"))
            targets += entry.getString("target")
        }
        assertThat(targets).containsAtLeast("Bin32/dinput8.dll", "Bin32/Crysis3NativeProbe.ini", "Bin32/Crysis3PanelAssets/nanosuit.rgba", "DualScreenDiagnostics/README.txt")
        assertThat(targets).hasSize(90)
        assertThat(targets.count { it.startsWith("Bin32/Crysis3PanelAssets/modules/") }).isEqualTo(16)
        assertThat(targets.count { it.startsWith("Bin32/Crysis3PanelAssets/weapons/") }).isEqualTo(68)
        assertThat(targets).containsAtLeast("Bin32/Crysis3PanelAssets/weapons/Hammer.rgba", "Bin32/Crysis3PanelAssets/weapons/SCAR.rgba", "Bin32/Crysis3PanelAssets/modules/BulletProof.rgba")
        assertThat(targets.count { it.startsWith("Bin32/Crysis3PanelAssets/model-views/") }).isEqualTo(2)
        for (weapon in listOf("SCAR", "Hammer")) {
            assertThat(targets).contains("Bin32/Crysis3PanelAssets/model-views/$weapon.rgba")
            val view = java.io.File(root, "payload/native/assets/model-views/$weapon.rgba").readBytes()
            val header = java.nio.ByteBuffer.wrap(view).order(java.nio.ByteOrder.LITTLE_ENDIAN)
            assertThat(header.int).isEqualTo(0x41523343)
            assertThat(header.int).isEqualTo(1)
            assertThat(header.int).isEqualTo(900)
            assertThat(header.int).isEqualTo(300)
            assertThat(view.size).isEqualTo(16 + 900 * 300 * 4)
        }
        assertThat(java.io.File(root, "payload/diagnostics/README.txt").readText())
            .contains("not the finished functional mod")
    }

    @Test fun usesTheOwnersUnmodifiedLibraryArtwork() {
        val artwork = java.io.File("src/main/res/drawable-nodpi/art_crysis3.png")
        val digest = java.security.MessageDigest.getInstance("SHA-256")
            .digest(artwork.readBytes()).joinToString("") { "%02x".format(it) }
        assertThat(digest).isEqualTo("9fc836b4e14b68294b217409a48d1ca3136ce537513ce473ab397ba3e097c7c7")
        assertThat(gameArtwork("crysis3")).isEqualTo(R.drawable.art_crysis3)
        assertThat(gameShortTitle("crysis3")).isEqualTo("Crysis 3")
    }

    @Test fun sharesSkyrimsPresetWithCrysisOnlyPerformanceOverrides() {
        val crysis = checkNotNull(GameCatalog.byId("crysis3"))
        val skyrim = checkNotNull(GameCatalog.byId("skyrim-se"))
        assertThat(PresetContainerManager.presetKeyFor(crysis))
            .isEqualTo(PresetContainerManager.presetKeyFor(skyrim))
        assertThat(supportedGameTranslatorExtras(crysis.id)).containsExactly(
            "emulator", "FEXCore", "fexcorePreset", "PERFORMANCE", "affinityBeforeLaunch", "1",
        )
        assertThat(supportedGameTranslatorExtras(skyrim.id)).isEmpty()
        assertThat(supportedGameFexCoreVersion(crysis.id)).isNull()
        assertThat(supportedGameGpuDriverId(crysis.id)).isNull()
    }

    @Test fun loggingIsShortcutScopedAndDoesNotClaimANativeMod() {
        val crysis = checkNotNull(GameCatalog.byId("crysis3"))
        assertThat(crysis.panelMode).isEqualTo(LowerPanelMode.NATIVE_WINDOW)
        assertThat(crysis.isBeta).isTrue()
        assertThat(crysis.nativeDllOverrides).containsExactly("dinput8", "native,builtin")
        assertThat(crysis.executableCandidates).containsExactly("Bin32/Crysis3.exe")
        assertThat(crysis.fileRules.single().sha256AnyOf).containsExactly(
            "f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e",
            "fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055",
        )
        assertThat(supportedGameLaunchEnvironment(crysis.id))
            .contains("WINEDEBUG=-all,+timestamp,+pid,+tid,+seh")
        GameCatalog.games.filter { it.id != crysis.id }.forEach {
            assertThat(supportedGameLaunchEnvironment(it.id)).doesNotContain("WINEDEBUG")
        }
        assertThat(supportedGameLaunchEnvironment(crysis.id)).doesNotContain("DEP")
    }
}
