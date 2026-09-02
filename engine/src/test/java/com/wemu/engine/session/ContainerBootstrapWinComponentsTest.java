package com.wemu.engine.session;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.nio.file.Files;

import org.junit.Test;

public class ContainerBootstrapWinComponentsTest {
    private static final String WFC_COMPONENTS =
            "direct3d=1,directsound=1,directmusic=0,directshow=0,directplay=0,"
                    + "xaudio=1,vcrun2010=1";

    @Test
    public void wfcRequiresNativeDirectXAudioAndVisualRuntimeInBothArchitectures()
            throws Exception {
        File root = Files.createTempDirectory("wemu-wfc-components").toFile();
        File system32 = new File(root, "home/xuser/.wine/drive_c/windows/system32");
        File syswow64 = new File(root, "home/xuser/.wine/drive_c/windows/syswow64");
        assertTrue(system32.mkdirs());
        assertTrue(syswow64.mkdirs());

        assertFalse(ContainerBootstrap.areNativeWinComponentsMaterialized(root, WFC_COMPONENTS));

        String[] required = {
                "d3dx9_42.dll", "dsound.dll", "xactengine3_0.dll", "xaudio2_7.dll",
                "msvcr100.dll",
        };
        for (String name : required) {
            assertTrue(new File(system32, name).createNewFile());
            assertTrue(new File(syswow64, name).createNewFile());
        }
        assertTrue(ContainerBootstrap.areNativeWinComponentsMaterialized(root, WFC_COMPONENTS));

        assertTrue(new File(syswow64, "xaudio2_7.dll").delete());
        assertFalse(ContainerBootstrap.areNativeWinComponentsMaterialized(root, WFC_COMPONENTS));
    }

    @Test
    public void disabledNativeComponentsDoNotRequireTheirSentinels() throws Exception {
        File root = Files.createTempDirectory("wemu-disabled-components").toFile();
        assertTrue(ContainerBootstrap.areNativeWinComponentsMaterialized(
                root,
                "direct3d=0,directsound=0,directmusic=0,directshow=0,directplay=0,"
                        + "xaudio=0,vcrun2010=0"));
    }
}
