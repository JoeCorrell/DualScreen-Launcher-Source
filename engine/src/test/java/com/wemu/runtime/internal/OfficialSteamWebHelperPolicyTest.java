package com.wemu.runtime.internal;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.junit.Assume.assumeTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFileAttributeView;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Set;

import org.junit.Test;

public class OfficialSteamWebHelperPolicyTest {
    private static final String TARGET_SECTION =
            "[Software\\\\Wine\\\\AppDefaults\\\\steamwebhelper.exe\\\\DllOverrides]";

    @Test
    public void onlyManagedX8664SteamOwnsThePolicy() {
        assertTrue(OfficialSteamWebHelperPolicy.shouldApply(true, false));
        assertFalse(OfficialSteamWebHelperPolicy.shouldApply(false, false));
        assertFalse(OfficialSteamWebHelperPolicy.shouldApply(true, true));
    }

    @Test
    public void addsOnlyThePerApplicationOverride() throws IOException {
        String original = "WINE REGISTRY Version 2\r\n"
                + ";; preserved comment\r\n"
                + "\r\n"
                + "[Software\\\\Wine\\\\DllOverrides] 1\r\n"
                + "#time=1\r\n"
                + "\"winegstreamer\"=\"native,builtin\"\r\n"
                + "\"mfplat\"=\"native\"\r\n";
        File prefix = prefixWithRegistry(original);
        try {
            assertTrue(OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(prefix));
            String updated = registry(prefix);

            assertTrue(updated.startsWith(original));
            assertTrue(updated.contains(TARGET_SECTION + "\r\n"
                    + "\"winegstreamer\"=\"\"\r\n"));
            assertTrue(updated.contains("[Software\\\\Wine\\\\DllOverrides] 1\r\n"
                    + "#time=1\r\n"
                    + "\"winegstreamer\"=\"native,builtin\"\r\n"
                    + "\"mfplat\"=\"native\"\r\n"));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void replacesOnlyAConflictingTargetValueAndPreservesOtherBytes() throws IOException {
        String original = "WINE REGISTRY Version 2\n"
                + ";; exact bytes around the managed value must survive\n"
                + "\n"
                + TARGET_SECTION + " 123\n"
                + "#time=abc\n"
                + "\"WineGStreamer\" = \"native,builtin\"\n"
                + "\"mfplat\"=\"native\"\n"
                + "\n"
                + "[Software\\\\Valve\\\\Steam] 456\n"
                + "\"Language\"=\"english\"\n";
        String expected = original.replace(
                "\"WineGStreamer\" = \"native,builtin\"",
                "\"WineGStreamer\" = \"\"");
        File prefix = prefixWithRegistry(original);
        try {
            assertTrue(OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(prefix));
            assertArrayEquals(
                    expected.getBytes(StandardCharsets.ISO_8859_1),
                    Files.readAllBytes(new File(prefix, "user.reg").toPath()));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void applyingThePolicyTwiceIsByteForByteIdempotent() throws IOException {
        File prefix = prefixWithRegistry("WINE REGISTRY Version 2\n");
        try {
            assertTrue(OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(prefix));
            byte[] first = Files.readAllBytes(new File(prefix, "user.reg").toPath());

            assertFalse(OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(prefix));
            assertArrayEquals(first, Files.readAllBytes(new File(prefix, "user.reg").toPath()));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void atomicReplacementPreservesPosixPermissionsWhenAvailable() throws IOException {
        File prefix = prefixWithRegistry("WINE REGISTRY Version 2\n");
        try {
            Path registry = new File(prefix, "user.reg").toPath();
            PosixFileAttributeView view = Files.getFileAttributeView(
                    registry,
                    PosixFileAttributeView.class,
                    LinkOption.NOFOLLOW_LINKS);
            assumeTrue("This filesystem does not expose POSIX permissions", view != null);
            Set<PosixFilePermission> expected = PosixFilePermissions.fromString("rw-r-----");
            Files.setPosixFilePermissions(registry, expected);

            assertTrue(OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(prefix));

            assertEquals(
                    expected,
                    Files.getPosixFilePermissions(registry, LinkOption.NOFOLLOW_LINKS));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void ambiguousDuplicateValuesFailWithoutPublishingAChange() throws IOException {
        String original = "WINE REGISTRY Version 2\n\n"
                + TARGET_SECTION + "\n"
                + "\"winegstreamer\"=\"native\"\n"
                + "\"WINEGSTREAMER\"=\"builtin\"\n";
        File prefix = prefixWithRegistry(original);
        try {
            try {
                OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(prefix);
                fail("Duplicate Wine values must fail closed");
            } catch (IOException expected) {
                // Expected: publishing either duplicate would leave an ambiguous load order.
            }
            assertArrayEquals(
                    original.getBytes(StandardCharsets.ISO_8859_1),
                    Files.readAllBytes(new File(prefix, "user.reg").toPath()));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void missingRegistryFailsClosed() throws IOException {
        File prefix = Files.createTempDirectory("steam-helper-policy-missing").toFile();
        try {
            try {
                OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(prefix);
                fail("A missing user.reg must prevent the managed launch");
            } catch (IOException expected) {
                // Expected.
            }
        } finally {
            deleteRecursively(prefix);
        }
    }

    private static File prefixWithRegistry(String contents) throws IOException {
        File prefix = Files.createTempDirectory("steam-helper-policy").toFile();
        Files.write(
                new File(prefix, "user.reg").toPath(),
                contents.getBytes(StandardCharsets.ISO_8859_1));
        return prefix;
    }

    private static String registry(File prefix) throws IOException {
        return new String(
                Files.readAllBytes(new File(prefix, "user.reg").toPath()),
                StandardCharsets.ISO_8859_1);
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (!file.delete()) file.deleteOnExit();
    }
}
