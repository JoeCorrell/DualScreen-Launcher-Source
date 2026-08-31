package com.wemu.engine.xenvironment.components;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.OpenOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Authenticated version marker for the one Box64 binary shared by containers. */
final class SharedBox64RuntimeState {
    static final String MARKER_NAME = "active-version-v1";
    static final String MANAGED_STEAM_CONTAINER_MARKER_KEY = "wemuOfficialSteamRuntime";
    static final String MANAGED_STEAM_SPEC_MARKER_KEY = "wemuOfficialSteamRuntimeSpec";
    static final String MANAGED_STEAM_CONTAINER_REVISION_KEY =
            "wemuOfficialSteamRuntimeRevision";
    static final String MANAGED_STEAM_CONTAINER_MARKER = "1";
    static final String BOX64_0_4_4_VERSION = "0.4.4";
    static final String BOX64_0_4_4_SHA256 =
            "218C31023635152B94F51F9A30B5E2930BADF22F9C20C98B3651A2FC3AA75E5F";
    static final String BOX64_0_4_2_VERSION = "0.4.2";
    static final String BOX64_0_4_2_SHA256 =
            "06443C6E561B45D3693468811977C8329D65577490156CE39A7F596BF19FF6DB";
    static final String BOX64_0_4_2_SAVE_MEM_VERSION = "0.4.2-save-mem";
    static final String BOX64_0_4_2_SAVE_MEM_SHA256 =
            "6BE096B81C8EEA2E0B9F57FDAF1917EC7A3C3E476C6C5B18F34AFCC102448FCE";
    static final String BOX64_B38_SAVE_MEM_VERSION = "0.4.4-b38a33c-save-mem";
    static final String BOX64_B38_SAVE_MEM_SHA256 =
            "8DBB9D61A013B51C6C860DA54C29FD8A7A6C667E5A421242F521E4CA0E830EEF";
    static final String BOX64_B38_QSORT_SAVE_MEM_VERSION =
            "0.4.4-b38a33c-qsort-save-mem";
    static final String BOX64_B38_QSORT_SAVE_MEM_SHA256 =
            "3DD5EC0B36DADF531F4C6616885F395FC443B6081F62C688F4BE084C6D2B48C7";
    private static final String PREVIOUS_MANAGED_STEAM_CONTAINER_REVISION = "2";
    private static final String MANAGED_STEAM_CONTAINER_REVISION = "3";
    private static final String PREVIOUS_PROTON_11_MANAGED_STEAM_CONTAINER_REVISION = "3";
    private static final String SAVE_MEM_PROTON_11_MANAGED_STEAM_CONTAINER_REVISION = "4";
    private static final String B38_PROTON_11_MANAGED_STEAM_CONTAINER_REVISION = "5";
    private static final String PROTON_11_MANAGED_STEAM_CONTAINER_REVISION = "6";
    private static final String LEGACY_MANAGED_STEAM_CONTAINER_REVISION = "1";
    private static final ManagedRuntimeSpec[] MANAGED_STEAM_RUNTIMES = {
        // Keep every canonical Wine/translator pairing explicit so a future
        // runtime cannot inherit a binary merely because it carries the
        // managed marker. Every canonical x64 Steam runtime now selects the
        // authenticated post-v0.4.4 SAVE_MEM build with the Android qsort_r
        // correction that prevents zero-progress MemoryInfra enumeration.
        new ManagedRuntimeSpec(
                "proton9-x64",
                "Proton-9.0-x86_64-0",
                MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_B38_QSORT_SAVE_MEM_VERSION,
                BOX64_B38_QSORT_SAVE_MEM_SHA256,
                false,
                false),
        new ManagedRuntimeSpec(
                "proton10-x64",
                "Proton-10.0-4-x86_64-2",
                MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_B38_QSORT_SAVE_MEM_VERSION,
                BOX64_B38_QSORT_SAVE_MEM_SHA256,
                false,
                false),

        // P9/P10 r2 used the ordinary authenticated 0.4.4 translator. Keep
        // these exact tuples launchable while the app migrates only their
        // managed metadata and shared Box64 binary to r3.
        new ManagedRuntimeSpec(
                "proton9-x64",
                "Proton-9.0-x86_64-0",
                PREVIOUS_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_4_VERSION,
                BOX64_0_4_4_SHA256,
                false,
                false),
        new ManagedRuntimeSpec(
                "proton10-x64",
                "Proton-10.0-4-x86_64-2",
                PREVIOUS_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_4_VERSION,
                BOX64_0_4_4_SHA256,
                false,
                false),
        new ManagedRuntimeSpec(
                "proton11-x64",
                "Proton-11.0-1-x86_64-1",
                PROTON_11_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_B38_QSORT_SAVE_MEM_VERSION,
                BOX64_B38_QSORT_SAVE_MEM_SHA256,
                false,
                false),

        // r5 used the same authenticated b38 SAVE_MEM source without the
        // Android qsort_r partition correction. Keep the exact tuple
        // launchable while the app migrates the container to r6.
        new ManagedRuntimeSpec(
                "proton11-x64",
                "Proton-11.0-1-x86_64-1",
                B38_PROTON_11_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_B38_SAVE_MEM_VERSION,
                BOX64_B38_SAVE_MEM_SHA256,
                false,
                false),

        // r4 used the authenticated 0.4.2 SAVE_MEM binary. Keep this exact
        // tuple launchable while the app migrates the container to r5.
        new ManagedRuntimeSpec(
                "proton11-x64",
                "Proton-11.0-1-x86_64-1",
                SAVE_MEM_PROTON_11_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_2_SAVE_MEM_VERSION,
                BOX64_0_4_2_SAVE_MEM_SHA256,
                false,
                false),

        // r3 used the ordinary authenticated 0.4.2 binary. Keep this exact
        // tuple launchable while the app migrates the container to r5.
        new ManagedRuntimeSpec(
                "proton11-x64",
                "Proton-11.0-1-x86_64-1",
                PREVIOUS_PROTON_11_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_2_VERSION,
                BOX64_0_4_2_SHA256,
                false,
                false),

        // r2 was published briefly with Box64 0.4.4. Keep the exact tuple
        // launchable while the app atomically migrates its config to P11 r5;
        // no other r2/translator combination is inferred.
        new ManagedRuntimeSpec(
                "proton11-x64",
                "Proton-11.0-1-x86_64-1",
                PREVIOUS_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_4_VERSION,
                BOX64_0_4_4_SHA256,
                false,
                false),

        // These exact legacy tuples keep existing containers repairable while
        // the app migrates their metadata in place. The missing-field flags
        // reproduce only layouts emitted before the corresponding keys were introduced.
        new ManagedRuntimeSpec(
                "proton9-x64",
                "Proton-9.0-x86_64-0",
                LEGACY_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_2_VERSION,
                BOX64_0_4_2_SHA256,
                true,
                true),
        new ManagedRuntimeSpec(
                "proton10-x64",
                "Proton-10.0-4-x86_64-2",
                LEGACY_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_2_VERSION,
                BOX64_0_4_2_SHA256,
                true,
                false),
        new ManagedRuntimeSpec(
                "proton11-x64",
                "Proton-11.0-1-x86_64-1",
                LEGACY_MANAGED_STEAM_CONTAINER_REVISION,
                BOX64_0_4_2_VERSION,
                BOX64_0_4_2_SHA256,
                true,
                false),
    };
    private static final byte[] MARKER_MAGIC =
            "WEMU_BOX64_V1\n".getBytes(StandardCharsets.US_ASCII);
    private static final int SHA256_LENGTH = 32;
    private static final int MAX_VERSION_LENGTH = 256;
    private static final int MAX_MARKER_LENGTH =
            MARKER_MAGIC.length + 2 + MAX_VERSION_LENGTH + SHA256_LENGTH;
    private static final int COPY_BUFFER_SIZE = 64 * 1024;

    private SharedBox64RuntimeState() {}

    static File markerFile(File privateRuntimeDirectory) {
        return new File(privateRuntimeDirectory, MARKER_NAME);
    }

    static RuntimeRequest runtimeRequest(
            String managedSteamContainerMarker,
            String managedSteamSpecMarker,
            String managedSteamContainerRevision,
            String wineVersion,
            String selectedVersion) {
        if (!MANAGED_STEAM_CONTAINER_MARKER.equals(managedSteamContainerMarker)) {
            return new RuntimeRequest(selectedVersion, selectedVersion, null, true);
        }

        ManagedRuntimeSpec spec = findManagedRuntime(
                managedSteamSpecMarker,
                managedSteamContainerRevision,
                wineVersion);
        if (spec == null || !spec.box64Version.equals(selectedVersion)) {
            throw new IllegalStateException(
                    "The managed Steam container has an unknown runtime/Box64 tuple");
        }
        return new RuntimeRequest(
                spec.box64Version,
                spec.markerId,
                spec.requiredSha256,
                false);
    }

    private static ManagedRuntimeSpec findManagedRuntime(
            String specMarker,
            String containerRevision,
            String wineVersion) {
        boolean missingSpecMarker = isEmpty(specMarker);
        boolean missingRevision = isEmpty(containerRevision);
        for (ManagedRuntimeSpec candidate : MANAGED_STEAM_RUNTIMES) {
            if (!candidate.wineVersion.equals(wineVersion)) continue;

            if (missingSpecMarker) {
                // Only Proton 9 existed before the per-runtime marker.
                if (candidate.acceptMissingSpecMarker && missingRevision) return candidate;
                continue;
            }
            if (!candidate.id.equals(specMarker)) continue;

            // Existing P10/P11 repair prefixes have a spec marker but predate
            // the revision key. This opt-in remains attached to their old
            // entry if a newer candidate revision is registered later.
            if ((missingRevision && candidate.acceptMissingRevision)
                    || candidate.containerRevision.equals(containerRevision)) {
                return candidate;
            }
        }
        return null;
    }

    private static boolean isEmpty(String value) {
        return value == null || value.isEmpty();
    }

    /** Verifies version and digest without ever following a replacement symlink. */
    static boolean isCurrent(File binary, File marker, String requestedVersion) {
        return isCurrent(binary, marker, requestedVersion, null);
    }

    static boolean isCurrent(
            File binary,
            File marker,
            String requestedVersion,
            String requiredSha256) {
        if (binary == null || marker == null || requestedVersion == null) return false;
        try {
            byte[] markerBytes = readStableRegularFile(
                    marker.toPath(), MAX_MARKER_LENGTH);
            final String activeVersion;
            final byte[] recordedDigest = new byte[SHA256_LENGTH];
            try (DataInputStream input = new DataInputStream(
                    new ByteArrayInputStream(markerBytes))) {
                byte[] magic = new byte[MARKER_MAGIC.length];
                input.readFully(magic);
                if (!MessageDigest.isEqual(MARKER_MAGIC, magic)) return false;

                int versionLength = input.readUnsignedShort();
                if (versionLength == 0 || versionLength > MAX_VERSION_LENGTH) return false;
                byte[] versionBytes = new byte[versionLength];
                input.readFully(versionBytes);
                activeVersion = new String(versionBytes, StandardCharsets.UTF_8);
                input.readFully(recordedDigest);
                if (input.read() != -1) return false;
            }
            byte[] actualDigest = digestStableRegularFile(binary.toPath());
            byte[] requiredDigest = decodeOptionalSha256(requiredSha256);
            return requestedVersion.equals(activeVersion)
                    && MessageDigest.isEqual(recordedDigest, actualDigest)
                    && (requiredDigest == null
                            || MessageDigest.isEqual(requiredDigest, actualDigest));
        } catch (IOException | SecurityException ignored) {
            return false;
        }
    }

    static void invalidate(File marker) throws IOException {
        Files.deleteIfExists(marker.toPath());
    }

    /**
     * Copies from one no-follow source handle into one no-follow staging handle,
     * hashes the bytes while they travel, verifies that same staging handle,
     * chmods it, and only then atomically replaces the live path.
     */
    static void installBinaryAtomically(
            File source,
            File allowedSourceRoot,
            File destination,
            File allowedDestinationRoot) throws IOException {
        installBinaryAtomically(
                source, allowedSourceRoot, destination, allowedDestinationRoot, null);
    }

    static void installBinaryAtomically(
            File source,
            File allowedSourceRoot,
            File destination,
            File allowedDestinationRoot,
            String requiredSha256) throws IOException {
        TrustedLocation sourceLocation = requireContainedSource(source, allowedSourceRoot);
        TrustedLocation destinationLocation = requireContainedDestination(
                destination, allowedDestinationRoot);
        Path destinationPath = destinationLocation.path;
        Path destinationParent = destinationPath.getParent();

        Path temporary = Files.createTempFile(
                destinationParent, ".box64-install-", ".tmp");
        boolean moved = false;
        byte[] copiedDigest;
        BasicFileAttributes stagedIdentity;
        try {
            verifyTrustedLocation(destinationLocation,
                    "The Box64 destination parent was replaced during staging");
            BasicFileAttributes initialStage = requireRegularFile(
                    temporary, false, "Box64 staging file");
            copiedDigest = copyAndVerifySameHandles(
                    sourceLocation, temporary, initialStage);
            requireExpectedDigest(copiedDigest, requiredSha256,
                    "The staged Box64 binary does not match the pinned Steam runtime");

            BasicFileAttributes beforePermissions = requireRegularFile(
                    temporary, true, "Box64 staging file");
            requireSameFileIdentity(initialStage, beforePermissions,
                    "The Box64 staging path was replaced during copying");
            setExecutablePermissions(temporary.toFile());
            stagedIdentity = requireRegularFile(
                    temporary, true, "Box64 staging file");
            requireUnchanged(beforePermissions, stagedIdentity,
                    "The Box64 staging path was replaced while applying permissions");
            verifyTrustedLocation(destinationLocation,
                    "The Box64 destination parent was replaced before publication");

            // rename(2) replaces an existing symlink itself; it never follows
            // that symlink to overwrite its target.
            Files.move(
                    temporary,
                    destinationPath,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            moved = true;
        } finally {
            if (!moved) Files.deleteIfExists(temporary);
        }

        verifyTrustedLocation(destinationLocation,
                "The Box64 destination parent was replaced during publication");
        BasicFileAttributes installedIdentity = requireRegularFile(
                destinationPath, true, "installed Box64 binary");
        requireUnchanged(stagedIdentity, installedIdentity,
                "The installed Box64 path was replaced during publication");
        if (!MessageDigest.isEqual(copiedDigest,
                digestStableRegularFile(destinationPath))) {
            throw new IOException("The installed Box64 binary failed SHA-256 verification");
        }
    }

    /** Writes version plus a fresh binary digest, then atomically publishes it. */
    static void publish(File marker, String version, File binary) throws IOException {
        publish(marker, version, binary, null);
    }

    static void publish(
            File marker,
            String version,
            File binary,
            String requiredSha256) throws IOException {
        if (marker == null || version == null || binary == null) {
            throw new IOException("The Box64 marker inputs are unavailable");
        }
        byte[] versionBytes = version.getBytes(StandardCharsets.UTF_8);
        if (versionBytes.length == 0 || versionBytes.length > MAX_VERSION_LENGTH) {
            throw new IOException("Invalid Box64 version marker value");
        }
        byte[] digest = digestStableRegularFile(binary.toPath());
        requireExpectedDigest(digest, requiredSha256,
                "The installed Box64 binary does not match the pinned Steam runtime");

        byte[] contents;
        try (ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                DataOutputStream output = new DataOutputStream(bytes)) {
            output.write(MARKER_MAGIC);
            output.writeShort(versionBytes.length);
            output.write(versionBytes);
            output.write(digest);
            output.flush();
            contents = bytes.toByteArray();
        }

        File directory = marker.getParentFile();
        if (directory == null || (!directory.isDirectory()
                && !directory.mkdirs() && !directory.isDirectory())) {
            throw new IOException("Could not create Box64 marker directory: " + directory);
        }
        Path directoryPath = directory.toPath().toAbsolutePath().normalize();
        requireDirectoryNoFollow(directoryPath, "Box64 marker parent");
        Path markerPath = marker.toPath().toAbsolutePath().normalize();
        if (!directoryPath.equals(markerPath.getParent())) {
            throw new IOException("The Box64 marker escapes its private directory");
        }

        writeFileAtomicallyNoFollow(markerPath, contents, ".box64-version-");
        if (!isCurrent(binary, markerPath.toFile(), version, requiredSha256)) {
            throw new IOException("The shared Box64 version marker could not be verified");
        }
    }

    private static TrustedLocation requireContainedSource(File source, File allowedRoot)
            throws IOException {
        if (source == null || allowedRoot == null) {
            throw new IOException("The Box64 source or its trusted root is unavailable");
        }
        Path rootPath = allowedRoot.toPath().toAbsolutePath().normalize();
        BasicFileAttributes rootIdentity = requireDirectoryNoFollow(
                rootPath, "Box64 source root");
        Path sourcePath = source.toPath().toAbsolutePath().normalize();
        if (!sourcePath.startsWith(rootPath)) {
            throw new IOException("The Box64 source escapes its trusted root");
        }

        Path parentPath = sourcePath.getParent();
        BasicFileAttributes parentIdentity = requireDirectoryNoFollow(
                parentPath, "Box64 source parent");
        Path realRoot = rootPath.toRealPath();
        Path realParent = parentPath.toRealPath();
        if (!realParent.startsWith(realRoot)) {
            throw new IOException("The Box64 source resolves outside its trusted root");
        }
        requireRegularFile(sourcePath, true, "Box64 source");
        return new TrustedLocation(
                sourcePath, rootPath, parentPath, rootIdentity, parentIdentity);
    }

    private static TrustedLocation requireContainedDestination(
            File destination,
            File allowedRoot) throws IOException {
        if (destination == null || allowedRoot == null) {
            throw new IOException("The Box64 destination or its trusted root is unavailable");
        }
        Path rootPath = allowedRoot.toPath().toAbsolutePath().normalize();
        BasicFileAttributes rootIdentity = requireDirectoryNoFollow(
                rootPath, "Box64 destination root");
        Path destinationPath = destination.toPath().toAbsolutePath().normalize();
        if (!destinationPath.startsWith(rootPath)) {
            throw new IOException("The Box64 destination escapes its trusted root");
        }

        Path destinationParent = destinationPath.getParent();
        BasicFileAttributes parentIdentity = requireDirectoryNoFollow(
                destinationParent, "Box64 destination parent");
        Path realRoot = rootPath.toRealPath();
        Path realParent = destinationParent.toRealPath();
        if (!realParent.startsWith(realRoot)) {
            throw new IOException("The Box64 destination resolves outside its trusted root");
        }
        return new TrustedLocation(
                destinationPath,
                rootPath,
                destinationParent,
                rootIdentity,
                parentIdentity);
    }

    private static byte[] copyAndVerifySameHandles(
            TrustedLocation sourceLocation,
            Path staging,
            BasicFileAttributes initialStage) throws IOException {
        Path source = sourceLocation.path;
        verifyTrustedLocation(sourceLocation,
                "The Box64 source parent was replaced before opening it");
        BasicFileAttributes sourceBefore = requireRegularFile(
                source, true, "Box64 source");
        MessageDigest copied = newSha256();
        byte[] copiedDigest;

        try (FileChannel sourceChannel = openNoFollow(
                        source, StandardOpenOption.READ);
                FileChannel stagingChannel = openNoFollow(
                        staging,
                        StandardOpenOption.READ,
                        StandardOpenOption.WRITE,
                        StandardOpenOption.TRUNCATE_EXISTING)) {
            BasicFileAttributes sourceOpened = requireRegularFile(
                    source, true, "Box64 source");
            BasicFileAttributes stagingOpened = requireRegularFile(
                    staging, false, "Box64 staging file");
            requireUnchanged(sourceBefore, sourceOpened,
                    "The Box64 source path was replaced while opening it");
            requireUnchanged(initialStage, stagingOpened,
                    "The Box64 staging path was replaced while opening it");
            verifyTrustedLocation(sourceLocation,
                    "The Box64 source parent was replaced while opening it");
            if (sourceChannel.size() != sourceBefore.size()) {
                throw new IOException("The Box64 source changed while opening it");
            }

            ByteBuffer buffer = ByteBuffer.allocateDirect(COPY_BUFFER_SIZE);
            long copiedBytes = 0L;
            while (true) {
                int count = sourceChannel.read(buffer);
                if (count < 0) break;
                if (count == 0) continue;
                copiedBytes += count;
                buffer.flip();
                copied.update(buffer.asReadOnlyBuffer());
                while (buffer.hasRemaining()) stagingChannel.write(buffer);
                buffer.clear();
            }
            if (copiedBytes == 0L || copiedBytes != sourceBefore.size()) {
                throw new IOException("The Box64 source changed while it was copied");
            }
            stagingChannel.force(true);
            copiedDigest = copied.digest();

            stagingChannel.position(0L);
            if (!MessageDigest.isEqual(copiedDigest, digestChannel(stagingChannel))) {
                throw new IOException("The staged Box64 binary failed SHA-256 verification");
            }
            if (stagingChannel.size() != copiedBytes) {
                throw new IOException("The staged Box64 binary has the wrong length");
            }

            BasicFileAttributes sourceAfter = requireRegularFile(
                    source, true, "Box64 source");
            BasicFileAttributes stagingAfter = requireRegularFile(
                    staging, true, "Box64 staging file");
            requireUnchanged(sourceBefore, sourceAfter,
                    "The Box64 source path changed during copying");
            requireSameFileIdentity(initialStage, stagingAfter,
                    "The Box64 staging path changed during copying");
            verifyTrustedLocation(sourceLocation,
                    "The Box64 source parent changed during copying");
        }
        return copiedDigest;
    }

    private static byte[] digestStableRegularFile(Path path) throws IOException {
        BasicFileAttributes before = requireRegularFile(path, true, "Box64 file");
        byte[] digest;
        try (FileChannel channel = openNoFollow(path, StandardOpenOption.READ)) {
            BasicFileAttributes opened = requireRegularFile(path, true, "Box64 file");
            requireUnchanged(before, opened, "The Box64 file was replaced while opening it");
            if (channel.size() != before.size()) {
                throw new IOException("The Box64 file changed while opening it");
            }
            digest = digestChannel(channel);
            if (channel.size() != before.size()) {
                throw new IOException("The Box64 file changed while hashing it");
            }
        }
        BasicFileAttributes after = requireRegularFile(path, true, "Box64 file");
        requireUnchanged(before, after, "The Box64 file was replaced while hashing it");
        return digest;
    }

    private static byte[] readStableRegularFile(Path path, int maximumLength)
            throws IOException {
        BasicFileAttributes before = requireRegularFile(path, true, "Box64 marker");
        if (before.size() > maximumLength || before.size() > Integer.MAX_VALUE) {
            throw new IOException("The Box64 marker is too large");
        }
        byte[] contents = new byte[(int) before.size()];
        try (FileChannel channel = openNoFollow(path, StandardOpenOption.READ)) {
            BasicFileAttributes opened = requireRegularFile(path, true, "Box64 marker");
            requireUnchanged(before, opened,
                    "The Box64 marker was replaced while opening it");
            ByteBuffer target = ByteBuffer.wrap(contents);
            while (target.hasRemaining()) {
                if (channel.read(target) < 0) {
                    throw new IOException("The Box64 marker was truncated while reading it");
                }
            }
            if (channel.read(ByteBuffer.allocate(1)) != -1) {
                throw new IOException("The Box64 marker grew while reading it");
            }
        }
        BasicFileAttributes after = requireRegularFile(path, true, "Box64 marker");
        requireUnchanged(before, after, "The Box64 marker changed while reading it");
        return contents;
    }

    private static void writeFileAtomicallyNoFollow(
            Path destination,
            byte[] contents,
            String temporaryPrefix) throws IOException {
        Path parent = destination.getParent();
        BasicFileAttributes parentIdentity = requireDirectoryNoFollow(
                parent, "Box64 marker parent");
        Path temporary = Files.createTempFile(parent, temporaryPrefix, ".tmp");
        boolean moved = false;
        BasicFileAttributes initial = requireRegularFile(
                temporary, false, "Box64 marker staging file");
        try {
            requireSameFileIdentity(
                    parentIdentity,
                    requireDirectoryNoFollow(parent, "Box64 marker parent"),
                    "The Box64 marker parent was replaced during staging");
            try (FileChannel channel = openNoFollow(
                    temporary,
                    StandardOpenOption.WRITE,
                    StandardOpenOption.TRUNCATE_EXISTING)) {
                BasicFileAttributes opened = requireRegularFile(
                        temporary, false, "Box64 marker staging file");
                requireUnchanged(initial, opened,
                        "The Box64 marker staging path was replaced while opening it");
                ByteBuffer source = ByteBuffer.wrap(contents);
                while (source.hasRemaining()) channel.write(source);
                channel.force(true);
            }
            BasicFileAttributes written = requireRegularFile(
                    temporary, true, "Box64 marker staging file");
            requireSameFileIdentity(initial, written,
                    "The Box64 marker staging path changed while writing it");
            if (written.size() != contents.length) {
                throw new IOException("The Box64 marker staging file has the wrong length");
            }
            requireSameFileIdentity(
                    parentIdentity,
                    requireDirectoryNoFollow(parent, "Box64 marker parent"),
                    "The Box64 marker parent was replaced before publication");
            Files.move(
                    temporary,
                    destination,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            moved = true;

            requireSameFileIdentity(
                    parentIdentity,
                    requireDirectoryNoFollow(parent, "Box64 marker parent"),
                    "The Box64 marker parent was replaced during publication");
            BasicFileAttributes installed = requireRegularFile(
                    destination, true, "Box64 marker");
            requireUnchanged(written, installed,
                    "The Box64 marker was replaced during publication");
        } finally {
            if (!moved) Files.deleteIfExists(temporary);
        }
    }

    private static FileChannel openNoFollow(Path path, OpenOption... options)
            throws IOException {
        OpenOption[] noFollowOptions = new OpenOption[options.length + 1];
        System.arraycopy(options, 0, noFollowOptions, 0, options.length);
        noFollowOptions[options.length] = LinkOption.NOFOLLOW_LINKS;
        return FileChannel.open(path, noFollowOptions);
    }

    private static BasicFileAttributes requireRegularFile(
            Path path,
            boolean requireNonEmpty,
            String description) throws IOException {
        BasicFileAttributes attributes = Files.readAttributes(
                path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        if (!attributes.isRegularFile()
                || (requireNonEmpty && attributes.size() == 0L)) {
            throw new IOException(description + " is not a regular non-empty file: " + path);
        }
        return attributes;
    }

    private static BasicFileAttributes requireDirectoryNoFollow(
            Path path,
            String description)
            throws IOException {
        if (path == null) throw new IOException(description + " is unavailable");
        BasicFileAttributes attributes = Files.readAttributes(
                path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        if (!attributes.isDirectory()) {
            throw new IOException(description + " is not a real directory: " + path);
        }
        return attributes;
    }

    private static void verifyTrustedLocation(
            TrustedLocation location,
            String message) throws IOException {
        BasicFileAttributes rootNow = requireDirectoryNoFollow(
                location.root, "trusted Box64 root");
        BasicFileAttributes parentNow = requireDirectoryNoFollow(
                location.parent, "trusted Box64 parent");
        requireSameFileIdentity(location.rootIdentity, rootNow, message);
        requireSameFileIdentity(location.parentIdentity, parentNow, message);
        if (!location.parent.toRealPath().startsWith(location.root.toRealPath())) {
            throw new IOException(message);
        }
    }

    private static void requireSameFileIdentity(
            BasicFileAttributes expected,
            BasicFileAttributes actual,
            String message) throws IOException {
        Object expectedKey = expected.fileKey();
        Object actualKey = actual.fileKey();
        boolean sameKey = expectedKey == null || actualKey == null
                ? expected.creationTime().equals(actual.creationTime())
                : expectedKey.equals(actualKey);
        if (!sameKey) {
            throw new IOException(message);
        }
    }

    private static void requireUnchanged(
            BasicFileAttributes expected,
            BasicFileAttributes actual,
            String message) throws IOException {
        requireSameFileIdentity(expected, actual, message);
        if (expected.size() != actual.size()
                || !expected.lastModifiedTime().equals(actual.lastModifiedTime())) {
            throw new IOException(message);
        }
    }

    private static byte[] digestChannel(FileChannel channel) throws IOException {
        MessageDigest digest = newSha256();
        ByteBuffer buffer = ByteBuffer.allocateDirect(COPY_BUFFER_SIZE);
        int count;
        while ((count = channel.read(buffer)) >= 0) {
            if (count == 0) continue;
            buffer.flip();
            digest.update(buffer);
            buffer.clear();
        }
        return digest.digest();
    }

    private static MessageDigest newSha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError("SHA-256 is unavailable", impossible);
        }
    }

    private static void requireExpectedDigest(
            byte[] actual,
            String requiredSha256,
            String message) throws IOException {
        byte[] required = decodeOptionalSha256(requiredSha256);
        if (required != null && !MessageDigest.isEqual(required, actual)) {
            throw new IOException(message);
        }
    }

    private static byte[] decodeOptionalSha256(String value) throws IOException {
        if (value == null) return null;
        if (value.length() != SHA256_LENGTH * 2) {
            throw new IOException("The required Box64 SHA-256 is malformed");
        }
        byte[] decoded = new byte[SHA256_LENGTH];
        for (int index = 0; index < decoded.length; index++) {
            int high = Character.digit(value.charAt(index * 2), 16);
            int low = Character.digit(value.charAt(index * 2 + 1), 16);
            if (high < 0 || low < 0) {
                throw new IOException("The required Box64 SHA-256 is malformed");
            }
            decoded[index] = (byte) ((high << 4) | low);
        }
        return decoded;
    }

    private static void setExecutablePermissions(File file) throws IOException {
        try {
            Files.setPosixFilePermissions(
                    file.toPath(), PosixFilePermissions.fromString("rwxr-xr-x"));
            return;
        } catch (UnsupportedOperationException ignored) {
            // The local JVM tests run on Windows. Android takes the POSIX path.
        }
        boolean readable = file.setReadable(true, false) || file.canRead();
        boolean writable = file.setWritable(true, true) || file.canWrite();
        boolean executable = file.setExecutable(true, false) || file.canExecute();
        if (!readable || !writable || !executable) {
            throw new IOException("Could not make the staged Box64 binary executable");
        }
    }

    private static final class TrustedLocation {
        final Path path;
        final Path root;
        final Path parent;
        final BasicFileAttributes rootIdentity;
        final BasicFileAttributes parentIdentity;

        TrustedLocation(
                Path path,
                Path root,
                Path parent,
                BasicFileAttributes rootIdentity,
                BasicFileAttributes parentIdentity) {
            this.path = path;
            this.root = root;
            this.parent = parent;
            this.rootIdentity = rootIdentity;
            this.parentIdentity = parentIdentity;
        }
    }

    /** One explicitly allowlisted managed-container/Box64 pairing. */
    private static final class ManagedRuntimeSpec {
        final String id;
        final String wineVersion;
        final String containerRevision;
        final String box64Version;
        final String requiredSha256;
        final boolean acceptMissingRevision;
        final boolean acceptMissingSpecMarker;
        final String markerId;

        ManagedRuntimeSpec(
                String id,
                String wineVersion,
                String containerRevision,
                String box64Version,
                String requiredSha256,
                boolean acceptMissingRevision,
                boolean acceptMissingSpecMarker) {
            this.id = id;
            this.wineVersion = wineVersion;
            this.containerRevision = containerRevision;
            this.box64Version = box64Version;
            this.requiredSha256 = requiredSha256;
            this.acceptMissingRevision = acceptMissingRevision;
            this.acceptMissingSpecMarker = acceptMissingSpecMarker;
            this.markerId = "wemu-official-steam-" + id + "-r" + containerRevision
                    + "-box64-" + box64Version;
        }
    }

    static final class RuntimeRequest {
        final String version;
        final String markerId;
        final String requiredSha256;
        final boolean allowInstalledProfile;

        RuntimeRequest(
                String version,
                String markerId,
                String requiredSha256,
                boolean allowInstalledProfile) {
            this.version = version;
            this.markerId = markerId;
            this.requiredSha256 = requiredSha256;
            this.allowInstalledProfile = allowInstalledProfile;
        }
    }
}
