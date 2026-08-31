package com.wemu.runtime.internal;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.Arrays;

/** One-shot prefix-maintenance state for the managed Proton 10/11 Steam runtimes. */
public final class OfficialSteamPrefixPreflight {
    public static final String MANAGED_RUNTIME_MARKER_KEY = "wemuOfficialSteamRuntime";
    public static final String SPEC_MARKER_KEY = "wemuOfficialSteamRuntimeSpec";
    public static final String CONTAINER_REVISION_KEY =
            "wemuOfficialSteamPrefixPreflightRevision";

    public static final String PROTON_10_SPEC = "proton10-x64";
    public static final String PROTON_11_SPEC = "proton11-x64";

    private static final String MANAGED_RUNTIME_SCHEMA = "1";
    private static final String PREVIOUS_STANDARD_MANAGED_RUNTIME_REVISION = "2";
    private static final String PROTON_10_MANAGED_RUNTIME_REVISION = "3";
    private static final String PREVIOUS_PROTON_11_MANAGED_RUNTIME_REVISION = "3";
    private static final String SAVE_MEM_PROTON_11_MANAGED_RUNTIME_REVISION = "4";
    private static final String B38_PROTON_11_MANAGED_RUNTIME_REVISION = "5";
    private static final String PROTON_11_MANAGED_RUNTIME_REVISION = "6";
    private static final String BOX64_0_4_4_VERSION = "0.4.4";
    private static final String BOX64_0_4_4_SHA256 =
            "218C31023635152B94F51F9A30B5E2930BADF22F9C20C98B3651A2FC3AA75E5F";
    private static final String LEGACY_MANAGED_RUNTIME_REVISION = "1";
    private static final String BOX64_0_4_2_VERSION = "0.4.2";
    private static final String BOX64_0_4_2_SHA256 =
            "06443C6E561B45D3693468811977C8329D65577490156CE39A7F596BF19FF6DB";
    private static final String BOX64_0_4_2_SAVE_MEM_VERSION = "0.4.2-save-mem";
    private static final String BOX64_0_4_2_SAVE_MEM_SHA256 =
            "6BE096B81C8EEA2E0B9F57FDAF1917EC7A3C3E476C6C5B18F34AFCC102448FCE";
    private static final String BOX64_B38_SAVE_MEM_VERSION = "0.4.4-b38a33c-save-mem";
    private static final String BOX64_B38_SAVE_MEM_SHA256 =
            "8DBB9D61A013B51C6C860DA54C29FD8A7A6C667E5A421242F521E4CA0E830EEF";
    private static final String BOX64_B38_QSORT_SAVE_MEM_VERSION =
            "0.4.4-b38a33c-qsort-save-mem";
    private static final String BOX64_B38_QSORT_SAVE_MEM_SHA256 =
            "3DD5EC0B36DADF531F4C6616885F395FC443B6081F62C688F4BE084C6D2B48C7";
    private static final String PREFLIGHT_SCHEMA = "wineboot-update-restart-mode-v2";
    private static final String ADDON_INSTALLER_OVERRIDES = "mscoree=d;mshtml=d";
    private static final String PREFIX_MARKER_NAME =
            ".wemu-official-steam-prefix-preflight";
    private static final int MAXIMUM_MARKER_BYTES = 320;

    private OfficialSteamPrefixPreflight() {}

    /**
     * Builds the Wine prefix-update command without invoking Wine's generated
     * {@code bin/wineboot} shell wrapper. The P10 package ships that wrapper as
     * a {@code /bin/sh} script; Box64 must execute the x86_64 Wine loader and
     * let Wine resolve {@code wineboot.exe} inside the prefix instead.
     */
    public static String[] winebootCommand(File box64, File wineLoader) {
        if (box64 == null || wineLoader == null) {
            throw new IllegalArgumentException("Box64 and the Wine loader are required");
        }
        return new String[]{
                box64.getAbsolutePath(),
                wineLoader.getAbsolutePath(),
                "wineboot.exe",
                "-u",
                "-r"
        };
    }

    /**
     * Makes the mandatory prefix update non-interactive without changing the
     * eventual Steam or game process environment. Registering Wine's builtin
     * mscoree/mshtml DLLs can synchronously open the optional Mono/Gecko
     * downloader and wait forever for a modal dialog. Those add-ons are not
     * part of the prefix schema migration, so suppress them only for this
     * bounded maintenance command.
     */
    public static String preflightDllOverrides(String existing) {
        if (existing == null || existing.isEmpty()) return ADDON_INSTALLER_OVERRIDES;
        return existing + ";" + ADDON_INSTALLER_OVERRIDES;
    }

    /**
     * Returns the revision this exact managed runtime needs, or {@code null}
     * for Proton 9, ordinary containers, and unknown future specifications.
     */
    public static String requiredRevision(
            String managedMarker,
            String specMarker,
            String runtimeRevision,
            String wineVersion,
            String box64Version,
            String box64MarkerId,
            String box64Sha256) {
        if (!MANAGED_RUNTIME_SCHEMA.equals(managedMarker)) return null;
        if (!PROTON_10_SPEC.equals(specMarker) && !PROTON_11_SPEC.equals(specMarker)) {
            return null;
        }

        String expectedWine = PROTON_10_SPEC.equals(specMarker)
                ? "Proton-10.0-4-x86_64-2"
                : "Proton-11.0-1-x86_64-1";
        String legacyBox64Marker = "wemu-official-steam-" + specMarker
                + "-r" + LEGACY_MANAGED_RUNTIME_REVISION
                + "-box64-" + BOX64_0_4_2_VERSION;
        boolean legacyRevision = runtimeRevision == null
                || runtimeRevision.isEmpty()
                || LEGACY_MANAGED_RUNTIME_REVISION.equals(runtimeRevision);
        boolean legacyTuple = legacyRevision
                && expectedWine.equals(wineVersion)
                && BOX64_0_4_2_VERSION.equals(box64Version)
                && legacyBox64Marker.equals(box64MarkerId)
                && BOX64_0_4_2_SHA256.equals(box64Sha256);

        String standardBox64Marker = "wemu-official-steam-" + specMarker
                + "-r" + PREVIOUS_STANDARD_MANAGED_RUNTIME_REVISION
                + "-box64-" + BOX64_0_4_4_VERSION;
        boolean standardTuple = PREVIOUS_STANDARD_MANAGED_RUNTIME_REVISION.equals(runtimeRevision)
                && expectedWine.equals(wineVersion)
                && BOX64_0_4_4_VERSION.equals(box64Version)
                && standardBox64Marker.equals(box64MarkerId)
                && BOX64_0_4_4_SHA256.equals(box64Sha256);

        String previousProton11Box64Marker = "wemu-official-steam-" + specMarker
                + "-r" + PREVIOUS_PROTON_11_MANAGED_RUNTIME_REVISION
                + "-box64-" + BOX64_0_4_2_VERSION;
        boolean previousProton11Tuple = PROTON_11_SPEC.equals(specMarker)
                && PREVIOUS_PROTON_11_MANAGED_RUNTIME_REVISION.equals(runtimeRevision)
                && expectedWine.equals(wineVersion)
                && BOX64_0_4_2_VERSION.equals(box64Version)
                && previousProton11Box64Marker.equals(box64MarkerId)
                && BOX64_0_4_2_SHA256.equals(box64Sha256);

        String saveMemProton11Box64Marker = "wemu-official-steam-" + specMarker
                + "-r" + SAVE_MEM_PROTON_11_MANAGED_RUNTIME_REVISION
                + "-box64-" + BOX64_0_4_2_SAVE_MEM_VERSION;
        boolean saveMemProton11Tuple = PROTON_11_SPEC.equals(specMarker)
                && SAVE_MEM_PROTON_11_MANAGED_RUNTIME_REVISION.equals(runtimeRevision)
                && expectedWine.equals(wineVersion)
                && BOX64_0_4_2_SAVE_MEM_VERSION.equals(box64Version)
                && saveMemProton11Box64Marker.equals(box64MarkerId)
                && BOX64_0_4_2_SAVE_MEM_SHA256.equals(box64Sha256);

        String b38Proton11Box64Marker = "wemu-official-steam-" + specMarker
                + "-r" + B38_PROTON_11_MANAGED_RUNTIME_REVISION
                + "-box64-" + BOX64_B38_SAVE_MEM_VERSION;
        boolean b38Proton11Tuple = PROTON_11_SPEC.equals(specMarker)
                && B38_PROTON_11_MANAGED_RUNTIME_REVISION.equals(runtimeRevision)
                && expectedWine.equals(wineVersion)
                && BOX64_B38_SAVE_MEM_VERSION.equals(box64Version)
                && b38Proton11Box64Marker.equals(box64MarkerId)
                && BOX64_B38_SAVE_MEM_SHA256.equals(box64Sha256);

        String proton11Box64Marker = "wemu-official-steam-" + specMarker
                + "-r" + PROTON_11_MANAGED_RUNTIME_REVISION
                + "-box64-" + BOX64_B38_QSORT_SAVE_MEM_VERSION;
        boolean proton11CurrentTuple = PROTON_11_SPEC.equals(specMarker)
                && PROTON_11_MANAGED_RUNTIME_REVISION.equals(runtimeRevision)
                && expectedWine.equals(wineVersion)
                && BOX64_B38_QSORT_SAVE_MEM_VERSION.equals(box64Version)
                && proton11Box64Marker.equals(box64MarkerId)
                && BOX64_B38_QSORT_SAVE_MEM_SHA256.equals(box64Sha256);

        String proton10Box64Marker = "wemu-official-steam-" + specMarker
                + "-r" + PROTON_10_MANAGED_RUNTIME_REVISION
                + "-box64-" + BOX64_B38_QSORT_SAVE_MEM_VERSION;
        boolean proton10CurrentTuple = PROTON_10_SPEC.equals(specMarker)
                && PROTON_10_MANAGED_RUNTIME_REVISION.equals(runtimeRevision)
                && expectedWine.equals(wineVersion)
                && BOX64_B38_QSORT_SAVE_MEM_VERSION.equals(box64Version)
                && proton10Box64Marker.equals(box64MarkerId)
                && BOX64_B38_QSORT_SAVE_MEM_SHA256.equals(box64Sha256);
        boolean proton10PreviousTuple = PROTON_10_SPEC.equals(specMarker) && standardTuple;
        // P11 r2 is narrowly accepted so an existing config can launch while
        // the app atomically advances its identity to canonical r3.
        boolean proton11TransitionalTuple = PROTON_11_SPEC.equals(specMarker) && standardTuple;
        if (!proton10CurrentTuple
                && !proton10PreviousTuple
                && !proton11CurrentTuple
                && !b38Proton11Tuple
                && !saveMemProton11Tuple
                && !previousProton11Tuple
                && !proton11TransitionalTuple
                && !legacyTuple) {
            throw new IllegalArgumentException(
                    "The managed Steam prefix preflight runtime tuple is not allowlisted");
        }

        // Changing the authenticated translator does not change the Wine
        // prefix schema. Return the exact r1 completion identity for P10 r2/r3,
        // P11 r1/r2/r3/r4/r5/r6, so translator-only migrations do not rerun wineboot.
        return PREFLIGHT_SCHEMA
                + "|" + specMarker
                + "|" + expectedWine
                + "|runtime-r" + LEGACY_MANAGED_RUNTIME_REVISION
                + "|" + legacyBox64Marker
                + "|" + BOX64_0_4_2_SHA256;
    }

    /**
     * Both records are required. The prefix-side record prevents a surviving
     * container config from skipping maintenance after its .wine directory is
     * deleted and recreated.
     */
    public static boolean isCurrent(
            File winePrefix,
            String containerRevision,
            String requiredRevision) throws IOException {
        if (requiredRevision == null || !requiredRevision.equals(containerRevision)) {
            return false;
        }
        Path marker = markerPath(winePrefix);
        if (!Files.exists(marker, LinkOption.NOFOLLOW_LINKS)) return false;
        return Arrays.equals(readRegularMarker(marker), markerBytes(requiredRevision));
    }

    /** Atomically records completion inside the exact canonical Wine prefix. */
    public static void publishPrefixRevision(
            File winePrefix,
            String requiredRevision) throws IOException {
        if (requiredRevision == null || requiredRevision.isEmpty()) {
            throw new IOException("The Steam prefix preflight revision is unavailable");
        }
        Path marker = markerPath(winePrefix);
        if (Files.exists(marker, LinkOption.NOFOLLOW_LINKS)) {
            requireRegularNoFollow(marker, "Steam prefix preflight marker");
        }

        Path temporary = Files.createTempFile(
                marker.getParent(), ".wemu-steam-preflight-", ".tmp");
        boolean published = false;
        try {
            requireRegularNoFollow(temporary, "Steam prefix preflight staging file");
            byte[] contents = markerBytes(requiredRevision);
            try (FileChannel channel = FileChannel.open(
                    temporary,
                    StandardOpenOption.WRITE,
                    StandardOpenOption.TRUNCATE_EXISTING,
                    LinkOption.NOFOLLOW_LINKS)) {
                ByteBuffer source = ByteBuffer.wrap(contents);
                while (source.hasRemaining()) channel.write(source);
                channel.force(true);
            }
            Files.move(
                    temporary,
                    marker,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            published = true;
        } finally {
            if (!published) Files.deleteIfExists(temporary);
        }

        if (!Arrays.equals(readRegularMarker(marker), markerBytes(requiredRevision))) {
            throw new IOException("The Steam prefix preflight marker could not be verified");
        }
    }

    static File prefixMarkerFile(File winePrefix) throws IOException {
        return markerPath(winePrefix).toFile();
    }

    private static Path markerPath(File winePrefix) throws IOException {
        if (winePrefix == null) throw new IOException("The Wine prefix is unavailable");
        Path supplied = winePrefix.toPath().toAbsolutePath().normalize();
        Path canonical = winePrefix.getCanonicalFile().toPath().toAbsolutePath().normalize();
        if (!supplied.equals(canonical)) {
            throw new IOException("The Wine prefix path is not canonical: " + supplied);
        }
        if (Files.isSymbolicLink(canonical)
                || !Files.isDirectory(canonical, LinkOption.NOFOLLOW_LINKS)) {
            throw new IOException("The Wine prefix is not a regular directory: " + canonical);
        }
        return canonical.resolve(PREFIX_MARKER_NAME);
    }

    private static byte[] readRegularMarker(Path marker) throws IOException {
        BasicFileAttributes before = requireRegularNoFollow(
                marker, "Steam prefix preflight marker");
        if (before.size() > MAXIMUM_MARKER_BYTES || before.size() > Integer.MAX_VALUE) {
            throw new IOException("The Steam prefix preflight marker is too large");
        }

        byte[] contents = new byte[(int) before.size()];
        try (FileChannel channel = FileChannel.open(
                marker, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
            ByteBuffer target = ByteBuffer.wrap(contents);
            while (target.hasRemaining()) {
                if (channel.read(target) < 0) {
                    throw new IOException("The Steam prefix preflight marker was truncated");
                }
            }
            if (channel.read(ByteBuffer.allocate(1)) != -1) {
                throw new IOException("The Steam prefix preflight marker grew while reading");
            }
        }

        BasicFileAttributes after = requireRegularNoFollow(
                marker, "Steam prefix preflight marker");
        if (before.size() != after.size()
                || !before.lastModifiedTime().equals(after.lastModifiedTime())
                || (before.fileKey() != null && !before.fileKey().equals(after.fileKey()))) {
            throw new IOException("The Steam prefix preflight marker changed while reading");
        }
        return contents;
    }

    private static BasicFileAttributes requireRegularNoFollow(Path path, String label)
            throws IOException {
        BasicFileAttributes attributes = Files.readAttributes(
                path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        if (attributes.isSymbolicLink() || !attributes.isRegularFile()) {
            throw new IOException(label + " is not a regular file: " + path);
        }
        return attributes;
    }

    private static byte[] markerBytes(String revision) {
        return (revision + "\n").getBytes(StandardCharsets.UTF_8);
    }
}
