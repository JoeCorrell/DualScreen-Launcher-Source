package com.wemu.runtime;

import com.wemu.engine.container.Container;
import com.wemu.engine.container.Shortcut;
import com.wemu.engine.core.WineInfo;
import com.wemu.engine.renderer.XServerRenderer;
import com.wemu.engine.winhandler.WinHandler;
import com.wemu.engine.xserver.XServer;

/**
 * One running Windows environment, independent of the backend that hosts it.
 *
 * The inherited types on this first contract — {@link XServer},
 * {@link WinHandler}, {@link Container}, {@link XServerRenderer} — are bridge
 * types, and are expected to leave it. Keeping them here lets the app move to a
 * Wemu-owned session lifecycle now, before the X server, the renderer and the
 * container model are replaced underneath it. Nothing outside {@code :engine}
 * should name an implementation of this interface.
 *
 * The call order is fixed and not defensive about it: {@link #prepare()} builds
 * the server, the host then creates the surface that draws it, and only then may
 * {@link #start()} run. Calling {@code start} first is a programming error.
 */
public interface RuntimeSession {

    void setTerminationListener(RuntimeTerminationListener listener);

    XServer getXServer();

    WinHandler getWinHandler();

    boolean isRunning();

    Container getContainer();

    Shortcut getShortcut();

    /** The resolved Wine build, for a host that wants to check it exists. */
    WineInfo getWineInfo();

    /**
     * Which renderer this session needs, resolved shortcut-then-container.
     *
     * Read by the host before the view exists, because the choice decides how
     * every X drawable is allocated and that has to be settled first.
     */
    String getDisplayRenderer();

    /** Puts the session's stored renderer settings onto a live renderer. */
    void configureRenderer(XServerRenderer renderer);

    /** Applies an opt-in gain to this session's game-audio sink; zero is unity. */
    void setAudioGainDb(float gainDb);

    /** Enables WEMU's self-contained Skyrim XAudio/XACT compatibility path. */
    void setSkyrimAudioCompatibility(boolean enabled);

    /**
     * Starts the Wine desktop before the configured shortcut is submitted by the host.
     *
     * The shortcut remains attached to the session so its input, renderer, environment,
     * compatibility and executable identity are still applied during {@link #prepare()}.
     */
    void setShortcutLaunchDeferred(boolean deferred);

    /** Reads configuration and builds the X server. Must precede {@link #start()}. */
    XServer prepare();

    /** Starts the guest process tree. Does real work; never call it on the main thread. */
    void start();

    void stop();
}
