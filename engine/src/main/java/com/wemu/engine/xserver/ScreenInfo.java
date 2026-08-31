package com.wemu.engine.xserver;

/**
 * The size of the root window.
 *
 * Stored as {@code short} because that is the width of every coordinate in the
 * X protocol, and every consumer of these fields writes them into a reply. The
 * constructors are where a value that cannot survive that is caught, rather
 * than at the point where a truncated number has already been sent to a guest.
 */
public class ScreenInfo {
    /** The X protocol carries dimensions as CARD16, so this is the ceiling. */
    private static final int MAX_DIMENSION = Short.MAX_VALUE;

    public final short width;
    public final short height;

    public ScreenInfo(String value) {
        String[] parts = value == null ? new String[0] : value.split("x");
        if (parts.length != 2) {
            throw new IllegalArgumentException(
                    "A screen size must be written as WIDTHxHEIGHT, not \"" + value + "\"");
        }
        int parsedWidth;
        int parsedHeight;
        try {
            parsedWidth = Integer.parseInt(parts[0].trim());
            parsedHeight = Integer.parseInt(parts[1].trim());
        } catch (NumberFormatException failure) {
            throw new IllegalArgumentException(
                    "A screen size must be two whole numbers, not \"" + value + "\"", failure);
        }
        this.width = checked(parsedWidth, "width");
        this.height = checked(parsedHeight, "height");
    }

    public ScreenInfo(int width, int height) {
        this.width = checked(width, "width");
        this.height = checked(height, "height");
    }

    /**
     * Refuses a dimension the protocol cannot carry.
     *
     * The cast to {@code short} used to be silent, so a root window wider than
     * 32767 became a small or negative number and every reply built from it
     * described a screen that does not exist. A container is configured once and
     * used for months; failing at the point of configuration is recoverable, and
     * failing later is a guest that renders to nowhere with nothing to explain
     * why.
     */
    private static short checked(int value, String what) {
        if (value <= 0 || value > MAX_DIMENSION) {
            throw new IllegalArgumentException(
                    "Screen " + what + " " + value + " is outside 1.." + MAX_DIMENSION);
        }
        return (short) value;
    }

    /**
     * Physical size, in the millimetres RandR reports and guests divide by.
     *
     * Never zero. A guest computes DPI as pixels over millimetres, so a zero
     * here is a division by zero in someone else's process -- which is why
     * {@link ScreenLayout.Monitor} has always guarded it. This is the same
     * guard, in the other place that answers the same question.
     */
    public short getWidthInMillimeters() {
        return (short) Math.max(1, width / 10);
    }

    public short getHeightInMillimeters() {
        return (short) Math.max(1, height / 10);
    }

    @Override
    public String toString() {
        return width+"x"+height;
    }
}
