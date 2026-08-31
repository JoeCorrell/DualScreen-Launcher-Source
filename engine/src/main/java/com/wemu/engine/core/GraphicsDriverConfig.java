package com.wemu.engine.core;

import java.util.HashMap;
import java.util.Map;

/**
 * Reads and writes the graphics-driver configuration string.
 *
 * A container stores its driver settings as one `key=value;key=value` string,
 * and in the original the code that encodes and decodes it lived on
 * `GraphicsDriverConfigDialog` — a dialog. Anything needing to read a container's
 * driver version therefore had to reach into a piece of the interface, which is
 * why `AdrenotoolsManager` imported one.
 *
 * The dialog is gone. The codec was never part of it.
 */
public final class GraphicsDriverConfig {

    private GraphicsDriverConfig() {}

    /**
     * Parses the stored string into its keys.
     *
     * Tolerant of malformed input on purpose: this string is written by earlier
     * versions of the app and edited by hand in exported container files, so a
     * bare key with no `=` yields an empty value rather than an exception that
     * would take the container list down with it.
     */
    public static HashMap<String, String> parse(String graphicsDriverConfig) {
        HashMap<String, String> mappedConfig = new HashMap<>();
        if (graphicsDriverConfig == null || graphicsDriverConfig.isEmpty()) return mappedConfig;

        for (String element : graphicsDriverConfig.split(";")) {
            if (element.isEmpty()) continue;
            final String[] parts = element.split("=");
            if (parts.length == 0) continue;
            mappedConfig.put(parts[0], parts.length > 1 ? parts[1] : "");
        }
        return mappedConfig;
    }

    /**
     * Encodes the keys back into the stored form.
     *
     * The original built this with `+=` and then chopped the trailing separator
     * off with `substring`, which throws on an empty map — reachable through a
     * container whose driver config had been cleared.
     */
    public static String toConfigString(Map<String, String> config) {
        final StringBuilder builder = new StringBuilder();
        for (Map.Entry<String, String> entry : config.entrySet()) {
            if (builder.length() > 0) builder.append(';');
            builder.append(entry.getKey()).append('=').append(entry.getValue());
        }
        return builder.toString();
    }

    public static String getVersion(String graphicsDriverConfig) {
        return parse(graphicsDriverConfig).get("version");
    }

    public static String getExtensionsBlacklist(String graphicsDriverConfig) {
        return parse(graphicsDriverConfig).get("blacklistedExtensions");
    }
}
