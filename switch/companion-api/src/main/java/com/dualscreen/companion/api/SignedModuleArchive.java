package com.dualscreen.companion.api;

import java.io.*;
import java.security.MessageDigest;
import java.security.cert.Certificate;
import java.util.*;
import java.util.jar.*;

/** Verifies EVERY non-signature entry before it can become executable. No Android dependencies. */
public final class SignedModuleArchive {
    public static final long MAX_BYTES = 128L * 1024 * 1024;
    public static String safePath(String path) {
        if (path.isEmpty() || path.length() > 240 || path.indexOf('\\') >= 0 || path.indexOf(':') >= 0 ||
            path.startsWith("/") || path.indexOf('\0') >= 0) throw new SecurityException("Invalid package path");
        for (String part : path.split("/", -1))
            if (part.isEmpty() || part.equals(".") || part.equals("..")) throw new SecurityException("Invalid package path");
        return path;
    }
    public static String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) result.append(String.format(Locale.ROOT, "%02x", b & 255));
        return result.toString();
    }
    public static Map<String, String> extract(File archive, File destination, Set<String> trustedCertificates) throws Exception {
        if (trustedCertificates.isEmpty() || !destination.isDirectory() || destination.list().length != 0 ||
            archive.length() > MAX_BYTES) throw new SecurityException("Invalid module installation");
        Map<String, String> hashes = new LinkedHashMap<>();
        Set<String> seen = new HashSet<>();
        long total = 0;
        try (JarFile jar = new JarFile(archive, true)) {
            Enumeration<JarEntry> entries = jar.entries();
            byte[] buffer = new byte[65536];
            while (entries.hasMoreElements()) {
                JarEntry entry = entries.nextElement();
                String name = entry.getName();
                if (!seen.add(name) || seen.size() > 5000) throw new SecurityException("Duplicate/too many entries");
                if (entry.isDirectory()) { safePath(name.substring(0, name.length() - 1)); continue; }
                safePath(name);
                // Only JAR signature metadata may lack a signer. Do not broadly exempt META-INF.
                boolean signature = name.matches("META-INF/(MANIFEST\\.MF|[A-Za-z0-9_-]{1,8}\\.(SF|RSA|DSA|EC))");
                if (!signature && !name.equals("module.json") && !name.equals("ui.jar") &&
                    !name.equals("lib/arm64-v8a/libmodule.so") && !name.startsWith("assets/"))
                    throw new SecurityException("Unexpected module entry: " + name);
                File output = new File(destination, name);
                if (!signature && !output.getParentFile().isDirectory() && !output.getParentFile().mkdirs())
                    throw new IOException("Cannot create staging directory");
                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                try (InputStream input = jar.getInputStream(entry);
                     OutputStream out = signature ? new OutputStream() {
                         public void write(int value) {}
                         public void write(byte[] data, int offset, int count) {}
                     } : new FileOutputStream(output)) {
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        total += count;
                        if (total > MAX_BYTES) throw new SecurityException("Expanded package too large");
                        digest.update(buffer, 0, count); out.write(buffer, 0, count);
                    }
                }
                if (signature) continue;
                Certificate[] certificates = entry.getCertificates();
                boolean trusted = false;
                if (certificates != null) for (Certificate cert : certificates)
                    trusted |= trustedCertificates.contains(hex(MessageDigest.getInstance("SHA-256").digest(cert.getEncoded())));
                if (!trusted) throw new SecurityException("Unsigned or untrusted module entry: " + name);
                hashes.put(name, hex(digest.digest()));
                if (!output.setReadOnly()) throw new IOException("Cannot protect verified module file");
            }
        }
        if (!hashes.containsKey("module.json") || !hashes.containsKey("ui.jar") ||
            !hashes.containsKey("lib/arm64-v8a/libmodule.so")) throw new SecurityException("Incomplete module package");
        return hashes;
    }
    private SignedModuleArchive() {}
}
