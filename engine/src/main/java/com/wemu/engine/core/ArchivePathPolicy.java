package com.wemu.engine.core;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Path policy shared by archive importers.
 *
 * Archive names are data, never paths that may be handed directly to
 * {@link File}.  This class keeps every output below one canonical root and
 * refuses to traverse a symlink while creating an entry.  The latter matters
 * even after a lexical {@code ..} check: an archive can create {@code out/link}
 * first and then use {@code out/link/file} to escape on a later entry.
 */
public final class ArchivePathPolicy {
    private ArchivePathPolicy() {}

    /** Resolves one archive entry and rejects absolute and escaping names. */
    public static File resolveEntry(File destination, String entryName) throws IOException {
        if (destination == null) throw new IOException("Archive destination is missing");
        if (entryName == null || entryName.indexOf('\0') >= 0) {
            throw new IOException("Archive entry has an invalid name");
        }

        String portableName = entryName.replace('\\', '/');
        if (portableName.startsWith("/") || hasWindowsDrivePrefix(portableName)) {
            throw new IOException("Archive entry is absolute: " + entryName);
        }

        Path relative;
        try {
            relative = Paths.get(portableName).normalize();
        } catch (RuntimeException invalidPath) {
            throw new IOException("Archive entry has an invalid path: " + entryName,
                    invalidPath);
        }
        if (relative.isAbsolute() || startsWithParent(relative)) {
            throw new IOException("Archive entry escapes its destination: " + entryName);
        }

        Path root = destination.getCanonicalFile().toPath();
        Path output = root.resolve(relative).normalize();
        if (!output.startsWith(root)) {
            throw new IOException("Archive entry escapes its destination: " + entryName);
        }
        return output.toFile();
    }

    /**
     * Revalidates an output returned by an extraction listener.
     *
     * Listeners are useful for progress and path rewriting, but their result is
     * still constrained to the extraction root.
     */
    public static File requireContainedOutput(File destination, File output) throws IOException {
        if (destination == null || output == null) {
            throw new IOException("Archive output is missing");
        }
        Path root = destination.getCanonicalFile().toPath();
        Path lexical = output.toPath().toAbsolutePath().normalize();
        if (!lexical.startsWith(root)) {
            throw new IOException("Archive listener redirected output outside its destination: "
                    + output);
        }

        // Canonicalization catches a pre-existing parent symlink.  A dangling
        // final symlink is handled separately by rejectExistingLink().
        Path canonical = output.getCanonicalFile().toPath();
        if (!canonical.startsWith(root)) {
            throw new IOException("Archive output resolves outside its destination: " + output);
        }
        return output;
    }

    /** Creates all parents without ever following an archive-created symlink. */
    public static void createSafeParents(File destination, File output) throws IOException {
        Path root = destination.getCanonicalFile().toPath();
        Path parent = output.toPath().toAbsolutePath().normalize().getParent();
        if (parent == null || !parent.startsWith(root)) {
            throw new IOException("Archive output has no safe parent: " + output);
        }

        ensureDirectory(root);
        Path current = root;
        Path relative = root.relativize(parent);
        for (Path part : relative) {
            current = current.resolve(part);
            if (Files.isSymbolicLink(current)) {
                throw new IOException("Archive output traverses a symbolic link: " + current);
            }
            if (Files.exists(current, LinkOption.NOFOLLOW_LINKS)) {
                if (!Files.isDirectory(current, LinkOption.NOFOLLOW_LINKS)) {
                    throw new IOException("Archive parent is not a directory: " + current);
                }
            } else {
                Files.createDirectory(current);
            }
        }
    }

    /** Creates a directory entry without replacing or following a link. */
    public static void createSafeDirectory(File destination, File directory) throws IOException {
        requireContainedOutput(destination, directory);

        /*
         * A tar may spell its extraction root as an explicit `./` directory
         * entry.  That directory has no parent *inside* the extraction root,
         * so createSafeParents() correctly rejects it.  It is nevertheless a
         * safe no-op: validate the destination itself and leave every child to
         * the ordinary parent/link checks below.
         */
        Path root = destination.getCanonicalFile().toPath();
        Path path = directory.toPath().toAbsolutePath().normalize();
        if (path.equals(root)) {
            ensureDirectory(root);
            return;
        }

        createSafeParents(destination, directory);
        if (Files.isSymbolicLink(path)) {
            throw new IOException("Archive directory collides with a symbolic link: " + directory);
        }
        if (Files.exists(path, LinkOption.NOFOLLOW_LINKS)) {
            if (!Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) {
                throw new IOException("Archive directory collides with a file: " + directory);
            }
        } else {
            Files.createDirectory(path);
        }
    }

    /** Refuses to stream file bytes through a link already present at the path. */
    public static void rejectExistingLink(File output) throws IOException {
        if (Files.isSymbolicLink(output.toPath())) {
            throw new IOException("Archive file collides with a symbolic link: " + output);
        }
    }

    /**
     * Validates a symbolic-link target.
     *
     * User supplied packages may only use relative links that remain in their
     * extraction root.  Trusted image payloads may retain absolute guest links;
     * callers opting into those must still never traverse them during extraction.
     */
    public static void validateSymbolicLink(
            File destination,
            File link,
            String target,
            boolean allowAbsoluteGuestTarget) throws IOException {
        if (target == null || target.isEmpty() || target.indexOf('\0') >= 0) {
            throw new IOException("Archive symbolic link has an invalid target");
        }
        String portableTarget = target.replace('\\', '/');
        if (portableTarget.startsWith("/") || hasWindowsDrivePrefix(portableTarget)) {
            if (allowAbsoluteGuestTarget) return;
            throw new IOException("Archive symbolic link has an absolute target: " + target);
        }

        Path root = destination.getCanonicalFile().toPath();
        Path parent = link.toPath().toAbsolutePath().normalize().getParent();
        Path resolved;
        try {
            resolved = parent.resolve(Paths.get(portableTarget)).normalize();
        } catch (RuntimeException invalidPath) {
            throw new IOException("Archive symbolic link has an invalid target: " + target,
                    invalidPath);
        }
        if (!resolved.startsWith(root)) {
            throw new IOException("Archive symbolic link escapes its destination: " + target);
        }
    }

    /** A metadata name that will be used as one child directory. */
    public static boolean isSafePathComponent(String name) {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                || name.length() > 255 || name.indexOf('\0') >= 0
                || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0
                || hasWindowsDrivePrefix(name)) {
            return false;
        }
        return true;
    }

    private static boolean startsWithParent(Path path) {
        return path.getNameCount() > 0 && path.getName(0).toString().equals("..");
    }

    private static boolean hasWindowsDrivePrefix(String path) {
        return path.length() >= 2 && Character.isLetter(path.charAt(0))
                && path.charAt(1) == ':';
    }

    private static void ensureDirectory(Path directory) throws IOException {
        if (Files.isSymbolicLink(directory)) {
            throw new IOException("Archive destination is a symbolic link: " + directory);
        }
        if (Files.exists(directory, LinkOption.NOFOLLOW_LINKS)) {
            if (!Files.isDirectory(directory, LinkOption.NOFOLLOW_LINKS)) {
                throw new IOException("Archive destination is not a directory: " + directory);
            }
        } else {
            Files.createDirectories(directory);
        }
    }
}
