package com.wemu.engine;

import android.content.Context;

import com.wemu.engine.widget.InputControlsView;
import com.wemu.engine.widget.XServerView;
import com.wemu.engine.xserver.XServer;

/**
 * What the engine needs from whatever is hosting a session.
 *
 * `WinHandler` and the controller layer used to hold an `XServerDisplayActivity`
 * outright, which made two things true that should not have been: the engine
 * depended on a specific screen, and that screen could not be replaced without
 * touching the engine. Between them they called four methods on it.
 *
 * Four methods is an interface. The host is now `:app`'s business — in this fork
 * a Compose activity that puts the game on one panel and its console on the
 * other — and the engine neither knows nor cares which.
 */
public interface XServerHost {

    /**
     * The host's context, for system services and preferences.
     *
     * The engine reads shared preferences and takes the vibrator and input
     * managers off this. It must be a context that outlives a single frame; an
     * activity or the application will both do.
     */
    Context getHostContext();

    /** The running X server. */
    XServer getXServer();

    /** The surface the server presents into. */
    XServerView getXServerView();

    /**
     * The touch-control overlay, or null when the host is not showing one.
     *
     * Nullable on purpose. The original's activity always had one because the view
     * was part of its layout whether or not it was visible; a host that puts its
     * controls on a second screen — or has no touch controls at all because a
     * gamepad is attached — genuinely has none, and the engine has to cope with
     * that rather than be handed an empty view to keep it quiet.
     */
    InputControlsView getInputControlsView();
}
