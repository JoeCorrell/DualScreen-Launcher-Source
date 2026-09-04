package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.json.JSONObject
import org.junit.Test

class PresetContainerMaterializationTest {
    @Test
    fun changedPresetInvalidatesEvenWhenTheOldRevisionWasCurrent() {
        assertThat(
            PresetContainerManager.shouldInvalidateRuntimeMaterialization(
                configurationChanged = true,
                materializationRevision = "6",
            ),
        ).isTrue()
    }

    @Test
    fun unchangedCurrentPresetKeepsItsVerifiedRuntime() {
        assertThat(
            PresetContainerManager.shouldInvalidateRuntimeMaterialization(
                configurationChanged = false,
                materializationRevision = "7",
            ),
        ).isFalse()
    }

    @Test
    fun invalidationClearsDxvkCacheAndMaterializationReceipts() {
        val extras = JSONObject()
            .put("dxvkCacheRevision", "2")
            .put("companionPresetMaterialization", "7")
            .put("displaySecondScreen", "1")

        PresetContainerManager.removeRuntimeMaterializationReceipts(extras)

        assertThat(extras.has("dxvkCacheRevision")).isFalse()
        assertThat(extras.has("companionPresetMaterialization")).isFalse()
        assertThat(extras.getString("displaySecondScreen")).isEqualTo("1")
    }
}
