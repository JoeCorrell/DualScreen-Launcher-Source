package com.wemu.engine.session;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class SkyrimAudioCompatibilityTest {
    @Test
    public void recognizesExecutableAndLoaderNames() {
        assertTrue(SkyrimAudioCompatibility.matches("D:\\Games\\SkyrimSE.exe"));
        assertTrue(SkyrimAudioCompatibility.matches("skse64_loader.exe"));
        assertTrue(SkyrimAudioCompatibility.matches("The Elder Scrolls V: Skyrim"));
        assertFalse(SkyrimAudioCompatibility.matches("Oblivion.exe"));
    }

    @Test
    public void enablesXAudioWithoutChangingOtherComponents() {
        String configured = "direct3d=1,directsound=0,xaudio=0,vcrun2010=1";
        assertEquals("direct3d=1,directsound=0,xaudio=1,vcrun2010=1",
                SkyrimAudioCompatibility.enableRequiredComponents(configured));
    }

    @Test
    public void enablingXAudioIsIdempotent() {
        String configured = "direct3d=1,xaudio=1";
        assertEquals(configured,
                SkyrimAudioCompatibility.enableRequiredComponents(configured));
    }
}
