package com.wemu.engine.contents;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class AdrenotoolsManagerTest {
    @Test
    public void storageIdUsesPackageVersionInsteadOfUnsafeDisplayPath() {
        assertEquals(
                "26.3.0-devel-apex-ultimate-v2",
                AdrenotoolsManager.safeDriverId("26.3.0-devel-apex-ultimate-v2"));
        assertEquals(
                "Balemuni-Apex-Mesa-Adreno-740",
                AdrenotoolsManager.safeDriverId("Balemuni Apex (Mesa / Adreno 740)"));
    }

    @Test
    public void reservedTemporaryDirectoryNamesAreRejected() {
        assertEquals("", AdrenotoolsManager.safeDriverId("tmp"));
        assertEquals("", AdrenotoolsManager.safeDriverId("tmp-inner"));
    }
}
