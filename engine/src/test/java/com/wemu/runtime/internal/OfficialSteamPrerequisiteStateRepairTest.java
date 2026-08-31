package com.wemu.runtime.internal;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import org.junit.Test;

public class OfficialSteamPrerequisiteStateRepairTest {
    private static final String HEADER = "WINE REGISTRY Version 2\n\n";
    private static final String DOTNET_4 =
            "Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.0";
    private static final String DOTNET_4_CLIENT =
            "Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist"
                    + "\\.NET\\4.0 Client Profile";
    private static final String DOTNET_48 =
            "Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.8";
    private static final String XNA_4 =
            "Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\XNA\\4.0";
    private static final String NDP_CLIENT =
            "Software\\Microsoft\\NET Framework Setup\\NDP\\v4\\Client";
    private static final String NDP_FULL =
            "Software\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full";
    private static final String XNA_PRODUCT =
            "Software\\Wow6432Node\\Microsoft\\XNA\\Framework\\v4.0";

    @Test
    public void unmanagedPrefixIsNeverEdited() throws Exception {
        File root = Files.createTempDirectory("steam-prereq-unmanaged").toFile();
        try {
            Path registry = writeRegistry(root, falseMarkerRegistry());
            byte[] before = Files.readAllBytes(registry);

            OfficialSteamPrerequisiteStateRepair.Result result =
                    OfficialSteamPrerequisiteStateRepair.repair(
                            root, "", OfficialSteamPrefixPreflight.PROTON_11_SPEC);

            assertFalse(result.applicable);
            assertFalse(result.changed());
            assertArrayEquals(before, Files.readAllBytes(registry));
        } finally {
            deleteRecursively(root);
        }
    }

    @Test
    public void unknownManagedRuntimeSpecIsNeverEdited() throws Exception {
        File root = Files.createTempDirectory("steam-prereq-unknown").toFile();
        try {
            Path registry = writeRegistry(root, falseMarkerRegistry());
            byte[] before = Files.readAllBytes(registry);

            OfficialSteamPrerequisiteStateRepair.Result result =
                    OfficialSteamPrerequisiteStateRepair.repair(
                            root, "1", "proton12-unvalidated");

            assertFalse(result.applicable);
            assertArrayEquals(before, Files.readAllBytes(registry));
        } finally {
            deleteRecursively(root);
        }
    }

    @Test
    public void removesOnlyFalseDotNet4AndXnaValues() throws Exception {
        File root = Files.createTempDirectory("steam-prereq-false").toFile();
        try {
            String source = falseMarkerRegistry();
            Path registry = writeRegistry(root, source);

            OfficialSteamPrerequisiteStateRepair.Result result =
                    OfficialSteamPrerequisiteStateRepair.repair(
                            root, "1", OfficialSteamPrefixPreflight.PROTON_11_SPEC);

            assertTrue(result.applicable);
            assertTrue(result.changed());
            assertEquals(4, result.valuesRemoved);

            String expected = HEADER
                    + section(DOTNET_4, dword("keep"))
                    + section(DOTNET_4_CLIENT)
                    + section(DOTNET_48, dword("4.8"))
                    + section(XNA_4, dword("unrelated"));
            assertEquals(expected, readRegistry(registry));
        } finally {
            deleteRecursively(root);
        }
    }

    @Test
    public void realMicrosoftRegistryAndFilesPreserveValveMarkers() throws Exception {
        File root = Files.createTempDirectory("steam-prereq-real").toFile();
        try {
            String source = HEADER
                    + section(DOTNET_4, dword("4.0"))
                    + section(DOTNET_4_CLIENT, dword("4.0 Client Profile"))
                    + section(XNA_4, dword("4.0 Refresh"))
                    + section(NDP_CLIENT, dword("Install"))
                    + section(NDP_FULL, dword("Install"))
                    + section(XNA_PRODUCT, dword("Installed"), dword("Refresh1Installed"));
            Path registry = writeRegistry(root, source);
            createDotNetClientFiles(root);
            createDotNetFullFiles(root);
            createXnaFiles(root);
            byte[] before = Files.readAllBytes(registry);

            OfficialSteamPrerequisiteStateRepair.Result result =
                    OfficialSteamPrerequisiteStateRepair.repair(
                            root, "1", OfficialSteamPrefixPreflight.PROTON_10_SPEC);

            assertTrue(result.applicable);
            assertFalse(result.changed());
            assertArrayEquals(before, Files.readAllBytes(registry));
        } finally {
            deleteRecursively(root);
        }
    }

    @Test
    public void partialInstallKeepsOnlyTheMarkerItCanProve() throws Exception {
        File root = Files.createTempDirectory("steam-prereq-partial").toFile();
        try {
            String source = HEADER
                    + section(DOTNET_4, dword("4.0"))
                    + section(DOTNET_4_CLIENT, dword("4.0 Client Profile"))
                    + section(XNA_4, dword("4.0"), dword("4.0 Refresh"))
                    + section(NDP_CLIENT, dword("Install"))
                    + section(XNA_PRODUCT, dword("Installed"), dword("Refresh1Installed"));
            Path registry = writeRegistry(root, source);
            createDotNetClientFiles(root);

            OfficialSteamPrerequisiteStateRepair.Result result =
                    OfficialSteamPrerequisiteStateRepair.repair(
                            root, "1", "proton9-x64");

            assertEquals(3, result.valuesRemoved);
            String repaired = readRegistry(registry);
            assertTrue(repaired.contains(dword("4.0 Client Profile")));
            assertFalse(repaired.contains(dword("4.0 Refresh")));
            assertFalse(repaired.contains(section(DOTNET_4, dword("4.0"))));
        } finally {
            deleteRecursively(root);
        }
    }

    @Test
    public void secondPassIsByteForByteIdempotent() throws Exception {
        File root = Files.createTempDirectory("steam-prereq-idempotent").toFile();
        try {
            Path registry = writeRegistry(root, falseMarkerRegistry());
            OfficialSteamPrerequisiteStateRepair.Result first =
                    OfficialSteamPrerequisiteStateRepair.repair(
                            root, "1", OfficialSteamPrefixPreflight.PROTON_11_SPEC);
            byte[] once = Files.readAllBytes(registry);

            OfficialSteamPrerequisiteStateRepair.Result second =
                    OfficialSteamPrerequisiteStateRepair.repair(
                            root, "1", OfficialSteamPrefixPreflight.PROTON_11_SPEC);

            assertTrue(first.changed());
            assertFalse(second.changed());
            assertArrayEquals(once, Files.readAllBytes(registry));
        } finally {
            deleteRecursively(root);
        }
    }

    @Test
    public void malformedRegistryFailsWithoutChangingTheSource() throws Exception {
        File root = Files.createTempDirectory("steam-prereq-malformed").toFile();
        try {
            Path registry = writeRegistry(root, "not a Wine registry\n" + falseMarkerRegistry());
            byte[] before = Files.readAllBytes(registry);

            assertThrows(
                    IOException.class,
                    () -> OfficialSteamPrerequisiteStateRepair.repair(
                            root, "1", OfficialSteamPrefixPreflight.PROTON_11_SPEC));
            assertArrayEquals(before, Files.readAllBytes(registry));
        } finally {
            deleteRecursively(root);
        }
    }

    private static String falseMarkerRegistry() {
        return HEADER
                + section(DOTNET_4, dword("4.0"), dword("keep"))
                + section(DOTNET_4_CLIENT, dword("4.0 Client Profile"))
                + section(DOTNET_48, dword("4.8"))
                + section(XNA_4,
                        dword("4.0"),
                        dword("4.0 Refresh"),
                        dword("unrelated"));
    }

    private static String section(String logicalKey, String... values) {
        StringBuilder result = new StringBuilder()
                .append('[')
                .append(logicalKey.replace("\\", "\\\\"))
                .append("] 1\n#time=1\n");
        for (String value : values) result.append(value).append('\n');
        return result.append('\n').toString();
    }

    private static String dword(String name) {
        return "\"" + name + "\"=dword:00000001";
    }

    private static Path writeRegistry(File root, String contents) throws IOException {
        Path prefix = root.toPath().resolve(".wine");
        Files.createDirectories(prefix);
        Path registry = prefix.resolve("system.reg");
        Files.write(registry, contents.getBytes(StandardCharsets.ISO_8859_1));
        return registry;
    }

    private static String readRegistry(Path registry) throws IOException {
        return new String(Files.readAllBytes(registry), StandardCharsets.ISO_8859_1);
    }

    private static void createDotNetClientFiles(File root) throws IOException {
        createFile(root, ".wine/drive_c/windows/Microsoft.NET/Framework"
                + "/v4.0.30319/mscorlib.dll");
        createFile(root, ".wine/drive_c/windows/Microsoft.NET/Framework64"
                + "/v4.0.30319/mscorlib.dll");
    }

    private static void createDotNetFullFiles(File root) throws IOException {
        createFile(root, ".wine/drive_c/windows/Microsoft.NET/Framework"
                + "/v4.0.30319/System.Web.dll");
        createFile(root, ".wine/drive_c/windows/Microsoft.NET/Framework64"
                + "/v4.0.30319/System.Web.dll");
    }

    private static void createXnaFiles(File root) throws IOException {
        createFile(root, ".wine/drive_c/Program Files (x86)/Common Files/Microsoft Shared"
                + "/XNA/Framework/v4.0/XnaNative.dll");
    }

    private static void createFile(File root, String relative) throws IOException {
        Path path = root.toPath().resolve(relative);
        Files.createDirectories(path.getParent());
        Files.write(path, new byte[]{1});
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        file.delete();
    }
}
