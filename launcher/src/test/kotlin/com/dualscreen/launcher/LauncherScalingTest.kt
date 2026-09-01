package com.dualscreen.launcher

import androidx.compose.ui.unit.dp
import com.google.common.truth.Truth.assertThat
import org.junit.Test

class LauncherScalingTest {
    @Test
    fun artworkContractsInsteadOfMetadataWhenBannerIsVisible() {
        assertThat(browseCoverHeight(288.dp)).isEqualTo(206.dp)
        assertThat(288.dp - browseCoverHeight(288.dp)).isEqualTo(82.dp)
    }

    @Test
    fun veryShortCardsRetainUsableArtwork() {
        assertThat(browseCoverHeight(210.dp)).isEqualTo(128.dp)
    }
}
