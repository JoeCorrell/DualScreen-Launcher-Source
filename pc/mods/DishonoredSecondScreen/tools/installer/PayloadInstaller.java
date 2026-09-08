package com.dualscreen.dishonoredinstaller;

import android.app.Instrumentation;
import android.os.Bundle;
import android.util.AtomicFile;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Local, release-signed instrumentation only. Never included in the launcher. */
public final class PayloadInstaller extends Instrumentation {
    private String expected;
    private boolean install;
    private String mode;
    private static final long LIMIT = 128L * 1024 * 1024;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        expected = arguments == null ? null : arguments.getString("sha256");
        install = arguments != null && "install".equals(arguments.getString("mode"));
        mode = arguments == null ? "" : arguments.getString("mode", "");
        start();
    }
    private static void require(boolean ok, String message) throws IOException {
        if (!ok) throw new IOException(message);
    }
    private static String digest(byte[] bytes) throws Exception {
        StringBuilder result = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes))
            result.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return result.toString();
    }
    private static byte[] read(InputStream in, long limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[32768]; int count;
        while ((count = in.read(buffer)) != -1) {
            require((long) out.size() + count <= limit, "Resource exceeds size limit");
            out.write(buffer, 0, count);
        }
        return out.toByteArray();
    }
    private static File child(File root, String path) throws IOException {
        require(!path.isEmpty() && !path.startsWith("/") && !path.contains("\\") && !path.contains(":"), "Invalid path");
        for (String part : path.split("/", -1)) require(!part.isEmpty() && !part.equals(".") && !part.equals(".."), "Invalid component");
        File file = new File(root, path);
        require(file.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator), "Path escapes payload");
        return file;
    }
    private static void atomic(File file, byte[] bytes) throws IOException {
        AtomicFile atomic = new AtomicFile(file);
        FileOutputStream stream = null;
        try { stream = atomic.startWrite(); stream.write(bytes); atomic.finishWrite(stream); }
        catch (IOException failure) { if (stream != null) atomic.failWrite(stream); throw failure; }
    }
    private static void cleanup(File root) throws IOException {
        if (root.isDirectory()) {
            File[] entries = root.listFiles(); require(entries != null, "Cannot list staging directory");
            for (File entry : entries) { require(entry.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator), "Unexpected staging link"); cleanup(entry); }
        }
        require(root.delete() || !root.exists(), "Cannot remove staging file");
    }
    @Override public void onStart() {
        Bundle result = new Bundle(); File staging = null;
        try {
            if (mode.equals("directx-settings")) {
                DirectXSettings.run(getTargetContext(), getContext(), result);
                finish(-1, result); return;
            }
            if (mode.equals("display-dual")) {
                DisplayBaseline.restoreDual(getTargetContext(), result);
                finish(-1, result); return;
            }
            if (mode.equals("display-inspect") || mode.equals("display-single")) {
                DisplayBaseline.run(getTargetContext(), mode.equals("display-single"), result);
                finish(-1, result); return;
            }
            require(expected != null && expected.matches("[0-9a-f]{64}"), "Exact archive SHA-256 required");
            require(getTargetContext().getPackageName().equals("com.dualscreen.launcher"), "Wrong target");
            byte[] archive, descriptorBytes;
            try (InputStream in = getContext().getAssets().open("payload.zip")) { archive = read(in, LIMIT); }
            try (InputStream in = getContext().getAssets().open("descriptor.json")) { descriptorBytes = read(in, 65536); }
            require(digest(archive).equals(expected), "Archive hash mismatch");
            JSONObject descriptor = new JSONObject(new String(descriptorBytes, StandardCharsets.UTF_8));
            require(descriptor.getInt("schema") == 1 && descriptor.getInt("launcherApi") == 1 &&
                descriptor.getString("gameId").equals("dishonored") && descriptor.getString("status").equals("development") &&
                descriptor.getString("sha256").equals(expected) && descriptor.getLong("bytes") == archive.length, "Descriptor mismatch");
            File root = new File(getTargetContext().getFilesDir(), "game-payloads/dishonored");
            require(root.isDirectory() || root.mkdirs(), "Cannot open payload cache");
            staging = new File(root, ".install-" + UUID.randomUUID());
            require(staging.mkdir(), "Cannot create staging directory");
            Set<String> extracted = new HashSet<>(); long total = 0;
            try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(archive))) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    String name = entry.getName();
                    require(!entry.isDirectory() && extracted.add(name) && extracted.size() <= 4097 &&
                        (name.equals("manifest.json") || name.startsWith("payload/")), "Unexpected archive entry");
                    File destination = child(staging, name);
                    byte[] content = read(zip, LIMIT - total); total += content.length;
                    require(destination.getParentFile().isDirectory() || destination.getParentFile().mkdirs(), "Cannot create payload directory");
                    Files.write(destination.toPath(), content);
                }
            }
            JSONObject manifest = new JSONObject(new String(Files.readAllBytes(child(staging, "manifest.json").toPath()), StandardCharsets.UTF_8));
            require(manifest.getString("version").equals(descriptor.getString("version")), "Version mismatch");
            JSONArray files = manifest.getJSONArray("files");
            require(files.length() > 0 && files.length() <= 4096 && extracted.size() == files.length() + 1, "Manifest size mismatch");
            Set<String> sources = new HashSet<>();
            for (int i = 0; i < files.length(); ++i) {
                JSONObject entry = files.getJSONObject(i); String source = entry.getString("source");
                require(sources.add(source) && entry.getString("target").equals("Binaries/Win32/" + source), "Unexpected install target");
                File file = child(staging, "payload/" + source);
                require(file.isFile() && file.length() > 0 && digest(Files.readAllBytes(file.toPath())).equals(entry.getString("sha256")), "Payload hash mismatch");
            }
            boolean disabledBaseline = descriptor.getString("version").endsWith("-baseline-disabled") &&
                sources.size() == 1 && sources.contains("DishonoredSecondScreen.ini") &&
                new String(Files.readAllBytes(child(staging, "payload/DishonoredSecondScreen.ini").toPath()), StandardCharsets.US_ASCII)
                    .equals("[Mod]\r\nEnabled=0\r\n");
            require(disabledBaseline || (sources.contains("dinput8.dll") && sources.contains("DishonoredSecondScreen.ini")), "Missing native payload");
            Files.write(new File(staging, "descriptor.json").toPath(), descriptorBytes);
            result.putInt("verifiedFiles", files.length()); result.putString("sha256", expected);
            if (install) {
                File current = new File(root, "current"), pin = new File(root, "development-package");
                byte[] oldCurrent = current.isFile() ? Files.readAllBytes(current.toPath()) : null;
                byte[] oldPin = pin.isFile() ? Files.readAllBytes(pin.toPath()) : null;
                require(oldCurrent != null && oldPin != null && java.util.Arrays.equals(oldCurrent, oldPin) &&
                    new String(oldPin, StandardCharsets.US_ASCII).matches("[0-9a-f]{64}"), "Existing explicit development opt-in required");
                File destination = new File(root, expected);
                if (destination.exists()) {
                    require(destination.isDirectory(), "Invalid existing cache");
                    for (String source : sources) {
                        File existing = child(destination, "payload/" + source);
                        require(existing.isFile() && digest(Files.readAllBytes(existing.toPath())).equals(
                            digest(Files.readAllBytes(child(staging, "payload/" + source).toPath()))), "Existing cache hash mismatch");
                    }
                    for (String metadata : new String[]{"manifest.json", "descriptor.json"})
                        require(java.util.Arrays.equals(Files.readAllBytes(child(destination, metadata).toPath()),
                            Files.readAllBytes(child(staging, metadata).toPath())), "Existing cache metadata mismatch");
                    cleanup(staging); staging = null;
                } else {
                    require(staging.renameTo(destination), "Cannot commit verified payload"); staging = null;
                }
                atomic(new File(root, "previous-ui-development-package"), oldPin);
                try { atomic(pin, expected.getBytes(StandardCharsets.US_ASCII)); atomic(current, expected.getBytes(StandardCharsets.US_ASCII)); }
                catch (IOException failure) { atomic(pin, oldPin); atomic(current, oldCurrent); throw failure; }
                result.putString("status", "installed-cache");
            } else { result.putString("status", "verified-only"); }
            finish(-1, result);
        } catch (Exception error) {
            result.putString("error", error.toString()); finish(0, result);
        } finally {
            if (staging != null) try { cleanup(staging); } catch (IOException ignored) { }
        }
    }
}
