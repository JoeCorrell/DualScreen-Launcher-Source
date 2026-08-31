package com.thor.core.model

import kotlinx.serialization.Serializable

/**
 * How the app spreads itself across the hardware it finds.
 *
 * The same four answers Loki gives, because they are answers about the *device*
 * rather than about a launcher: an AYN Thor has two panels, a dock has one big
 * one, and a phone has neither. Naming them identically is what lets a user who
 * has set up Loki understand this app without reading anything.
 *
 * What differs is what each surface holds. In Loki the second surface is a
 * detail panel; here it is the session console — the HUD and everything that
 * used to be a drawer sliding over the game.
 */
@Serializable
enum class DualScreenMode(val label: String) {
    AUTO("Automatic"),

    /** Force use of a secondary Display via a Presentation. */
    DUAL_DISPLAY("Dual display"),

    /** Split one display into a top and bottom half. */
    SPLIT_SINGLE("Split single display"),

    /**
     * Bottom surface only; the second surface becomes an overlay sheet.
     *
     * The only mode in which anything is ever drawn over a running game, and it
     * exists because a device with one screen leaves no alternative.
     */
    SINGLE("Single screen"),

    /**
     * The top screen alone, with the bottom panel dark.
     *
     * For the device sitting in a dock with a controller in your hands rather
     * than the device itself. The bottom panel is under the dock, or facing the
     * ceiling, or simply not where you are looking.
     */
    COUCH("Couch mode"),
}

/**
 * Which panel a running game is drawn on.
 *
 * Separate from [DualScreenMode] because it is a different question. The mode
 * describes the hardware; this describes a preference about it, and the two are
 * only related in that most of this enum means nothing when there is one screen.
 */
@Serializable
enum class GameScreenPlacement(val label: String, val description: String) {
    /**
     * The game on the large panel, the console on the small one.
     *
     * The default, and the reason this app exists in the shape it does: nothing
     * is ever drawn over the game. The original's own drawer slides in over the
     * picture and takes a third of it with it, which on a 2716×1220 panel means
     * losing the part of the frame you were looking at in order to check the
     * frame rate.
     */
    PRIMARY("Game on main screen", "Console and HUD on the second panel"),

    /**
     * The game on the small panel, the console on the large one.
     *
     * For a device in a dock driving an external display, where the large panel
     * is the one you are not holding.
     */
    SECONDARY("Game on second screen", "Console and HUD on the main panel"),
}

/**
 * A page of the session console — what the second screen is showing.
 *
 * These are the original's own drawer sections, kept as they were because they are a
 * reasonable division of the subject and because a user coming from the original
 * should not have to relearn where anything is. What changed is that they are no
 * longer mutually exclusive with seeing the game.
 */
@Serializable
enum class ConsolePage(val label: String) {
    /** Frame rate, CPU, GPU, memory, battery and thermals. */
    STATS("Stats"),

    /**
     * The container's programs, and the means to start one.
     *
     * This is what replaced `wfm.exe`. The original's file manager is a Win32 window
     * drawn by Wine's classic control set, so an opened container looked like
     * 1995 no matter what the palette said. The window is no longer drawn; this
     * page does its job in this app's own interface, on the panel that is not
     * showing the game — which is also better than the thing it replaced, since
     * you can browse for the next program without covering the current one.
     */
    PROGRAMS("Programs"),

    /** Touch controls, controller bindings, the on-screen keyboard. */
    INPUT("Input"),

    /** Pointer capture, relative motion, sensitivity. */
    MOUSE("Mouse"),

    /** Renderer, frame generation, shader and driver options. */
    GRAPHICS("Graphics"),

    /** Resolution, stretch, filtering, the magnifier. */
    SCREEN("Screen"),

    /** Running Wine processes, with the means to end one. */
    PROCESSES("Processes"),

    /**
     * What the running game's own mod puts on the other screen.
     *
     * The only page here that is not about the session. Every other one
     * describes the machinery — frame rate, drivers, processes — and this one
     * lets the game describe itself, through a mod that opens a socket and says
     * what it is.
     *
     * It belongs on the console rather than in an overlay for the same reason
     * everything else does, and more so: a journal or a calendar is precisely
     * the thing people pause a game to look at, and the second panel means not
     * pausing.
     */
    MODS("Mods"),

    /**
     * What the guest printed, and the preflight that ran before it.
     *
     * Not one of the original's own sections — it kept this behind a settings toggle
     * and a dialog. On a handheld nobody can attach `adb` to, a container that
     * closes on launch is undiagnosable without it.
     */
    LOGS("Log"),
}
