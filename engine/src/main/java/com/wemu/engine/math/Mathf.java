package com.wemu.engine.math;

public abstract class Mathf {
    /*
     * An empty range answers its lower bound, not its upper one.
     *
     * `clamp(x, 0, width - 1)` is the shape every caller uses, and a zero-width
     * surface turns that into `clamp(x, 0, -1)`. Testing `x > max` first
     * returned -1 -- a coordinate outside the surface, from a function whose
     * whole purpose is to keep coordinates inside it. Callers then computed
     * sizes from it and indexed backwards off the start of a buffer.
     *
     * Checking the range first means an impossible range answers the only value
     * that cannot be wrong for it.
     */
    public static float clamp(float x, float min, float max) {
        if (max < min) return min;
        return (x < min) ? min : ((x > max) ? max : x);
    }

    public static int clamp(int x, int min, int max) {
        if (max < min) return min;
        return (x < min) ? min : (x > max ? max : x);
    }

    public static float roundTo(float x, float step) {
        return (float)(Math.floor(x / step) * step);
    }

    public static int roundPoint(float x) {
        return (int)(x <= 0 ? Math.floor(x) : Math.ceil(x));
    }

    public static byte sign(float x) {
        return (byte)(x < 0 ? -1 : (x > 0 ? 1 : 0));
    }

    public static float lengthSq(float x, float y) {
        return x * x + y * y;
    }
}
