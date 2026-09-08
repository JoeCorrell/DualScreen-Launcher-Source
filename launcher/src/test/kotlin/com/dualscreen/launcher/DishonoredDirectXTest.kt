package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class DishonoredDirectXTest {
    @Test fun changesOnlyTheTwoVersionsAndDoesNotAffectOtherGames() {
        val original = "version=2.3.1,async=1,asyncCache=0,vkd3dVersion=None,renderer=gl,custom=value=kept"
        val result = supportedGameDirectXConfig("dishonored", original)
        assertThat(result).isEqualTo("version=DXVK-2.6-2-gplasync-0,async=1,asyncCache=0,vkd3dVersion=VKD3D-2.9-0,renderer=gl,custom=value=kept")
        assertThat(supportedGameDirectXConfig("dishonored", result!!)).isEqualTo(result)
        assertThat(supportedGameDirectXConfig("transformers-wfc", original)).isNull()
        assertThat(CapturedRuntimeRequirements.directX(result).componentIds)
            .containsExactly("DXVK-2.6-2-gplasync-0", "VKD3D-2.9-0")
        assertThat(CapturedRuntimeRequirements.directX(result).fixedAssets).isNull()
    }
}
