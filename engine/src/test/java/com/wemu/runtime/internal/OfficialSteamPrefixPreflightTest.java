package com.wemu.runtime.internal;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

import org.junit.Test;

public class OfficialSteamPrefixPreflightTest {
    @Test
    public void prefixUpdateUsesWineLoaderInsteadOfPackagedShellWrapper() {
        assertArrayEquals(
                new String[]{
                        new File("box64").getAbsolutePath(),
                        new File("wine").getAbsolutePath(),
                        "wineboot.exe",
                        "-u",
                        "-r"
                },
                OfficialSteamPrefixPreflight.winebootCommand(
                        new File("box64"), new File("wine")));
    }

    @Test
    public void prefixUpdateDisablesOnlyInteractiveWineAddonInstallers() {
        assertTrue(OfficialSteamPrefixPreflight.preflightDllOverrides(null)
                .equals("mscoree=d;mshtml=d"));
        assertTrue(OfficialSteamPrefixPreflight.preflightDllOverrides(
                        "winemenubuilder.exe=d")
                .equals("winemenubuilder.exe=d;mscoree=d;mshtml=d"));
    }

    private static final String CURRENT_BOX64_SHA256 =
            "218C31023635152B94F51F9A30B5E2930BADF22F9C20C98B3651A2FC3AA75E5F";
    private static final String LEGACY_BOX64_SHA256 =
            "06443C6E561B45D3693468811977C8329D65577490156CE39A7F596BF19FF6DB";
    private static final String SAVE_MEM_BOX64_SHA256 =
            "6BE096B81C8EEA2E0B9F57FDAF1917EC7A3C3E476C6C5B18F34AFCC102448FCE";
    private static final String B38_SAVE_MEM_BOX64_SHA256 =
            "8DBB9D61A013B51C6C860DA54C29FD8A7A6C667E5A421242F521E4CA0E830EEF";
    private static final String B38_QSORT_SAVE_MEM_BOX64_SHA256 =
            "3DD5EC0B36DADF531F4C6616885F395FC443B6081F62C688F4BE084C6D2B48C7";

    @Test
    public void onlyExactManagedProton10And11SpecsRequireTheRepair() {
        assertTrue(revision("1", "proton10-x64").contains("|proton10-x64|"));
        assertTrue(revision("1", "proton11-x64").contains("|proton11-x64|"));

        assertNull(revision("1", "proton9-x64"));
        assertNull(revision("1", "proton12-x64"));
        assertNull(revision("", "proton10-x64"));
        assertNull(revision(null, "proton11-x64"));
    }

    @Test
    public void currentAndLegacyTuplesShareTheExistingCompletionIdentityByteForByte() {
        for (String spec : new String[]{"proton10-x64", "proton11-x64"}) {
            String current = revision("1", spec);
            assertEquals(current, legacyRevision("1", spec, "1"));
            assertEquals(current, legacyRevision("1", spec, ""));
            assertEquals(current, legacyRevision("1", spec, null));
        }
        assertEquals(
                revision("1", "proton10-x64"),
                previousProton10Revision());
        assertEquals(
                revision("1", "proton11-x64"),
                transitionalProton11Revision());
        assertEquals(
                revision("1", "proton11-x64"),
                previousProton11Revision());
        assertEquals(
                revision("1", "proton11-x64"),
                saveMemProton11Revision());
        assertEquals(
                revision("1", "proton11-x64"),
                b38Proton11Revision());

        assertEquals(
                "wineboot-update-restart-mode-v2"
                        + "|proton10-x64"
                        + "|Proton-10.0-4-x86_64-2"
                        + "|runtime-r1"
                        + "|wemu-official-steam-proton10-x64-r1-box64-0.4.2"
                        + "|06443C6E561B45D3693468811977C8329D65577490156CE39A7F596BF19FF6DB",
                revision("1", "proton10-x64"));
    }

    @Test
    public void completionIdentityRejectsRuntimeOrBox64HybridTuples() {
        assertThrows(
                IllegalArgumentException.class,
                () -> OfficialSteamPrefixPreflight.requiredRevision(
                        "1",
                        "proton10-x64",
                        "2",
                        "Proton-10.0-4-x86_64-2",
                        "0.4.2",
                        legacyBox64Marker("proton10-x64"),
                        LEGACY_BOX64_SHA256));
        assertThrows(
                IllegalArgumentException.class,
                () -> OfficialSteamPrefixPreflight.requiredRevision(
                        "1",
                        "proton11-x64",
                        "1",
                        "Proton-11.0-1-x86_64-1",
                        "0.4.4",
                        standardBox64Marker("proton11-x64"),
                        CURRENT_BOX64_SHA256));
        assertThrows(
                IllegalArgumentException.class,
                () -> OfficialSteamPrefixPreflight.requiredRevision(
                        "1",
                        "proton10-x64",
                        "2",
                        "Proton-10.0-4-x86_64-2",
                        "0.4.4",
                        legacyBox64Marker("proton10-x64"),
                        CURRENT_BOX64_SHA256));
        assertThrows(
                IllegalArgumentException.class,
                () -> OfficialSteamPrefixPreflight.requiredRevision(
                        "1",
                        "proton11-x64",
                        "2",
                        "Proton-11.0-1-x86_64-1",
                        "0.4.4",
                        standardBox64Marker("proton11-x64"),
                        LEGACY_BOX64_SHA256));
        assertThrows(
                IllegalArgumentException.class,
                () -> OfficialSteamPrefixPreflight.requiredRevision(
                        "1",
                        "proton11-x64",
                        "4",
                        "Proton-11.0-1-x86_64-1",
                        "0.4.2",
                        currentBox64Marker("proton11-x64"),
                        LEGACY_BOX64_SHA256));
    }

    @Test
    public void completionRequiresMatchingContainerAndPrefixRecords() throws IOException {
        File prefix = Files.createTempDirectory("steam-prefix-preflight").toFile();
        try {
            String required = revision("1", "proton10-x64");
            assertFalse(OfficialSteamPrefixPreflight.isCurrent(prefix, "", required));
            assertFalse(OfficialSteamPrefixPreflight.isCurrent(prefix, required, required));

            OfficialSteamPrefixPreflight.publishPrefixRevision(prefix, required);
            assertFalse(OfficialSteamPrefixPreflight.isCurrent(prefix, "stale", required));
            assertTrue(OfficialSteamPrefixPreflight.isCurrent(prefix, required, required));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void recreatedPrefixCannotInheritTheContainerRecord() throws IOException {
        File parent = Files.createTempDirectory("steam-prefix-parent").toFile();
        File prefix = new File(parent, "prefix");
        assertTrue(prefix.mkdir());
        try {
            String required = revision("1", "proton11-x64");
            OfficialSteamPrefixPreflight.publishPrefixRevision(prefix, required);
            assertTrue(OfficialSteamPrefixPreflight.isCurrent(prefix, required, required));

            deleteRecursively(prefix);
            assertTrue(prefix.mkdir());
            assertFalse(OfficialSteamPrefixPreflight.isCurrent(prefix, required, required));
        } finally {
            deleteRecursively(parent);
        }
    }

    @Test
    public void specialMarkerEntryIsRejectedInsteadOfReplaced() throws IOException {
        File prefix = Files.createTempDirectory("steam-prefix-special-marker").toFile();
        try {
            File marker = OfficialSteamPrefixPreflight.prefixMarkerFile(prefix);
            assertTrue(marker.mkdir());
            String required = revision("1", "proton10-x64");

            assertThrows(
                    IOException.class,
                    () -> OfficialSteamPrefixPreflight.publishPrefixRevision(prefix, required));
            assertThrows(
                    IOException.class,
                    () -> OfficialSteamPrefixPreflight.isCurrent(prefix, required, required));
        } finally {
            deleteRecursively(prefix);
        }
    }

    private static String revision(String managedMarker, String specMarker) {
        String wineVersion = "proton10-x64".equals(specMarker)
                ? "Proton-10.0-4-x86_64-2"
                : "Proton-11.0-1-x86_64-1";
        boolean proton11 = "proton11-x64".equals(specMarker);
        return OfficialSteamPrefixPreflight.requiredRevision(
                managedMarker,
                specMarker,
                proton11 ? "6" : "3",
                wineVersion,
                "0.4.4-b38a33c-qsort-save-mem",
                currentBox64Marker(specMarker),
                B38_QSORT_SAVE_MEM_BOX64_SHA256);
    }

    private static String previousProton10Revision() {
        return OfficialSteamPrefixPreflight.requiredRevision(
                "1",
                "proton10-x64",
                "2",
                "Proton-10.0-4-x86_64-2",
                "0.4.4",
                standardBox64Marker("proton10-x64"),
                CURRENT_BOX64_SHA256);
    }

    private static String transitionalProton11Revision() {
        return OfficialSteamPrefixPreflight.requiredRevision(
                "1",
                "proton11-x64",
                "2",
                "Proton-11.0-1-x86_64-1",
                "0.4.4",
                standardBox64Marker("proton11-x64"),
                CURRENT_BOX64_SHA256);
    }

    private static String previousProton11Revision() {
        return OfficialSteamPrefixPreflight.requiredRevision(
                "1",
                "proton11-x64",
                "3",
                "Proton-11.0-1-x86_64-1",
                "0.4.2",
                "wemu-official-steam-proton11-x64-r3-box64-0.4.2",
                LEGACY_BOX64_SHA256);
    }

    private static String saveMemProton11Revision() {
        return OfficialSteamPrefixPreflight.requiredRevision(
                "1",
                "proton11-x64",
                "4",
                "Proton-11.0-1-x86_64-1",
                "0.4.2-save-mem",
                "wemu-official-steam-proton11-x64-r4-box64-0.4.2-save-mem",
                SAVE_MEM_BOX64_SHA256);
    }

    private static String b38Proton11Revision() {
        return OfficialSteamPrefixPreflight.requiredRevision(
                "1",
                "proton11-x64",
                "5",
                "Proton-11.0-1-x86_64-1",
                "0.4.4-b38a33c-save-mem",
                "wemu-official-steam-proton11-x64-r5-box64-0.4.4-b38a33c-save-mem",
                B38_SAVE_MEM_BOX64_SHA256);
    }

    private static String legacyRevision(
            String managedMarker,
            String specMarker,
            String runtimeRevision) {
        String wineVersion = "proton10-x64".equals(specMarker)
                ? "Proton-10.0-4-x86_64-2"
                : "Proton-11.0-1-x86_64-1";
        return OfficialSteamPrefixPreflight.requiredRevision(
                managedMarker,
                specMarker,
                runtimeRevision,
                wineVersion,
                "0.4.2",
                legacyBox64Marker(specMarker),
                LEGACY_BOX64_SHA256);
    }

    private static String currentBox64Marker(String specMarker) {
        if ("proton11-x64".equals(specMarker)) {
            return "wemu-official-steam-proton11-x64-r6-box64-0.4.4-b38a33c-qsort-save-mem";
        }
        return "wemu-official-steam-proton10-x64-r3-box64-0.4.4-b38a33c-qsort-save-mem";
    }

    private static String standardBox64Marker(String specMarker) {
        return "wemu-official-steam-" + specMarker + "-r2-box64-0.4.4";
    }

    private static String legacyBox64Marker(String specMarker) {
        return "wemu-official-steam-" + specMarker + "-r1-box64-0.4.2";
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (!file.delete()) file.deleteOnExit();
    }
}
