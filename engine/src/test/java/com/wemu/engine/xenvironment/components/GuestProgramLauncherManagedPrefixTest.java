package com.wemu.engine.xenvironment.components;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import org.junit.Test;

public class GuestProgramLauncherManagedPrefixTest {
    @Test
    public void acceptsOnlyTheExactContainerWinePrefix() throws Exception {
        File containerRoot = Files.createTempDirectory("managed-prefix-container").toFile();
        try {
            File prefix = new File(containerRoot, ".wine");
            Files.createDirectory(prefix.toPath());

            assertEquals(
                    prefix.getCanonicalFile(),
                    GuestProgramLauncherComponent.requireExactManagedWinePrefix(
                            prefix.getAbsolutePath(), containerRoot));
        } finally {
            deleteRecursively(containerRoot);
        }
    }

    @Test
    public void refusesAValidSiblingPrefix() throws Exception {
        File containerRoot = Files.createTempDirectory("managed-prefix-container").toFile();
        File otherRoot = Files.createTempDirectory("managed-prefix-other").toFile();
        try {
            Files.createDirectory(new File(containerRoot, ".wine").toPath());
            File otherPrefix = new File(otherRoot, ".wine");
            Files.createDirectory(otherPrefix.toPath());

            assertThrows(
                    IOException.class,
                    () -> GuestProgramLauncherComponent.requireExactManagedWinePrefix(
                            otherPrefix.getAbsolutePath(), containerRoot));
        } finally {
            deleteRecursively(containerRoot);
            deleteRecursively(otherRoot);
        }
    }

    @Test
    public void refusesMissingPrefixIdentity() throws Exception {
        File containerRoot = Files.createTempDirectory("managed-prefix-missing").toFile();
        try {
            Files.createDirectory(new File(containerRoot, ".wine").toPath());
            assertThrows(
                    IOException.class,
                    () -> GuestProgramLauncherComponent.requireExactManagedWinePrefix(
                            "", containerRoot));
        } finally {
            deleteRecursively(containerRoot);
        }
    }

    @Test
    public void processScanMatchesOnlyTheCompletePrefixEnvironmentEntry() throws Exception {
        File procRoot = Files.createTempDirectory("managed-prefix-proc").toFile();
        try {
            File process = new File(procRoot, "123");
            Files.createDirectory(process.toPath());
            String prefix = "/containers/7/.wine";
            Files.write(
                    new File(process, "environ").toPath(),
                    ("HOME=/home/xuser\0WINEPREFIX=" + prefix + "\0DISPLAY=:0\0")
                            .getBytes(StandardCharsets.UTF_8));

            assertTrue(GuestProgramLauncherComponent.hasRunningPrefixProcess(procRoot, prefix));
            assertFalse(GuestProgramLauncherComponent.hasRunningPrefixProcess(
                    procRoot, prefix + "-other"));
        } finally {
            deleteRecursively(procRoot);
        }
    }

    @Test
    public void processScanFailsClosedWhenNoEnvironmentCanBeInspected() throws Exception {
        File procRoot = Files.createTempDirectory("managed-prefix-proc-unreadable").toFile();
        try {
            Files.createDirectory(new File(procRoot, "456").toPath());

            assertThrows(
                    IOException.class,
                    () -> GuestProgramLauncherComponent.hasRunningPrefixProcess(
                            procRoot, "/containers/7/.wine"));
        } finally {
            deleteRecursively(procRoot);
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (!file.delete()) file.deleteOnExit();
    }
}
