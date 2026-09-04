package com.wemu.engine.xenvironment.components;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

import org.junit.Assume;
import org.junit.Test;

public class SharedBox64RuntimeStateTest {
    @Test
    public void managedSteamRequestPinsBundledBox64AndDisallowsInstalledProfile() {
        SharedBox64RuntimeState.RuntimeRequest request =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton9-x64", "3", "Proton-9.0-x86_64-0",
                        "0.4.4-b38a33c-qsort-save-mem");

        assertEquals("0.4.4-b38a33c-qsort-save-mem", request.version);
        assertEquals(
                "3DD5EC0B36DADF531F4C6616885F395FC443B6081F62C688F4BE084C6D2B48C7",
                request.requiredSha256);
        assertEquals(
                "wemu-official-steam-proton9-x64-r3-box64-0.4.4-b38a33c-qsort-save-mem",
                request.markerId);
        assertFalse(request.allowInstalledProfile);
        assertFalse(request.markerId.equals(request.version));
    }

    @Test
    public void ordinaryRequestKeepsSelectedProfileBehavior() {
        SharedBox64RuntimeState.RuntimeRequest request =
                SharedBox64RuntimeState.runtimeRequest(
                        "", "ignored", "ignored", "ignored", "Box64-0.4.4-1");

        assertEquals("Box64-0.4.4-1", request.version);
        assertEquals(request.version, request.markerId);
        assertNull(request.requiredSha256);
        assertTrue(request.allowInstalledProfile);
        assertFalse(request.managedSteamRuntime);
        assertFalse(request.forceBox64Translator);
    }

    @Test
    public void transformersRequestPinsExactWemuBox64ButMatchesArm64EcFexExecution() {
        SharedBox64RuntimeState.RuntimeRequest request =
                SharedBox64RuntimeState.runtimeRequest(
                        "", "", "", "Proton-11.0-5-arm64ec-6", "0.4.2",
                        SharedBox64RuntimeState.TRANSFORMERS_WFC_RUNTIME);

        assertEquals("0.4.2", request.version);
        assertEquals(SharedBox64RuntimeState.BOX64_0_4_2_SHA256,
                request.requiredSha256);
        assertEquals(SharedBox64RuntimeState.WOWBOX64_0_4_2_SHA256,
                request.requiredWowBox64Sha256);
        assertFalse(request.allowInstalledProfile);
        assertFalse(request.managedSteamRuntime);
        assertFalse(request.forceBox64Translator);
        assertEquals("fexcore", SharedBox64RuntimeState.resolveEmulator(
                request, "box64", null, true));
        assertEquals("fexcore", SharedBox64RuntimeState.resolveEmulator(
                request, "box64", "FEXCore", true));
        assertEquals("box64", SharedBox64RuntimeState.resolveEmulator(
                request, "box64", null, false));
        assertPinnedGameRequestRejected("0.4.4",
                SharedBox64RuntimeState.TRANSFORMERS_WFC_RUNTIME);
        assertPinnedGameRequestRejected("0.4.2", "unknown-game-runtime");
    }

    @Test
    public void managedSteamSpecsResolveOnlyTheirExplicitCurrentTuple() {
        SharedBox64RuntimeState.RuntimeRequest proton10 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton10-x64", "3", "Proton-10.0-4-x86_64-2",
                        "0.4.4-b38a33c-qsort-save-mem");
        SharedBox64RuntimeState.RuntimeRequest proton11 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton11-x64", "6", "Proton-11.0-1-x86_64-1",
                        "0.4.4-b38a33c-qsort-save-mem");

        assertEquals("0.4.4-b38a33c-qsort-save-mem", proton10.version);
        assertEquals("0.4.4-b38a33c-qsort-save-mem", proton11.version);
        assertEquals(SharedBox64RuntimeState.BOX64_B38_QSORT_SAVE_MEM_SHA256,
                proton10.requiredSha256);
        assertEquals(SharedBox64RuntimeState.BOX64_B38_QSORT_SAVE_MEM_SHA256,
                proton11.requiredSha256);
        assertEquals("wemu-official-steam-proton11-x64-r6-box64-0.4.4-b38a33c-qsort-save-mem",
                proton11.markerId);
        assertFalse(proton10.markerId.equals(proton11.markerId));
        assertFalse(proton10.allowInstalledProfile);
        assertFalse(proton11.allowInstalledProfile);
        assertTrue(proton10.managedSteamRuntime);
        assertTrue(proton11.managedSteamRuntime);
        assertFalse(proton10.forceBox64Translator);
    }

    @Test
    public void exactHistoricalAndTransitionalTuplesRemainLaunchable() {
        SharedBox64RuntimeState.RuntimeRequest proton9 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton9-x64", "1", "Proton-9.0-x86_64-0", "0.4.2");
        SharedBox64RuntimeState.RuntimeRequest proton10Repair =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton10-x64", "", "Proton-10.0-4-x86_64-2", "0.4.2");
        SharedBox64RuntimeState.RuntimeRequest proton11 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton11-x64", "1", "Proton-11.0-1-x86_64-1", "0.4.2");
        SharedBox64RuntimeState.RuntimeRequest transitionalProton11 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton11-x64", "2", "Proton-11.0-1-x86_64-1", "0.4.4");
        SharedBox64RuntimeState.RuntimeRequest previousProton9 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton9-x64", "2", "Proton-9.0-x86_64-0", "0.4.4");
        SharedBox64RuntimeState.RuntimeRequest previousProton10 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton10-x64", "2", "Proton-10.0-4-x86_64-2", "0.4.4");
        SharedBox64RuntimeState.RuntimeRequest previousProton11 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton11-x64", "3", "Proton-11.0-1-x86_64-1", "0.4.2");
        SharedBox64RuntimeState.RuntimeRequest saveMemProton11 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton11-x64", "4", "Proton-11.0-1-x86_64-1",
                        "0.4.2-save-mem");
        SharedBox64RuntimeState.RuntimeRequest b38Proton11 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "proton11-x64", "5", "Proton-11.0-1-x86_64-1",
                        "0.4.4-b38a33c-save-mem");
        SharedBox64RuntimeState.RuntimeRequest legacyProton9 =
                SharedBox64RuntimeState.runtimeRequest(
                        "1", "", "", "Proton-9.0-x86_64-0", "0.4.2");

        assertEquals(SharedBox64RuntimeState.BOX64_0_4_2_SHA256,
                proton9.requiredSha256);
        assertEquals("0.4.2", proton10Repair.version);
        assertEquals("wemu-official-steam-proton10-x64-r1-box64-0.4.2",
                proton10Repair.markerId);
        assertEquals(SharedBox64RuntimeState.BOX64_0_4_2_SHA256,
                proton11.requiredSha256);
        assertEquals("wemu-official-steam-proton11-x64-r2-box64-0.4.4",
                transitionalProton11.markerId);
        assertEquals(SharedBox64RuntimeState.BOX64_0_4_4_SHA256,
                transitionalProton11.requiredSha256);
        assertEquals("wemu-official-steam-proton9-x64-r2-box64-0.4.4",
                previousProton9.markerId);
        assertEquals(SharedBox64RuntimeState.BOX64_0_4_4_SHA256,
                previousProton9.requiredSha256);
        assertEquals("wemu-official-steam-proton10-x64-r2-box64-0.4.4",
                previousProton10.markerId);
        assertEquals(SharedBox64RuntimeState.BOX64_0_4_4_SHA256,
                previousProton10.requiredSha256);
        assertEquals("wemu-official-steam-proton11-x64-r3-box64-0.4.2",
                previousProton11.markerId);
        assertEquals(SharedBox64RuntimeState.BOX64_0_4_2_SHA256,
                previousProton11.requiredSha256);
        assertEquals("wemu-official-steam-proton11-x64-r4-box64-0.4.2-save-mem",
                saveMemProton11.markerId);
        assertEquals(SharedBox64RuntimeState.BOX64_0_4_2_SAVE_MEM_SHA256,
                saveMemProton11.requiredSha256);
        assertEquals(SharedBox64RuntimeState.BOX64_B38_SAVE_MEM_SHA256,
                b38Proton11.requiredSha256);
        assertEquals("wemu-official-steam-proton11-x64-r5-box64-0.4.4-b38a33c-save-mem",
                b38Proton11.markerId);
        assertEquals("0.4.2", legacyProton9.version);
        assertManagedRequestRejected("", "", "Proton-10.0-4-x86_64-2", "0.4.2");
    }

    @Test
    public void managedSteamHybridAndUnknownTuplesAreRejected() {
        assertManagedRequestRejected("proton9-x64", "2",
                "Proton-9.0-x86_64-0", "0.4.2");
        assertManagedRequestRejected("proton9-x64", "1",
                "Proton-9.0-x86_64-0", "0.4.4");
        assertManagedRequestRejected("proton10-x64", "2",
                "Proton-10.0-4-x86_64-2", "0.4.2");
        assertManagedRequestRejected("proton10-x64", "1",
                "Proton-10.0-4-x86_64-2", "0.4.4");
        assertManagedRequestRejected("proton11-x64", "",
                "Proton-11.0-1-x86_64-1", "0.4.4");
        assertManagedRequestRejected("proton11-x64", "2",
                "Proton-11.0-1-x86_64-1", "0.4.2");
        assertManagedRequestRejected("proton11-x64", "3",
                "Proton-11.0-1-x86_64-1", "0.4.4");
        assertManagedRequestRejected("proton11-x64", "4",
                "Proton-11.0-1-x86_64-1", "0.4.2");
        assertManagedRequestRejected("proton11-x64", "3",
                "Proton-11.0-1-x86_64-1", "0.4.2-save-mem");
        assertManagedRequestRejected("proton11-x64", "4",
                "Proton-11.0-1-x86_64-1", "0.4.4-b38a33c-save-mem");
        assertManagedRequestRejected("proton11-x64", "5",
                "Proton-11.0-1-x86_64-1", "0.4.2-save-mem");
        assertManagedRequestRejected("proton11-x64", "5",
                "Proton-11.0-1-x86_64-1", "0.4.4-b38a33c-qsort-save-mem");
        assertManagedRequestRejected("", "",
                "Proton-9.0-x86_64-0", "0.4.4");
        assertManagedRequestRejected("proton-future", "1",
                "Proton-future-1", "0.4.2");
        assertManagedRequestRejected("proton10-x64", "1",
                "Proton-9.0-x86_64-0", "0.4.2");
    }

    @Test
    public void markerVersionAndBinaryDigestMustAllMatch() throws IOException {
        File directory = Files.createTempDirectory("shared-box64").toFile();
        try {
            File binary = new File(directory, "imagefs/usr/bin/box64");
            assertTrue(binary.getParentFile().mkdirs());
            Files.write(binary.toPath(), new byte[]{1, 2, 3});
            File marker = SharedBox64RuntimeState.markerFile(
                    new File(directory, "private/runtime/box64"));

            assertFalse(SharedBox64RuntimeState.isCurrent(binary, marker, "0.4.4"));
            SharedBox64RuntimeState.publish(marker, "0.4.4", binary);
            assertTrue(SharedBox64RuntimeState.isCurrent(binary, marker, "0.4.4"));
            assertFalse(SharedBox64RuntimeState.isCurrent(binary, marker, "0.4.5"));

            Files.write(binary.toPath(), new byte[]{1, 2, 4});
            assertFalse(SharedBox64RuntimeState.isCurrent(binary, marker, "0.4.4"));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void forgedMarkerBesideGuestVisibleBinaryIsIgnored() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-forgery").toFile();
        try {
            File binary = new File(directory, "imagefs/usr/bin/box64");
            assertTrue(binary.getParentFile().mkdirs());
            Files.write(binary.toPath(), new byte[]{7, 8, 9});

            File forgedGuestMarker = new File(
                    binary.getParentFile(), SharedBox64RuntimeState.MARKER_NAME);
            SharedBox64RuntimeState.publish(forgedGuestMarker, "0.4.4", binary);

            File trustedMarker = SharedBox64RuntimeState.markerFile(
                    new File(directory, "private/runtime/box64"));
            assertFalse(SharedBox64RuntimeState.isCurrent(binary, trustedMarker, "0.4.4"));

            SharedBox64RuntimeState.publish(trustedMarker, "0.4.4", binary);
            assertTrue(SharedBox64RuntimeState.isCurrent(binary, trustedMarker, "0.4.4"));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void binaryInstallStagesVerifiesAndAtomicallyReplacesLivePath() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-install").toFile();
        try {
            File source = new File(directory, "private/source-box64");
            byte[] expected = new byte[]{10, 11, 12, 13};
            assertTrue(source.getParentFile().mkdirs());
            Files.write(source.toPath(), expected);

            File destination = new File(directory, "imagefs/usr/bin/box64");
            assertTrue(destination.getParentFile().mkdirs());
            Files.write(destination.toPath(), new byte[]{99});

            SharedBox64RuntimeState.installBinaryAtomically(
                    source,
                    source.getParentFile(),
                    destination,
                    new File(directory, "imagefs"));

            assertArrayEquals(expected, Files.readAllBytes(destination.toPath()));
            assertTrue(destination.canExecute());
            File[] temporary = destination.getParentFile().listFiles(
                    file -> file.getName().endsWith(".tmp"));
            assertTrue(temporary != null && temporary.length == 0);
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void requiredDigestIsEnforcedByInstallPublishAndCurrent() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-required-digest").toFile();
        try {
            File sourceRoot = new File(directory, "private");
            assertTrue(sourceRoot.mkdirs());
            byte[] expectedBytes = new byte[]{5, 4, 3, 2, 1};
            File source = new File(sourceRoot, "box64");
            Files.write(source.toPath(), expectedBytes);
            String expectedSha256 = sha256Hex(expectedBytes);

            File destinationRoot = new File(directory, "imagefs");
            File destination = new File(destinationRoot, "usr/bin/box64");
            assertTrue(destination.getParentFile().mkdirs());
            SharedBox64RuntimeState.installBinaryAtomically(
                    source,
                    sourceRoot,
                    destination,
                    destinationRoot,
                    expectedSha256);

            File marker = SharedBox64RuntimeState.markerFile(
                    new File(directory, "marker"));
            SharedBox64RuntimeState.publish(
                    marker, "managed-steam", destination, expectedSha256);
            assertTrue(SharedBox64RuntimeState.isCurrent(
                    destination, marker, "managed-steam", expectedSha256));
            assertFalse(SharedBox64RuntimeState.isCurrent(
                    destination, marker, "managed-steam", "00".repeat(32)));
            assertFalse(SharedBox64RuntimeState.isCurrent(
                    destination, marker, "0.4.2", expectedSha256));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void requiredDigestMismatchNeverReplacesLiveBinary() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-wrong-digest").toFile();
        try {
            File sourceRoot = new File(directory, "private");
            assertTrue(sourceRoot.mkdirs());
            File source = new File(sourceRoot, "box64");
            Files.write(source.toPath(), new byte[]{1, 2, 3});

            File destinationRoot = new File(directory, "imagefs");
            File destination = new File(destinationRoot, "usr/bin/box64");
            assertTrue(destination.getParentFile().mkdirs());
            byte[] original = new byte[]{9, 8, 7};
            Files.write(destination.toPath(), original);

            boolean failed = false;
            try {
                SharedBox64RuntimeState.installBinaryAtomically(
                        source,
                        sourceRoot,
                        destination,
                        destinationRoot,
                        "00".repeat(32));
            } catch (IOException expected) {
                failed = true;
            }

            assertTrue(failed);
            assertArrayEquals(original, Files.readAllBytes(destination.toPath()));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void failedStagingLeavesExistingLiveBinaryUntouched() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-failed-install").toFile();
        try {
            File emptySource = new File(directory, "empty-source");
            Files.write(emptySource.toPath(), new byte[0]);
            File destination = new File(directory, "imagefs/usr/bin/box64");
            assertTrue(destination.getParentFile().mkdirs());
            byte[] original = new byte[]{42, 43};
            Files.write(destination.toPath(), original);

            boolean failed = false;
            try {
                SharedBox64RuntimeState.installBinaryAtomically(
                        emptySource,
                        directory,
                        destination,
                        new File(directory, "imagefs"));
            } catch (IOException expected) {
                failed = true;
            }

            assertTrue(failed);
            assertArrayEquals(original, Files.readAllBytes(destination.toPath()));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void sourceOutsideTrustedRootIsRejectedWithoutTouchingLiveBinary()
            throws IOException {
        File directory = Files.createTempDirectory("shared-box64-containment").toFile();
        try {
            File trustedSourceRoot = new File(directory, "trusted");
            assertTrue(trustedSourceRoot.mkdirs());
            File outsideSource = new File(directory, "outside-box64");
            Files.write(outsideSource.toPath(), new byte[]{1, 2, 3});

            File destinationRoot = new File(directory, "imagefs");
            File destination = new File(destinationRoot, "usr/bin/box64");
            assertTrue(destination.getParentFile().mkdirs());
            byte[] original = new byte[]{9, 9};
            Files.write(destination.toPath(), original);

            boolean failed = false;
            try {
                SharedBox64RuntimeState.installBinaryAtomically(
                        outsideSource, trustedSourceRoot, destination, destinationRoot);
            } catch (IOException expected) {
                failed = true;
            }

            assertTrue(failed);
            assertArrayEquals(original, Files.readAllBytes(destination.toPath()));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void binaryAndMarkerSymlinksAreNeverAcceptedAsCurrent() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-symlink-current").toFile();
        try {
            File realBinary = new File(directory, "real-box64");
            Files.write(realBinary.toPath(), new byte[]{4, 5, 6});
            File realMarker = SharedBox64RuntimeState.markerFile(
                    new File(directory, "private"));
            SharedBox64RuntimeState.publish(realMarker, "0.4.4", realBinary);

            Path binaryLink = new File(directory, "box64-link").toPath();
            createSymlinkOrSkip(binaryLink, realBinary.toPath());
            assertFalse(SharedBox64RuntimeState.isCurrent(
                    binaryLink.toFile(), realMarker, "0.4.4"));

            Path markerLink = new File(directory, "marker-link").toPath();
            createSymlinkOrSkip(markerLink, realMarker.toPath());
            assertFalse(SharedBox64RuntimeState.isCurrent(
                    realBinary, markerLink.toFile(), "0.4.4"));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void symlinkSourceIsRejectedAndDestinationSymlinkIsReplacedNotFollowed()
            throws IOException {
        File directory = Files.createTempDirectory("shared-box64-symlink-install").toFile();
        try {
            File sourceRoot = new File(directory, "private");
            assertTrue(sourceRoot.mkdirs());
            File realSource = new File(sourceRoot, "real-box64");
            byte[] replacement = new byte[]{10, 20, 30};
            Files.write(realSource.toPath(), replacement);
            Path sourceLink = new File(sourceRoot, "source-link").toPath();
            createSymlinkOrSkip(sourceLink, realSource.toPath());

            File destinationRoot = new File(directory, "imagefs");
            File destination = new File(destinationRoot, "usr/bin/box64");
            assertTrue(destination.getParentFile().mkdirs());
            byte[] original = new byte[]{77};
            Files.write(destination.toPath(), original);

            boolean sourceRejected = false;
            try {
                SharedBox64RuntimeState.installBinaryAtomically(
                        sourceLink.toFile(), sourceRoot, destination, destinationRoot);
            } catch (IOException expected) {
                sourceRejected = true;
            }
            assertTrue(sourceRejected);
            assertArrayEquals(original, Files.readAllBytes(destination.toPath()));

            File outsideTarget = new File(directory, "outside-target");
            byte[] outsideContents = new byte[]{88};
            Files.write(outsideTarget.toPath(), outsideContents);
            assertTrue(destination.delete());
            createSymlinkOrSkip(destination.toPath(), outsideTarget.toPath());

            SharedBox64RuntimeState.installBinaryAtomically(
                    realSource, sourceRoot, destination, destinationRoot);

            assertTrue(Files.isRegularFile(
                    destination.toPath(), LinkOption.NOFOLLOW_LINKS));
            assertArrayEquals(replacement, Files.readAllBytes(destination.toPath()));
            assertArrayEquals(outsideContents, Files.readAllBytes(outsideTarget.toPath()));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void destinationParentSymlinkCannotEscapeTrustedImageRoot() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-dest-parent").toFile();
        try {
            File sourceRoot = new File(directory, "private");
            assertTrue(sourceRoot.mkdirs());
            File source = new File(sourceRoot, "box64");
            Files.write(source.toPath(), new byte[]{1, 2, 3});

            File destinationRoot = new File(directory, "imagefs");
            assertTrue(destinationRoot.mkdirs());
            File outsideUsr = new File(directory, "outside-usr");
            File outsideBin = new File(outsideUsr, "bin");
            assertTrue(outsideBin.mkdirs());
            File outsideLive = new File(outsideBin, "box64");
            byte[] outsideContents = new byte[]{99};
            Files.write(outsideLive.toPath(), outsideContents);
            createSymlinkOrSkip(
                    new File(destinationRoot, "usr").toPath(), outsideUsr.toPath());

            boolean failed = false;
            try {
                SharedBox64RuntimeState.installBinaryAtomically(
                        source,
                        sourceRoot,
                        new File(destinationRoot, "usr/bin/box64"),
                        destinationRoot);
            } catch (IOException expected) {
                failed = true;
            }

            assertTrue(failed);
            assertArrayEquals(outsideContents, Files.readAllBytes(outsideLive.toPath()));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void invalidationPreventsAnOldSharedBinaryFromBeingReused() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-invalidate").toFile();
        try {
            File binary = new File(directory, "box64");
            File marker = SharedBox64RuntimeState.markerFile(
                    new File(directory, "private"));
            Files.write(binary.toPath(), new byte[]{1});
            SharedBox64RuntimeState.publish(marker, "old", binary);

            SharedBox64RuntimeState.invalidate(marker);

            assertFalse(marker.exists());
            assertFalse(SharedBox64RuntimeState.isCurrent(binary, marker, "old"));
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void publishingReplacesMarkerWithoutLeavingTemporaryFiles() throws IOException {
        File directory = Files.createTempDirectory("shared-box64-publish").toFile();
        try {
            File binary = new File(directory, "box64");
            Files.write(binary.toPath(), new byte[]{1});
            File marker = SharedBox64RuntimeState.markerFile(
                    new File(directory, "private"));
            SharedBox64RuntimeState.publish(marker, "old", binary);
            SharedBox64RuntimeState.publish(marker, "new", binary);

            assertTrue(SharedBox64RuntimeState.isCurrent(binary, marker, "new"));
            assertFalse(SharedBox64RuntimeState.isCurrent(binary, marker, "old"));
            File[] temporary = marker.getParentFile().listFiles(
                    file -> file.getName().endsWith(".tmp"));
            assertTrue(temporary != null && temporary.length == 0);
        } finally {
            deleteRecursively(directory);
        }
    }

    private static void assertManagedRequestRejected(
            String specMarker,
            String revision,
            String wineVersion,
            String box64Version) {
        boolean rejected = false;
        try {
            SharedBox64RuntimeState.runtimeRequest(
                    "1", specMarker, revision, wineVersion, box64Version);
        } catch (IllegalStateException expected) {
            rejected = true;
        }
        assertTrue(rejected);
    }

    private static void assertPinnedGameRequestRejected(
            String box64Version,
            String pinnedRuntime) {
        boolean rejected = false;
        try {
            SharedBox64RuntimeState.runtimeRequest(
                    "", "", "", "Proton-11.0-5-arm64ec-6",
                    box64Version, pinnedRuntime);
        } catch (IllegalStateException expected) {
            rejected = true;
        }
        assertTrue(rejected);
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (!file.delete()) file.deleteOnExit();
    }

    private static void createSymlinkOrSkip(Path link, Path target) {
        try {
            Files.createSymbolicLink(link, target.toAbsolutePath());
        } catch (IOException | UnsupportedOperationException | SecurityException unavailable) {
            Assume.assumeNoException("Symbolic links are unavailable", unavailable);
        }
    }

    private static String sha256Hex(byte[] contents) {
        final byte[] digest;
        try {
            digest = MessageDigest.getInstance("SHA-256").digest(contents);
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError(impossible);
        }
        StringBuilder value = new StringBuilder(digest.length * 2);
        for (byte item : digest) value.append(String.format("%02X", item & 0xff));
        return value.toString();
    }
}
