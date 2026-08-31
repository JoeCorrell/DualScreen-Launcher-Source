package com.wemu.engine.core;

/** Steam app identity shared by the engine launcher and its unit tests. */
public final class SteamLaunchIdentity {

    private SteamLaunchIdentity() {}

    /**
     * Validates and publishes a Steam app id, returning its canonical value.
     *
     * Steam app ids are unsigned 32-bit values. Invalid shortcut metadata is
     * ignored rather than replacing the launcher's harmless non-Steam default.
     */
    public static String apply(EnvVars env, String rawAppId) {
        if (env == null || rawAppId == null) return null;
        String trimmed = rawAppId.trim();
        if (trimmed.isEmpty()) return null;

        try {
            long appId = Long.parseLong(trimmed);
            if (appId <= 0 || appId > 0xffffffffL) return null;
            String value = Long.toString(appId);
            env.put("SteamAppId", value);
            env.put("SteamGameId", value);
            return value;
        } catch (NumberFormatException invalid) {
            return null;
        }
    }
}
