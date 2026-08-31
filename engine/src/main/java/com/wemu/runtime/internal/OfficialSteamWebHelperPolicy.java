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
import java.nio.file.attribute.PosixFilePermission;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Set;

/** Wine policy scoped to Valve's official {@code steamwebhelper.exe}. */
public final class OfficialSteamWebHelperPolicy {
    static final String APP_DEFAULTS_KEY =
            "Software\\Wine\\AppDefaults\\steamwebhelper.exe\\DllOverrides";
    static final String WINE_GSTREAMER_VALUE = "winegstreamer";

    private static final String REGISTRY_HEADER = "WINE REGISTRY Version 2";
    private static final String FILE_SECTION =
            "[" + APP_DEFAULTS_KEY.replace("\\", "\\\\") + "]";
    private static final String DISABLED_VALUE_LINE =
            "\"" + WINE_GSTREAMER_VALUE + "\"=\"\"";
    private static final long MAXIMUM_USER_REGISTRY_BYTES = 64L * 1024L * 1024L;

    private OfficialSteamWebHelperPolicy() {}

    /**
     * Only authenticated, app-managed x86_64 Steam runtimes own this policy.
     * Ordinary Wine containers and ARM64EC launches retain their selected DLL
     * load order.
     */
    public static boolean shouldApply(boolean managedOfficialSteamRuntime, boolean arm64EcWine) {
        return managedOfficialSteamRuntime && !arm64EcWine;
    }

    /**
     * Disables Wine's built-in GStreamer module for Steam's CEF helper only.
     *
     * <p>Wine resolves this key by executable basename. An empty load-order
     * value means disabled, so games Steam launches keep their own Wine media
     * stack. The prefix must be inactive while this backing registry file is
     * changed; the launch coordinator enforces that before calling here.</p>
     *
     * @return {@code true} when {@code user.reg} changed, or {@code false} when
     *         the exact policy was already present
     */
    public static boolean ensureWineGStreamerDisabled(File winePrefix) throws IOException {
        Path canonicalPrefix = requireCanonicalPrefix(winePrefix);
        Path userRegistry = canonicalPrefix.resolve("user.reg");
        StableRegularFile current = readStableRegularFile(
                userRegistry, "Wine user registry", MAXIMUM_USER_REGISTRY_BYTES);
        byte[] expected = withWineGStreamerDisabled(current.contents);
        if (Arrays.equals(current.contents, expected)) return false;

        publishAtomically(userRegistry, expected, current.attributes);
        StableRegularFile published = readStableRegularFile(
                userRegistry, "Wine user registry", MAXIMUM_USER_REGISTRY_BYTES);
        if (!Arrays.equals(expected, published.contents)
                || !Arrays.equals(
                        published.contents,
                        withWineGStreamerDisabled(published.contents))) {
            throw new IOException("The Steam web helper registry policy could not be verified");
        }
        return true;
    }

    /** Performs a byte-preserving edit of Wine's text registry format. */
    static byte[] withWineGStreamerDisabled(byte[] registry) throws IOException {
        String source = new String(registry, StandardCharsets.ISO_8859_1);
        List<RegistryLine> lines = lines(source);
        if (lines.isEmpty()
                || !REGISTRY_HEADER.equals(lines.get(0).content(source))) {
            throw new IOException("Wine user.reg is not a version 2 registry file");
        }

        int sectionLine = -1;
        for (int index = 1; index < lines.size(); index++) {
            if (!isTargetSection(lines.get(index).content(source))) continue;
            if (sectionLine != -1) {
                throw new IOException("Wine user.reg contains duplicate Steam helper policy keys");
            }
            sectionLine = index;
        }

        String newline = newlineStyle(source);
        if (sectionLine == -1) {
            StringBuilder appended = new StringBuilder(source.length()
                    + FILE_SECTION.length() + DISABLED_VALUE_LINE.length() + 4);
            appended.append(source);
            if (!source.isEmpty() && !source.endsWith("\n")) appended.append(newline);
            if (!endsWithBlankLine(appended, newline)) appended.append(newline);
            appended.append(FILE_SECTION).append(newline)
                    .append(DISABLED_VALUE_LINE).append(newline);
            return appended.toString().getBytes(StandardCharsets.ISO_8859_1);
        }

        int nextSectionLine = lines.size();
        for (int index = sectionLine + 1; index < lines.size(); index++) {
            if (isSection(lines.get(index).content(source))) {
                nextSectionLine = index;
                break;
            }
        }

        ValueLocation target = null;
        for (int index = sectionLine + 1; index < nextSectionLine; index++) {
            int rightHandSide = targetValueRightHandSide(lines.get(index).content(source));
            if (rightHandSide < 0) continue;
            if (target != null) {
                throw new IOException("Wine user.reg contains duplicate winegstreamer values");
            }
            target = new ValueLocation(index, lines.get(index).start + rightHandSide);
        }

        if (target != null) {
            RegistryLine first = lines.get(target.line);
            int logicalEnd = first.end;
            int continuation = target.line;
            while (lineContinues(source, lines.get(continuation))) {
                continuation++;
                if (continuation >= nextSectionLine) break;
                logicalEnd = lines.get(continuation).end;
            }

            String existing = source.substring(target.rightHandSide, first.contentEnd).trim();
            if ("\"\"".equals(existing) && logicalEnd == first.end) {
                return registry;
            }
            String updated = source.substring(0, target.rightHandSide)
                    + "\"\""
                    + source.substring(first.contentEnd, first.end)
                    + source.substring(logicalEnd);
            return updated.getBytes(StandardCharsets.ISO_8859_1);
        }

        int insertionLine = nextSectionLine;
        while (insertionLine > sectionLine + 1
                && lines.get(insertionLine - 1).content(source).isEmpty()) {
            insertionLine--;
        }
        int insertion = insertionLine < lines.size()
                ? lines.get(insertionLine).start : source.length();
        String separator = insertion > 0 && source.charAt(insertion - 1) != '\n' ? newline : "";
        String updated = source.substring(0, insertion)
                + separator + DISABLED_VALUE_LINE + newline
                + source.substring(insertion);
        return updated.getBytes(StandardCharsets.ISO_8859_1);
    }

    private static Path requireCanonicalPrefix(File winePrefix) throws IOException {
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
        return canonical;
    }

    private static StableRegularFile readStableRegularFile(
            Path file,
            String label,
            long maximumBytes)
            throws IOException {
        BasicFileAttributes before = requireRegularNoFollow(file, label);
        if (before.size() > maximumBytes || before.size() > Integer.MAX_VALUE) {
            throw new IOException(label + " is too large");
        }

        byte[] contents = new byte[(int) before.size()];
        try (FileChannel channel = FileChannel.open(
                file, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
            ByteBuffer target = ByteBuffer.wrap(contents);
            while (target.hasRemaining()) {
                if (channel.read(target) < 0) throw new IOException(label + " was truncated");
            }
            if (channel.read(ByteBuffer.allocate(1)) != -1) {
                throw new IOException(label + " grew while it was read");
            }
        }

        BasicFileAttributes after = requireRegularNoFollow(file, label);
        if (!sameFileState(before, after)) {
            throw new IOException(label + " changed while it was read");
        }
        return new StableRegularFile(contents, after);
    }

    private static BasicFileAttributes requireRegularNoFollow(Path file, String label)
            throws IOException {
        BasicFileAttributes attributes = Files.readAttributes(
                file, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        if (attributes.isSymbolicLink() || !attributes.isRegularFile()) {
            throw new IOException(label + " is not a regular file: " + file);
        }
        return attributes;
    }

    private static boolean sameFileState(
            BasicFileAttributes expected,
            BasicFileAttributes actual) {
        return expected.size() == actual.size()
                && expected.lastModifiedTime().equals(actual.lastModifiedTime())
                && (expected.fileKey() == null
                || expected.fileKey().equals(actual.fileKey()));
    }

    private static void publishAtomically(
            Path destination,
            byte[] contents,
            BasicFileAttributes expectedDestination) throws IOException {
        Path temporary = Files.createTempFile(
                destination.getParent(), ".wemu-steam-user-reg-", ".tmp");
        boolean published = false;
        try {
            requireRegularNoFollow(temporary, "Steam registry staging file");
            try (FileChannel channel = FileChannel.open(
                    temporary,
                    StandardOpenOption.WRITE,
                    StandardOpenOption.TRUNCATE_EXISTING,
                    LinkOption.NOFOLLOW_LINKS)) {
                ByteBuffer source = ByteBuffer.wrap(contents);
                while (source.hasRemaining()) channel.write(source);
                channel.force(true);
            }
            copyPosixPermissionsIfSupported(destination, temporary);
            if (!sameFileState(
                    expectedDestination,
                    requireRegularNoFollow(destination, "Wine user registry"))) {
                throw new IOException(
                        "Wine user registry changed before the helper policy was published");
            }
            Files.move(
                    temporary,
                    destination,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            published = true;
        } finally {
            if (!published) Files.deleteIfExists(temporary);
        }
    }

    private static void copyPosixPermissionsIfSupported(Path source, Path destination)
            throws IOException {
        try {
            Set<PosixFilePermission> permissions = Files.getPosixFilePermissions(
                    source, LinkOption.NOFOLLOW_LINKS);
            Files.setPosixFilePermissions(destination, permissions);
        } catch (UnsupportedOperationException ignored) {
            // Host-side unit tests may use a filesystem without POSIX modes.
        }
    }

    private static List<RegistryLine> lines(String source) {
        ArrayList<RegistryLine> result = new ArrayList<>();
        int start = 0;
        while (start < source.length()) {
            int newline = source.indexOf('\n', start);
            int end = newline < 0 ? source.length() : newline + 1;
            int contentEnd = newline < 0 ? end : newline;
            if (contentEnd > start && source.charAt(contentEnd - 1) == '\r') contentEnd--;
            result.add(new RegistryLine(start, contentEnd, end));
            start = end;
        }
        return result;
    }

    private static boolean isTargetSection(String line) {
        if (line.length() < FILE_SECTION.length()
                || !line.regionMatches(true, 0, FILE_SECTION, 0, FILE_SECTION.length())) {
            return false;
        }
        return line.length() == FILE_SECTION.length()
                || line.charAt(FILE_SECTION.length()) == ' '
                || line.charAt(FILE_SECTION.length()) == '\t';
    }

    private static boolean isSection(String line) {
        return line.length() > 1 && line.charAt(0) == '[';
    }

    private static int targetValueRightHandSide(String line) {
        int at = 0;
        while (at < line.length() && (line.charAt(at) == ' ' || line.charAt(at) == '\t')) at++;
        if (at >= line.length() || line.charAt(at++) != '"') return -1;
        int nameStart = at;
        boolean escaped = false;
        while (at < line.length()) {
            char current = line.charAt(at);
            if (current == '"' && !escaped) break;
            if (current == '\\' && !escaped) escaped = true;
            else escaped = false;
            at++;
        }
        if (at >= line.length()
                || !line.substring(nameStart, at).equalsIgnoreCase(WINE_GSTREAMER_VALUE)) {
            return -1;
        }
        at++;
        while (at < line.length() && (line.charAt(at) == ' ' || line.charAt(at) == '\t')) at++;
        if (at >= line.length() || line.charAt(at++) != '=') return -1;
        while (at < line.length() && (line.charAt(at) == ' ' || line.charAt(at) == '\t')) at++;
        return at;
    }

    private static boolean lineContinues(String source, RegistryLine line) {
        int at = line.contentEnd - 1;
        while (at >= line.start
                && (source.charAt(at) == ' ' || source.charAt(at) == '\t')) at--;
        return at >= line.start && source.charAt(at) == '\\';
    }

    private static String newlineStyle(String source) {
        int newline = source.indexOf('\n');
        return newline > 0 && source.charAt(newline - 1) == '\r' ? "\r\n" : "\n";
    }

    private static boolean endsWithBlankLine(StringBuilder value, String newline) {
        int length = value.length();
        if (length < newline.length() * 2) return false;
        return value.substring(length - newline.length() * 2)
                .equals(newline + newline);
    }

    private static final class RegistryLine {
        final int start;
        final int contentEnd;
        final int end;

        RegistryLine(int start, int contentEnd, int end) {
            this.start = start;
            this.contentEnd = contentEnd;
            this.end = end;
        }

        String content(String source) {
            return source.substring(start, contentEnd);
        }
    }

    private static final class ValueLocation {
        final int line;
        final int rightHandSide;

        ValueLocation(int line, int rightHandSide) {
            this.line = line;
            this.rightHandSide = rightHandSide;
        }
    }

    private static final class StableRegularFile {
        final byte[] contents;
        final BasicFileAttributes attributes;

        StableRegularFile(byte[] contents, BasicFileAttributes attributes) {
            this.contents = contents;
            this.attributes = attributes;
        }
    }
}
