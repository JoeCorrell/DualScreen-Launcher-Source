package com.wemu.engine.core;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

import org.junit.Assume;
import org.junit.Test;

public class ArchivePathPolicyTest {
    @Test
    public void entryNamesStayBelowTheCanonicalRoot() throws Exception {
        File root = Files.createTempDirectory("wemu-archive-root").toFile();
        assertEquals(new File(root, "driver/lib.so").getCanonicalFile(),
                ArchivePathPolicy.resolveEntry(root, "./driver/lib.so").getCanonicalFile());

        assertThrows(IOException.class,
                () -> ArchivePathPolicy.resolveEntry(root, "../../outside"));
        assertThrows(IOException.class,
                () -> ArchivePathPolicy.resolveEntry(root, "..\\..\\outside"));
        assertThrows(IOException.class,
                () -> ArchivePathPolicy.resolveEntry(root, "/absolute/path"));
        assertThrows(IOException.class,
                () -> ArchivePathPolicy.resolveEntry(root, "C:\\absolute\\path"));
    }

    @Test
    public void untrustedLinksCannotEscapeOrUseHostAbsolutePaths() throws Exception {
        File root = Files.createTempDirectory("wemu-link-root").toFile();
        File link = new File(root, "lib/current");

        ArchivePathPolicy.validateSymbolicLink(root, link, "../versions/current", false);
        assertThrows(IOException.class,
                () -> ArchivePathPolicy.validateSymbolicLink(
                        root, link, "../../../outside", false));
        assertThrows(IOException.class,
                () -> ArchivePathPolicy.validateSymbolicLink(root, link, "/system/lib", false));

        // Trusted root-filesystem payloads may retain guest-absolute links; the
        // extractor still refuses to traverse them for later entries.
        ArchivePathPolicy.validateSymbolicLink(root, link, "/usr/lib", true);
    }

    @Test
    public void explicitExtractionRootDirectoryIsSafe() throws Exception {
        File parent = Files.createTempDirectory("wemu-directory-parent").toFile();
        File root = new File(parent, "payload");

        ArchivePathPolicy.createSafeDirectory(root, root);
        assertTrue(root.isDirectory());
    }

    @Test
    public void childDirectoryLinkIsStillRejected() throws Exception {
        File root = Files.createTempDirectory("wemu-directory-root").toFile();

        File target = Files.createTempDirectory("wemu-directory-target").toFile();
        File link = new File(root, "linked-directory");
        try {
            Files.createSymbolicLink(link.toPath(), target.toPath());
        } catch (IOException | UnsupportedOperationException | SecurityException unavailable) {
            Assume.assumeNoException("Symbolic links are unavailable", unavailable);
        }
        assertThrows(IOException.class,
                () -> ArchivePathPolicy.createSafeDirectory(root, link));
    }

    @Test
    public void metadataNamesAreSingleSafePathComponents() {
        assertTrue(ArchivePathPolicy.isSafePathComponent("Turnip 26.2.0"));
        assertFalse(ArchivePathPolicy.isSafePathComponent("../driver"));
        assertFalse(ArchivePathPolicy.isSafePathComponent("folder/driver"));
        assertFalse(ArchivePathPolicy.isSafePathComponent("folder\\driver"));
        assertFalse(ArchivePathPolicy.isSafePathComponent("C:driver"));
        assertFalse(ArchivePathPolicy.isSafePathComponent(".."));
    }
}
