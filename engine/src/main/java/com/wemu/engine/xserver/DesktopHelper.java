package com.wemu.engine.xserver;

import androidx.collection.ArrayMap;

import java.util.Map;

public abstract class DesktopHelper {
    public static void attachTo(final XServer xServer) {
        setupXResources(xServer);

        xServer.pointer.addOnPointerMotionListener(new Pointer.OnPointerMotionListener() {
            @Override
            public void onPointerButtonPress(Pointer.Button button) {
                updateFocusedWindow(xServer);
            }
        });
    }

    private static void updateFocusedWindow(XServer xServer) {
        try (XLock lock = xServer.lock(XServer.Lockable.WINDOW_MANAGER, XServer.Lockable.INPUT_DEVICE)) {
            Window focusedWindow = xServer.windowManager.getFocusedWindow();
            Window child = xServer.windowManager.findPointWindow(xServer.pointer.getClampedX(), xServer.pointer.getClampedY());
            if (child == null && focusedWindow != xServer.windowManager.rootWindow) {
                xServer.windowManager.setFocus(xServer.windowManager.rootWindow, WindowManager.FocusRevertTo.NONE);
            }
            else if (child != null && child != focusedWindow) {
                setFocusedWindow(xServer, child);
            }
        }
    }

    /** Gives a clicked Wine application both X input focus and Win32 foreground state. */
    private static void setFocusedWindow(XServer xServer, Window window) {
        if (!window.isApplicationWindow()) return;

        boolean parentIsRoot = window.getParent() == xServer.windowManager.rootWindow;
        xServer.windowManager.setFocus(
                window,
                parentIsRoot
                        ? WindowManager.FocusRevertTo.POINTER_ROOT
                        : WindowManager.FocusRevertTo.PARENT);
        xServer.getWinHandler().bringToFront(window.getClassName(), window.getHandle());
    }

    /**
     * WFC's final DRI3 client may be mapped after Win32 already reports focus.
     * Repair the X11 half of that transition using the same foreground path as
     * a real click, without injecting mouse events or changing controller code.
     * Called off the render thread; never acquire X locks inside a GPU submit.
     */
    public static void recoverWfcInputFocus(XServer xServer, Window presented) {
        if (xServer == null || presented == null || xServer.getWinHandler() == null
                || !com.wemu.engine.renderer.SecondScreenOutput.isTransformersWfcPanelActive()) return;
        try (XLock ignored = xServer.lock(XServer.Lockable.WINDOW_MANAGER,
                XServer.Lockable.INPUT_DEVICE)) {
            if (xServer.windowManager.getWindow(presented.id) != presented) return;
            Window target = null;
            boolean wfc = false;
            int remaining = 64;
            for (Window item = presented; item != null
                    && item != xServer.windowManager.rootWindow && remaining-- > 0;
                    item = item.getParent()) {
                String cls = item.getClassName();
                if (cls != null && cls.trim().equalsIgnoreCase("twfc.exe")) wfc = true;
                if (target == null && item.isApplicationWindow() && item.getHandle() != 0) {
                    target = item;
                }
            }
            if (!wfc || target == null) return;
            Window current = xServer.windowManager.getFocusedWindow();
            boolean related = current != null && (current == target
                    || target.isAncestorOf(current));
            boolean root = current == null || current == xServer.windowManager.rootWindow;
            boolean lower = false;
            remaining = 64;
            for (Window item = current; item != null
                    && item != xServer.windowManager.rootWindow && remaining-- > 0;
                    item = item.getParent()) {
                if (com.wemu.engine.renderer.SecondScreenOutput.isTransformersWfcPanelWindow(
                        item.getClassName(), item.getName())) { lower = true; break; }
            }
            boolean viewable = target.isApplicationWindow() && target.getHandle() != 0
                    && presented.getMapState() == Window.MapState.VIEWABLE;
            if (!WfcFocusRecoveryPolicy.shouldRecover(wfc, viewable, related, root,
                    current != null && current.getMapState() == Window.MapState.VIEWABLE,
                    lower, xServer.grabManager.getWindow() != null)) return;
            int previous = xServer.windowManager.getProtocolFocusId();
            setFocusedWindow(xServer, target);
            com.wemu.engine.core.ProcessHelper.emitDiagnostic(
                    "WFC focus recovered: previous=" + previous + " target=" + target.id
                            + " drawable=" + presented.id + " hwnd=" + target.getHandle());
        }
    }
    
    private static void setupXResources(XServer xServer) {
        int atom = Atom.getId("RESOURCE_MANAGER");
        int type = Atom.getId("STRING");

        ArrayMap<String, String> values = new ArrayMap<>();
        values.put("size", "20");
        values.put("theme", "dmz");
        values.put("theme_core", "true");

        StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, String> entry : values.entrySet()) {
            sb.append("Xcursor")
              .append('.')
              .append(entry.getKey())
              .append(':')
              .append('\t')
              .append(entry.getValue())
              .append('\n');
        }

        byte[] data = sb.toString().getBytes(XServer.LATIN1_CHARSET);
        xServer.windowManager.rootWindow.modifyProperty(atom, type, Property.Format.BYTE_ARRAY, Property.Mode.APPEND, data);
    }
}
