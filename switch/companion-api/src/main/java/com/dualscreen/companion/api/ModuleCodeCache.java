package com.dualscreen.companion.api;

import java.io.*;
import java.security.MessageDigest;
import java.util.*;

/** Keeps Android-generated OAT/VDEX files outside the signed, immutable package. */
public final class ModuleCodeCache {
    public static String hash(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) {
            byte[] bytes = new byte[65536];
            for (int n; (n = input.read(bytes)) != -1;) digest.update(bytes, 0, n);
        }
        return SignedModuleArchive.hex(digest.digest());
    }

    public static File verifiedDex(File source, File cache, String expectedHash) throws Exception {
        if (!expectedHash.matches("[0-9a-f]{64}") || !hash(source).equals(expectedHash))
            throw new SecurityException("Module UI checksum failed");
        File directory = new File(cache, "companion-dex/" + expectedHash);
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create module code cache");
        File target = new File(directory, "ui.jar");
        if (!target.exists()) {
            File temporary = File.createTempFile("verified-", ".jar", directory);
            try {
                try (InputStream input = new FileInputStream(source); OutputStream output = new FileOutputStream(temporary)) {
                    byte[] bytes = new byte[65536];
                    for (int n; (n = input.read(bytes)) != -1;) output.write(bytes, 0, n);
                }
                if (!hash(temporary).equals(expectedHash) || !temporary.setReadOnly())
                    throw new SecurityException("Cannot protect verified UI cache");
                if (!temporary.renameTo(target)) throw new IOException("Cannot publish module UI cache");
            } finally { if (temporary.exists()) temporary.delete(); }
        }
        if (!hash(target).equals(expectedHash)) throw new SecurityException("Cached module UI checksum failed");
        return target;
    }

    /** Migrate only ART sidecars from the old loader; never accept arbitrary extra files. */
    public static void relocateLegacyArt(File packageDir, File cache, Set<String> extras) throws IOException {
        if (extras.isEmpty()) return;
        for (String name : extras) {
            // Observed on the Thor: ART's profile saver writes this alongside ui.jar.
            // Keep the exception exact; unrelated profiles/code must still fail validation.
            if (!name.matches("oat/arm64/ui\\.(odex|vdex|art)") && !name.equals("oat/ui.jar.cur.prof"))
                throw new SecurityException("Module file list mismatch: unexpected " + name);
            File entry = new File(packageDir, name);
            if (!entry.getCanonicalPath().equals(new File(packageDir.getCanonicalFile(), name).getAbsolutePath()))
                throw new SecurityException("Module cache path is not canonical");
        }
        File directory = new File(cache, "companion-legacy/" + UUID.randomUUID());
        if (!directory.mkdirs()) throw new IOException("Cannot preserve legacy module cache");
        // Retain the generated files for diagnostics; they are never loaded again.
        for (String name : extras) {
            if (!new File(packageDir, name).renameTo(new File(directory, new File(name).getName())))
                throw new IOException("Cannot isolate legacy module cache: " + name);
        }
    }
    private ModuleCodeCache() {}
}
