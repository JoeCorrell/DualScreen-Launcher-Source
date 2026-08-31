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
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.Objects;
import java.util.Set;

/**
 * Reconciles Valve's Common Redistributables completion records with the
 * Microsoft products that are actually installed in a managed Steam prefix.
 *
 * <p>Steam treats the named DWORD below each {@code hasrunkey} as proof that an
 * install script completed. A copied or pre-seeded value can therefore suppress
 * the legitimate signed installer even when its product registry and files do
 * not exist. This repair never manufactures Microsoft or Valve state. It only
 * removes an exact false-positive Valve value, allowing Steam to run the signed
 * depot installer on the next game launch.</p>
 *
 * <p>The caller must invoke this while the exact prefix is inactive. The
 * registry replacement is atomic and refuses symlinked or malformed inputs.</p>
 */
public final class OfficialSteamPrerequisiteStateRepair {
    private static final String MANAGED_RUNTIME_SCHEMA = "1";
    private static final String PROTON_9_SPEC = "proton9-x64";

    private static final long MAXIMUM_SYSTEM_REG_BYTES = 64L * 1024L * 1024L;
    private static final String REGISTRY_HEADER = "WINE REGISTRY Version 2";

    private static final String VALVE_DOTNET_4_KEY =
            "Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.0";
    private static final String VALVE_DOTNET_4_CLIENT_KEY =
            "Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist"
                    + "\\.NET\\4.0 Client Profile";
    private static final String VALVE_XNA_4_KEY =
            "Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\XNA\\4.0";

    private static final String NDP_4_CLIENT_KEY =
            "Software\\Microsoft\\NET Framework Setup\\NDP\\v4\\Client";
    private static final String NDP_4_CLIENT_WOW64_KEY =
            "Software\\Wow6432Node\\Microsoft\\NET Framework Setup\\NDP\\v4\\Client";
    private static final String NDP_4_FULL_KEY =
            "Software\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full";
    private static final String NDP_4_FULL_WOW64_KEY =
            "Software\\Wow6432Node\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full";
    private static final String XNA_4_KEY =
            "Software\\Microsoft\\XNA\\Framework\\v4.0";
    private static final String XNA_4_WOW64_KEY =
            "Software\\Wow6432Node\\Microsoft\\XNA\\Framework\\v4.0";

    private static final RegistryValue DOTNET_4_MARKER =
            new RegistryValue(VALVE_DOTNET_4_KEY, "4.0");
    private static final RegistryValue DOTNET_4_CLIENT_MARKER =
            new RegistryValue(VALVE_DOTNET_4_CLIENT_KEY, "4.0 Client Profile");
    private static final RegistryValue XNA_4_REFRESH_MARKER =
            new RegistryValue(VALVE_XNA_4_KEY, "4.0 Refresh");
    /** A legacy seeded value not named by Valve's signed XNA 4 Refresh script. */
    private static final RegistryValue XNA_4_LEGACY_MARKER =
            new RegistryValue(VALVE_XNA_4_KEY, "4.0");

    private OfficialSteamPrerequisiteStateRepair() {}

    /** Outcome suitable for fixed, account-free diagnostics. */
    public static final class Result {
        public final boolean applicable;
        public final int valuesRemoved;

        private Result(boolean applicable, int valuesRemoved) {
            this.applicable = applicable;
            this.valuesRemoved = valuesRemoved;
        }

        public boolean changed() {
            return valuesRemoved > 0;
        }
    }

    /**
     * Removes exact false completion values from {@code .wine/system.reg}.
     * Missing prefixes/registries are a normal no-op for a new container.
     */
    public static Result repair(
            File containerRoot,
            String managedRuntimeMarker,
            String managedRuntimeSpec) throws IOException {
        if (!isManagedOfficialSteamSpec(managedRuntimeMarker, managedRuntimeSpec)) {
            return new Result(false, 0);
        }
        if (containerRoot == null) {
            throw new IOException("The managed Steam container root is unavailable");
        }

        Path root = containerRoot.toPath().toAbsolutePath().normalize();
        if (Files.isSymbolicLink(root)
                || !Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS)) {
            throw new IOException("The managed Steam container root is not a regular directory: "
                    + root);
        }

        Path prefix = root.resolve(".wine");
        if (!Files.exists(prefix, LinkOption.NOFOLLOW_LINKS)) {
            return new Result(true, 0);
        }
        if (Files.isSymbolicLink(prefix)
                || !Files.isDirectory(prefix, LinkOption.NOFOLLOW_LINKS)) {
            throw new IOException("The managed Steam prefix is not a regular directory: " + prefix);
        }

        Path systemReg = prefix.resolve("system.reg");
        if (!Files.exists(systemReg, LinkOption.NOFOLLOW_LINKS)) {
            return new Result(true, 0);
        }
        BasicFileAttributes before = requireRegularRegistry(systemReg);
        if (before.size() > MAXIMUM_SYSTEM_REG_BYTES || before.size() > Integer.MAX_VALUE) {
            throw new IOException("The managed Steam system registry is unexpectedly large");
        }

        byte[] original = readRegularRegistry(systemReg, before);
        String registryText = new String(original, StandardCharsets.ISO_8859_1);
        RegistrySnapshot registry = RegistrySnapshot.parse(registryText);

        boolean clientFilesPresent = regularNonempty(prefix.resolve(
                "drive_c/windows/Microsoft.NET/Framework/v4.0.30319/mscorlib.dll"))
                && regularNonempty(prefix.resolve(
                "drive_c/windows/Microsoft.NET/Framework64/v4.0.30319/mscorlib.dll"));
        boolean fullFilesPresent = regularNonempty(prefix.resolve(
                "drive_c/windows/Microsoft.NET/Framework/v4.0.30319/System.Web.dll"))
                && regularNonempty(prefix.resolve(
                "drive_c/windows/Microsoft.NET/Framework64/v4.0.30319/System.Web.dll"));
        boolean xnaFilesPresent = regularNonempty(prefix.resolve(
                "drive_c/Program Files (x86)/Common Files/Microsoft Shared"
                        + "/XNA/Framework/v4.0/XnaNative.dll"));

        boolean clientInstalled = clientFilesPresent
                && (registry.hasDwordOne(NDP_4_CLIENT_KEY, "Install")
                || registry.hasDwordOne(NDP_4_CLIENT_WOW64_KEY, "Install"));
        boolean fullInstalled = fullFilesPresent
                && (registry.hasDwordOne(NDP_4_FULL_KEY, "Install")
                || registry.hasDwordOne(NDP_4_FULL_WOW64_KEY, "Install"));
        boolean xnaInstalled = xnaFilesPresent
                && ((registry.hasDwordOne(XNA_4_KEY, "Installed")
                && registry.hasDwordOne(XNA_4_KEY, "Refresh1Installed"))
                || (registry.hasDwordOne(XNA_4_WOW64_KEY, "Installed")
                && registry.hasDwordOne(XNA_4_WOW64_KEY, "Refresh1Installed")));

        Set<RegistryValue> removals = new LinkedHashSet<>();
        if (!fullInstalled && registry.hasDwordOne(DOTNET_4_MARKER)) {
            removals.add(DOTNET_4_MARKER);
        }
        if (!clientInstalled && registry.hasDwordOne(DOTNET_4_CLIENT_MARKER)) {
            removals.add(DOTNET_4_CLIENT_MARKER);
        }
        if (!xnaInstalled) {
            if (registry.hasDwordOne(XNA_4_REFRESH_MARKER)) {
                removals.add(XNA_4_REFRESH_MARKER);
            }
            if (registry.hasDwordOne(XNA_4_LEGACY_MARKER)) {
                removals.add(XNA_4_LEGACY_MARKER);
            }
        }
        if (removals.isEmpty()) return new Result(true, 0);

        String repairedText = removeExactDwordOneValues(registryText, removals);
        byte[] repaired = repairedText.getBytes(StandardCharsets.ISO_8859_1);
        if (Arrays.equals(original, repaired)) {
            throw new IOException("The false Steam prerequisite markers could not be isolated");
        }

        BasicFileAttributes afterRead = requireRegularRegistry(systemReg);
        if (!sameFileState(before, afterRead)) {
            throw new IOException("The managed Steam system registry changed during repair");
        }
        publishAtomically(prefix, systemReg, repaired, before);
        return new Result(true, removals.size());
    }

    private static boolean isManagedOfficialSteamSpec(String marker, String spec) {
        if (!MANAGED_RUNTIME_SCHEMA.equals(marker)) return false;
        return PROTON_9_SPEC.equals(spec)
                || OfficialSteamPrefixPreflight.PROTON_10_SPEC.equals(spec)
                || OfficialSteamPrefixPreflight.PROTON_11_SPEC.equals(spec);
    }

    private static boolean regularNonempty(Path path) throws IOException {
        return Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) && Files.size(path) > 0;
    }

    private static BasicFileAttributes requireRegularRegistry(Path registry) throws IOException {
        BasicFileAttributes attributes = Files.readAttributes(
                registry, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        if (attributes.isSymbolicLink() || !attributes.isRegularFile()) {
            throw new IOException("The managed Steam system registry is not a regular file: "
                    + registry);
        }
        return attributes;
    }

    private static byte[] readRegularRegistry(
            Path registry,
            BasicFileAttributes expected) throws IOException {
        if (expected.size() > Integer.MAX_VALUE) {
            throw new IOException("The managed Steam system registry is too large to read");
        }
        byte[] contents = new byte[(int) expected.size()];
        try (FileChannel channel = FileChannel.open(
                registry, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
            ByteBuffer target = ByteBuffer.wrap(contents);
            while (target.hasRemaining()) {
                if (channel.read(target) < 0) {
                    throw new IOException("The managed Steam system registry was truncated");
                }
            }
            if (channel.read(ByteBuffer.allocate(1)) != -1) {
                throw new IOException("The managed Steam system registry grew while being read");
            }
        }
        if (!sameFileState(expected, requireRegularRegistry(registry))) {
            throw new IOException("The managed Steam system registry changed while being read");
        }
        return contents;
    }

    private static boolean sameFileState(
            BasicFileAttributes before,
            BasicFileAttributes after) {
        return before.size() == after.size()
                && before.lastModifiedTime().equals(after.lastModifiedTime())
                && (before.fileKey() == null
                || after.fileKey() == null
                || before.fileKey().equals(after.fileKey()));
    }

    private static void publishAtomically(
            Path prefix,
            Path systemReg,
            byte[] contents,
            BasicFileAttributes expected) throws IOException {
        Path staged = Files.createTempFile(prefix, ".wemu-steam-prerequisite-", ".tmp");
        boolean published = false;
        try {
            BasicFileAttributes stagedAttributes = Files.readAttributes(
                    staged, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
            if (stagedAttributes.isSymbolicLink() || !stagedAttributes.isRegularFile()) {
                throw new IOException("The Steam prerequisite staging file is not regular");
            }
            try (FileChannel channel = FileChannel.open(
                    staged,
                    StandardOpenOption.WRITE,
                    StandardOpenOption.TRUNCATE_EXISTING,
                    LinkOption.NOFOLLOW_LINKS)) {
                ByteBuffer source = ByteBuffer.wrap(contents);
                while (source.hasRemaining()) channel.write(source);
                channel.force(true);
            }

            if (!sameFileState(expected, requireRegularRegistry(systemReg))) {
                throw new IOException("The managed Steam system registry changed before publication");
            }
            Files.move(
                    staged,
                    systemReg,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            published = true;
        } finally {
            if (!published) Files.deleteIfExists(staged);
        }

        BasicFileAttributes publishedAttributes = requireRegularRegistry(systemReg);
        byte[] publishedContents = readRegularRegistry(systemReg, publishedAttributes);
        if (!Arrays.equals(contents, publishedContents)) {
            throw new IOException("The repaired managed Steam registry could not be verified");
        }
    }

    private static String removeExactDwordOneValues(
            String source,
            Set<RegistryValue> removals) throws IOException {
        String currentKey = null;
        StringBuilder output = null;
        int lineStart = 0;
        while (lineStart < source.length()) {
            int newline = source.indexOf('\n', lineStart);
            int nextLine = newline == -1 ? source.length() : newline + 1;
            int contentEnd = newline == -1 ? source.length() : newline;
            if (contentEnd > lineStart && source.charAt(contentEnd - 1) == '\r') {
                contentEnd--;
            }
            String line = source.substring(lineStart, contentEnd);
            String header = parseHeader(line);
            if (header != null) currentKey = header;

            RegistryValue value = parseDwordOne(currentKey, line);
            boolean remove = value != null && removals.contains(value);
            if (remove && output == null) {
                output = new StringBuilder(source.length());
                output.append(source, 0, lineStart);
            } else if (!remove && output != null) {
                output.append(source, lineStart, nextLine);
            }
            lineStart = nextLine;
        }
        return output == null ? source : output.toString();
    }

    private static String parseHeader(String line) throws IOException {
        if (!line.startsWith("[")) return null;
        int closing = line.indexOf(']');
        if (closing <= 1) throw new IOException("Malformed Wine registry section header");
        return unescapeToken(line.substring(1, closing));
    }

    private static RegistryValue parseDwordOne(String key, String line) throws IOException {
        if (key == null || !line.startsWith("\"")) return null;
        int closing = findClosingQuote(line, 1);
        if (closing < 0 || closing + 1 >= line.length() || line.charAt(closing + 1) != '=') {
            return null;
        }
        String raw = line.substring(closing + 2);
        if (!"dword:00000001".equalsIgnoreCase(raw)) return null;
        return new RegistryValue(key, unescapeToken(line.substring(1, closing)));
    }

    private static int findClosingQuote(String value, int start) {
        boolean escaped = false;
        for (int i = start; i < value.length(); i++) {
            char current = value.charAt(i);
            if (escaped) {
                escaped = false;
            } else if (current == '\\') {
                escaped = true;
            } else if (current == '"') {
                return i;
            }
        }
        return -1;
    }

    private static String unescapeToken(String escaped) throws IOException {
        StringBuilder plain = new StringBuilder(escaped.length());
        for (int i = 0; i < escaped.length(); i++) {
            char current = escaped.charAt(i);
            if (current != '\\') {
                plain.append(current);
                continue;
            }
            if (++i >= escaped.length()) throw new IOException("Malformed Wine registry escape");
            char next = escaped.charAt(i);
            if (next != '\\' && next != '"') {
                throw new IOException("Unsupported Wine registry escape in prerequisite state");
            }
            plain.append(next);
        }
        return plain.toString();
    }

    private static final class RegistrySnapshot {
        private final Set<RegistryValue> dwordOnes;

        private RegistrySnapshot(Set<RegistryValue> dwordOnes) {
            this.dwordOnes = dwordOnes;
        }

        static RegistrySnapshot parse(String source) throws IOException {
            if (!source.startsWith(REGISTRY_HEADER)) {
                throw new IOException("The managed Steam system registry header is invalid");
            }
            Set<RegistryValue> values = new HashSet<>();
            String currentKey = null;
            int lineStart = 0;
            while (lineStart < source.length()) {
                int newline = source.indexOf('\n', lineStart);
                int contentEnd = newline == -1 ? source.length() : newline;
                if (contentEnd > lineStart && source.charAt(contentEnd - 1) == '\r') {
                    contentEnd--;
                }
                String line = source.substring(lineStart, contentEnd);
                String header = parseHeader(line);
                if (header != null) currentKey = header;
                RegistryValue value = parseDwordOne(currentKey, line);
                if (value != null) values.add(value);
                if (newline == -1) break;
                lineStart = newline + 1;
            }
            return new RegistrySnapshot(values);
        }

        boolean hasDwordOne(String key, String name) {
            return hasDwordOne(new RegistryValue(key, name));
        }

        boolean hasDwordOne(RegistryValue value) {
            return dwordOnes.contains(value);
        }
    }

    private static final class RegistryValue {
        final String key;
        final String name;

        RegistryValue(String key, String name) {
            this.key = key;
            this.name = name;
        }

        @Override
        public boolean equals(Object other) {
            if (this == other) return true;
            if (!(other instanceof RegistryValue)) return false;
            RegistryValue that = (RegistryValue) other;
            return key.equals(that.key) && name.equals(that.name);
        }

        @Override
        public int hashCode() {
            return Objects.hash(key, name);
        }
    }
}
