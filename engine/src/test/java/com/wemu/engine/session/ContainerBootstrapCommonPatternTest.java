package com.wemu.engine.session;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;

import java.io.File;
import java.nio.file.Files;

import org.junit.Test;

public class ContainerBootstrapCommonPatternTest {
    @Test
    public void fixedArchiveHomeIsStrippedIntoTheRealContainer() throws Exception {
        File container = Files.createTempDirectory("wemu-common-pattern").toFile();

        assertNull(ContainerBootstrap.mapCommonPatternOutput(
                container, new File(container, "home")));
        assertEquals(container.getCanonicalFile(),
                ContainerBootstrap.mapCommonPatternOutput(
                        container, new File(container, "home/xuser")).getCanonicalFile());
        assertEquals(new File(container, ".wine/drive_c/windows/notepad.exe").getCanonicalFile(),
                ContainerBootstrap.mapCommonPatternOutput(container,
                        new File(container,
                                "home/xuser/.wine/drive_c/windows/notepad.exe"))
                        .getCanonicalFile());
    }

    @Test
    public void unexpectedArchiveRootsAreRejected() throws Exception {
        File container = Files.createTempDirectory("wemu-common-pattern-reject").toFile();

        assertThrows(IllegalArgumentException.class,
                () -> ContainerBootstrap.mapCommonPatternOutput(
                        container, new File(container, "usr/bin/unexpected")));
        assertThrows(IllegalArgumentException.class,
                () -> ContainerBootstrap.mapCommonPatternOutput(
                        container, new File(container, "home/xuser-other/file")));
    }
}
