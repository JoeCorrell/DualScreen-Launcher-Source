package com.wemu.engine.box64;

import androidx.annotation.NonNull;

public class Box64Preset {
    public static final String STABILITY = "STABILITY";
    public static final String COMPATIBILITY = "COMPATIBILITY";
    public static final String INTERMEDIATE = "INTERMEDIATE";
    public static final String PERFORMANCE = "PERFORMANCE";

    /**
     * WinHub's Mali-oriented performance profile.
     *
     * This is deliberately separate from PERFORMANCE: disabling safe flags and
     * weakening memory barriers can improve CPU-bound games on some ARM SoCs,
     * but it is a correctness trade-off and must remain opt-in.
     */
    public static final String PERFORMANCE_MALI = "PERFORMANCE_MALI";

    /**
     * WinHub's most aggressive general-purpose dynarec profile.
     *
     * It keeps the normal performance settings and opts into weak barriers and
     * dirty-page tracking. Games which crash or corrupt state should use the
     * regular Performance preset instead.
     */
    public static final String EXTREME = "EXTREME";

    /**
     * Performance, retuned for a Unity game.
     *
     * Not a marketing label — Unity's player and its Mono runtime have two
     * habits the general preset gets wrong. It takes AVX2 paths, which is why
     * this is the only preset that asks for `BOX64_AVX=2`; and Mono's threading
     * needs the recompiler to respect x86 memory ordering, so `STRONGMEM` goes
     * back to 1 where Performance turns it off.
     */
    public static final String UNITY = "UNITY";

    /**
     * The same, minus big blocks.
     *
     * Unity's "Mono bleeding edge" runtime generates and patches code at run
     * time, and a recompiler that has stitched a large block together has to
     * throw the whole thing away each time. `BIGBLOCK=0` costs throughput and
     * wins it back on the games that spend their time in the JIT.
     */
    public static final String UNITY_MONO_BLEEDING_EDGE = "UNITY_MONO_BLEEDING_EDGE";

    /**
     * For a game wrapped in Denuvo.
     *
     * The DRM checks itself by reading its own code and timing what happens, so
     * this is the strictest memory ordering Box64 has plus a long forward scan
     * — slow everywhere else and the only thing some titles will start under.
     */
    public static final String DENUVO = "DENUVO";

    public static final String CUSTOM = "CUSTOM";
    public final String id;
    public final String name;

    public Box64Preset(String id, String name) {
        this.id = id;
        this.name = name;
    }

    public boolean isCustom() {
        return id.startsWith(CUSTOM);
    }

    @NonNull
    @Override
    public String toString() {
        return name;
    }
}
