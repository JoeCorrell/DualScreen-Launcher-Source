package com.wemu.engine.xserver.requests;

import com.wemu.engine.xserver.WindowManager;
import com.wemu.engine.xserver.errors.BadWindow;

import java.util.function.IntFunction;

/** Resolves the special window ids accepted by the core SetInputFocus request. */
final class InputFocusTargetResolver {
    /** X11's protocol-level None value. */
    static final int NONE = 0;

    /** X11's protocol-level PointerRoot value, not the resource id of the root window. */
    static final int POINTER_ROOT = WindowManager.FOCUS_POINTER_ROOT;

    private InputFocusTargetResolver() {}

    static <T> T resolve(int windowId, T rootWindow, IntFunction<T> windowLookup)
            throws BadWindow {
        if (windowId == NONE) return null;
        if (windowId == POINTER_ROOT) return rootWindow;

        T window = windowLookup.apply(windowId);
        if (window == null) throw new BadWindow(windowId);
        return window;
    }
}
