package com.wemu.engine.session;

import com.wemu.engine.core.KeyValueSet;
import com.wemu.engine.core.WineRegistryEditor;

import java.util.Locale;

/**
 * WEMU-owned compatibility policy for Skyrim's post-intro audio mixer.
 *
 * <p>The logo movies can play without the legacy DirectX audio runtime, but
 * Skyrim SE moves to XAudio 2.7/XACT when the main game initializes.  A plain
 * Wine prefix therefore has the misleading failure mode where the splash
 * screens have sound and the loaded game is silent.  WEMU already ships the
 * matching DirectX payload as its {@code xaudio} Windows component; this policy
 * selects it automatically without asking the player to alter a container.</p>
 */
public final class SkyrimAudioCompatibility {
    public static final String AUDIO_DRIVER = "alsa";
    public static final String PROVISIONING_REVISION = "2";

    private static final String[] EXECUTABLES = {
            "SkyrimSE.exe",
            "SkyrimSELauncher.exe",
            "skse64_loader.exe"
    };

    private static final String[] NATIVE_AUDIO_DLLS = {
            "xaudio2_7",
            "x3daudio1_7",
            "xapofx1_5"
    };

    private SkyrimAudioCompatibility() {}

    public static boolean matches(String... values) {
        if (values == null) return false;
        StringBuilder identity = new StringBuilder();
        for (String value : values) {
            if (value != null && !value.isEmpty()) identity.append(' ').append(value);
        }
        String normalized = identity.toString().toLowerCase(Locale.ROOT);
        return normalized.contains("skyrim")
                || normalized.contains("elder scrolls v")
                || normalized.contains("tesv")
                || normalized.contains("skse64_loader");
    }

    /** Returns the configured component set with native XAudio/XACT enabled. */
    public static String enableRequiredComponents(String configured) {
        KeyValueSet components = new KeyValueSet(configured);
        components.put("xaudio", "1");
        return components.toString();
    }

    /**
     * Selects the DirectX June 2010 audio runtime for Skyrim only.
     *
     * <p>This is deliberately written for every prepared prefix rather than
     * only for a shortcut whose name contains "Skyrim". A common WEMU workflow
     * is to open a generic container and start Skyrim from Wine's file manager;
     * by the time WM_CLASS identifies the game, its XAudio process has already
     * initialized and it is too late to change DLL policy. Wine AppDefaults
     * keep the native selection scoped to these executables, so other games in
     * the same prefix retain their configured audio runtime.</p>
     */
    public static void applyExecutableOverrides(WineRegistryEditor registryEditor) {
        if (registryEditor == null) return;
        for (String executable : EXECUTABLES) {
            String key = "Software\\Wine\\AppDefaults\\" + executable + "\\DllOverrides";
            for (String dll : NATIVE_AUDIO_DLLS) {
                registryEditor.setStringValue(key, dll, "native,builtin");
            }
        }
    }
}
