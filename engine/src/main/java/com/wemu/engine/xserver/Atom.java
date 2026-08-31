package com.wemu.engine.xserver;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;

/**
 * The X atom table: a two-way mapping between an atom's name and its id.
 *
 * Lookup by name is backed by a hash map rather than a scan of the list. The
 * scan was O(n) with a string compare per entry, under this class's global
 * lock, and the predefined atoms most worth finding sit at the far end of it —
 * {@code WM_CLASS} is the second-to-last of sixty-seven. The renderers ask for
 * it once per window per frame, so at 60fps with a handful of windows that was
 * thousands of string comparisons a second spent arriving at a constant.
 *
 * The list stays as the id-to-name direction, because an atom id <em>is</em> an
 * index into it and the protocol depends on that.
 */
public abstract class Atom {
    private static final ArrayList<String> atoms = new ArrayList<>(Arrays.asList(null, "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL", "COLORMAP", "CURSOR", "CUT_BUFFER0", "CUT_BUFFER1", "CUT_BUFFER2", "CUT_BUFFER3", "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6", "CUT_BUFFER7", "DRAWABLE", "FONT", "INTEGER", "PIXMAP", "POINT", "RECTANGLE", "RESOURCE_MANAGER", "RGB_COLOR_MAP", "RGB_BEST_MAP", "RGB_BLUE_MAP", "RGB_DEFAULT_MAP", "RGB_GRAY_MAP", "RGB_GREEN_MAP", "RGB_RED_MAP", "STRING", "VISUALID", "WINDOW", "WM_COMMAND", "WM_HINTS", "WM_CLIENT_MACHINE", "WM_ICON_NAME", "WM_ICON_SIZE", "WM_NAME", "WM_NORMAL_HINTS", "WM_SIZE_HINTS", "WM_ZOOM_HINTS", "MIN_SPACE", "NORM_SPACE", "MAX_SPACE", "END_SPACE", "SUPERSC.LPT_X", "SUPERSC.LPT_Y", "SUBSC.LPT_X", "SUBSC.LPT_Y", "UNDERLINE_POSITION", "UNDERLINE_THICKNESS", "STRIKEOUT_ASCENT", "STRIKEOUT_DESCENT", "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH", "WEIGHT", "POINT_SIZE", "RESOLUTION", "COPYRIGHT", "NOTICE", "FONT_NAME", "FAMILY_NAME", "FULL_NAME", "CAP_HEIGHT", "WM_CLASS", "WM_TRANSIENT_FOR"));

    private static final HashMap<String, Integer> idsByName = new HashMap<>(atoms.size() * 2);

    static {
        /*
         * putIfAbsent, not put: the scan this replaces returned the lowest
         * matching index, so a duplicate name added later must not take the
         * earlier one's id away from it.
         */
        for (int i = 0; i < atoms.size(); i++) {
            String name = atoms.get(i);
            if (name != null) idsByName.putIfAbsent(name, i);
        }
    }

    public synchronized static String getName(int id) {
        return atoms.get(id);
    }

    public synchronized static int getId(String name) {
        if (name == null) return 0;
        Integer id = idsByName.get(name);
        return id != null ? id : -1;
    }

    public synchronized static int internAtom(String name) {
        int id = getId(name);
        if (id == -1) {
            id = atoms.size();
            atoms.add(name);
            idsByName.putIfAbsent(name, id);
        }
        return id;
    }

    public synchronized static boolean isValid(int id) {
        return id > 0 && id < atoms.size();
    }
}
