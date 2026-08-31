package com.wemu.engine.core;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.DisplayMetrics;

import androidx.preference.PreferenceManager;

import com.wemu.engine.xenvironment.ImageFs;
import com.wemu.engine.xserver.ScreenInfo;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public abstract class WineThemeManager {
    public enum Theme {LIGHT, DARK}
    public enum BackgroundType {IMAGE, COLOR}
    public static final String DEFAULT_DESKTOP_THEME = Theme.DARK+","+BackgroundType.IMAGE+",#0277bd";

    /**
     * Bumped whenever the generated wallpaper changes.
     *
     * The theme is only re-applied when its recorded key differs, which is
     * correct — redrawing a wallpaper on every launch is pointless work — but it
     * also means a change to the *generator* reaches no container that has
     * already booted. Folding this into the key gives every existing container
     * exactly one redraw and then goes quiet again.
     */
    public static final int WALLPAPER_REVISION = 4;

    /**
     * Bumped whenever the window metrics change.
     *
     * Its own number rather than the wallpaper's, because the two answer
     * different questions and a metrics change that reused the wallpaper's
     * revision would redraw every container's background to move a menu bar.
     */
    public static final int METRICS_REVISION = 1;

    public static class ThemeInfo {
        public final Theme theme;
        public final BackgroundType backgroundType;
        public final int backgroundColor;

        public ThemeInfo(String value) {
            String[] values = value.split(",");
            theme = Theme.valueOf(values[0]);
            if (values.length < 3) {
                backgroundColor = Color.parseColor(values[1]);
                backgroundType = BackgroundType.IMAGE;
            }
            else {
                backgroundType = BackgroundType.valueOf(values[1]);
                backgroundColor = Color.parseColor(values[2]);
            }
        }
    }

    public static void apply(Context context, ThemeInfo themeInfo, ScreenInfo screenInfo, int dpi) {
        apply(context, themeInfo, screenInfo, dpi, false);
    }

    /** Applies the visual theme without ever mutating a shared Wine package. */
    public static void apply(Context context, ThemeInfo themeInfo, ScreenInfo screenInfo, int dpi,
                             boolean preserveWineRuntime) {
        File rootDir = ImageFs.find(context).getRootDir();
        File userRegFile = new File(rootDir, ImageFs.WINEPREFIX+"/user.reg");
        String background = Color.red(themeInfo.backgroundColor)+" "+Color.green(themeInfo.backgroundColor)+" "+Color.blue(themeInfo.backgroundColor);

        if (themeInfo.backgroundType == BackgroundType.IMAGE) {
            createWallpaperBMPFile(context, screenInfo, themeInfo.backgroundColor);
        }

        try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
            if (themeInfo.backgroundType == BackgroundType.IMAGE) {
                registryEditor.setStringValue("Control Panel\\Desktop", "Wallpaper", ImageFs.CACHE_PATH+"/wallpaper.bmp");
            }
            else registryEditor.removeValue("Control Panel\\Desktop", "Wallpaper");

            GuestPalette palette = GuestPalette.read(context);
            WineVisualStyle.Style style = styleFor(palette, themeInfo, dpi);
            applyVisualStyle(context, registryEditor, style);

            /*
             * The icons, from the same colours.
             *
             * Outside the style because a visual style cannot carry them: it
             * says how a control is drawn, and a folder is not a control. What
             * the file manager shows is bitmaps compiled into `shell32` and into
             * the manager itself, and until those are repainted a container
             * themed down to the caption gradient still opens onto manila
             * folders and glossy blue drives.
             */
            if (preserveWineRuntime) WineIconTheme.applyPrefixOnly(context, style);
            else WineIconTheme.apply(context, style);

            applyWindowMetrics(registryEditor);
            applyFontRendering(registryEditor);

            /*
             * The palette is written whether or not the style loaded.
             *
             * These values reach everything uxtheme does not paint — an
             * application's own client area, anything drawn with `GetSysColor`
             * — and they are the entire appearance if the style fails to open.
             * That used to be a conflict, because the style was Wine's own light
             * one and a dark palette underneath it produced black text on black
             * panels next to light buttons. It is not a conflict now: the style
             * is generated from this same palette, so the two agree by
             * construction and whichever ends up drawing is right.
             */
            if (palette != null) {
                applyPaletteColors(registryEditor, palette, background);
            }
            else if (themeInfo.theme == Theme.LIGHT) {
                registryEditor.setStringValue("Control Panel\\Colors", "ActiveBorder", "245 245 245");
                registryEditor.setStringValue("Control Panel\\Colors", "ActiveTitle", "96 125 139");
                registryEditor.setStringValue("Control Panel\\Colors", "Background", background);
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonAlternateFace", "245 245 245");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonDkShadow", "158 158 158");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonFace", "245 245 245");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonHilight", "224 224 224");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonLight", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonShadow", "158 158 158");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonText", "0 0 0");
                registryEditor.setStringValue("Control Panel\\Colors", "GradientActiveTitle", "96 125 139");
                registryEditor.setStringValue("Control Panel\\Colors", "GradientInactiveTitle", "117 117 117");
                registryEditor.setStringValue("Control Panel\\Colors", "GrayText", "158 158 158");
                registryEditor.setStringValue("Control Panel\\Colors", "Hilight", "2 136 209");
                registryEditor.setStringValue("Control Panel\\Colors", "HilightText", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "HotTrackingColor", "2 136 209");
                registryEditor.setStringValue("Control Panel\\Colors", "InactiveBorder", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "InactiveTitle", "117 117 117");
                registryEditor.setStringValue("Control Panel\\Colors", "InactiveTitleText", "200 200 200");
                registryEditor.setStringValue("Control Panel\\Colors", "InfoText", "0 0 0");
                registryEditor.setStringValue("Control Panel\\Colors", "InfoWindow", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "Menu", "245 245 245");
                registryEditor.setStringValue("Control Panel\\Colors", "MenuBar", "245 245 245");
                registryEditor.setStringValue("Control Panel\\Colors", "MenuHilight", "2 136 209");
                registryEditor.setStringValue("Control Panel\\Colors", "MenuText", "0 0 0");
                registryEditor.setStringValue("Control Panel\\Colors", "Scrollbar", "245 245 245");
                registryEditor.setStringValue("Control Panel\\Colors", "TitleText", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "Window", "245 245 245");
                registryEditor.setStringValue("Control Panel\\Colors", "WindowFrame", "158 158 158");
                registryEditor.setStringValue("Control Panel\\Colors", "WindowText", "0 0 0");
            }
            else if (themeInfo.theme == Theme.DARK) {
                registryEditor.setStringValue("Control Panel\\Colors", "ActiveBorder", "48 48 48");
                registryEditor.setStringValue("Control Panel\\Colors", "ActiveTitle", "33 33 33");
                registryEditor.setStringValue("Control Panel\\Colors", "Background", background);
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonAlternateFace", "33 33 33");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonDkShadow", "0 0 0");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonFace", "33 33 33");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonHilight", "48 48 48");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonLight", "48 48 48");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonShadow", "0 0 0");
                registryEditor.setStringValue("Control Panel\\Colors", "ButtonText", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "GradientActiveTitle", "33 33 33");
                registryEditor.setStringValue("Control Panel\\Colors", "GradientInactiveTitle", "33 33 33");
                registryEditor.setStringValue("Control Panel\\Colors", "GrayText", "117 117 117");
                registryEditor.setStringValue("Control Panel\\Colors", "Hilight", "2 136 209");
                registryEditor.setStringValue("Control Panel\\Colors", "HilightText", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "HotTrackingColor", "2 136 209");
                registryEditor.setStringValue("Control Panel\\Colors", "InactiveBorder", "48 48 48");
                registryEditor.setStringValue("Control Panel\\Colors", "InactiveTitle", "33 33 33");
                registryEditor.setStringValue("Control Panel\\Colors", "InactiveTitleText", "117 117 117");
                registryEditor.setStringValue("Control Panel\\Colors", "InfoText", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "InfoWindow", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "Menu", "33 33 33");
                registryEditor.setStringValue("Control Panel\\Colors", "MenuBar", "48 48 48");
                registryEditor.setStringValue("Control Panel\\Colors", "MenuHilight", "2 136 209");
                registryEditor.setStringValue("Control Panel\\Colors", "MenuText", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "Scrollbar", "48 48 48");
                registryEditor.setStringValue("Control Panel\\Colors", "TitleText", "255 255 255");
                registryEditor.setStringValue("Control Panel\\Colors", "Window", "48 48 48");
                registryEditor.setStringValue("Control Panel\\Colors", "WindowFrame", "0 0 0");
                registryEditor.setStringValue("Control Panel\\Colors", "WindowText", "255 255 255");
            }
        }
    }

    /**
     * The launcher's palette, as the app left it for us.
     *
     * The engine cannot resolve an OKLCH theme — that lives in the app, in
     * Kotlin, behind Compose — so the app flattens the handful of colours the
     * guest desktop needs into the shared preferences both sides already use.
     * Absent on a fresh install, in which case the hardcoded tables below still
     * apply and the desktop looks the way the original's always did.
     */
    static final class GuestPalette {
        final int background, surface, surfaceElevated, primary, onSurface, onSurfaceVariant, outline;

        private GuestPalette(SharedPreferences prefs) {
            background = prefs.getInt(KEY_PREFIX + "background", 0);
            surface = prefs.getInt(KEY_PREFIX + "surface", 0);
            surfaceElevated = prefs.getInt(KEY_PREFIX + "surfaceElevated", 0);
            primary = prefs.getInt(KEY_PREFIX + "primary", 0);
            onSurface = prefs.getInt(KEY_PREFIX + "onSurface", 0);
            onSurfaceVariant = prefs.getInt(KEY_PREFIX + "onSurfaceVariant", 0);
            outline = prefs.getInt(KEY_PREFIX + "outline", 0);
        }

        static GuestPalette read(Context context) {
            SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
            if (!prefs.contains(KEY_PREFIX + "background")) return null;
            return new GuestPalette(prefs);
        }

        static final String KEY_PREFIX = "guest_theme_";
    }

    /**
     * A short signature of the launcher's palette, for change detection.
     *
     * The desktop's colours are only rewritten when the container's theme key
     * changes, and the key already carries the rendered wallpaper's timestamp —
     * but a theme can move `surface` and `outline` without moving any colour the
     * wallpaper is drawn from, in which case the file is untouched and the
     * window chrome would keep the previous theme's greys.
     */
    public static int paletteFingerprint(Context context) {
        GuestPalette p = GuestPalette.read(context);
        if (p == null) return 0;
        int hash = p.background;
        hash = 31 * hash + p.surface;
        hash = 31 * hash + p.surfaceElevated;
        hash = 31 * hash + p.primary;
        hash = 31 * hash + p.onSurface;
        hash = 31 * hash + p.onSurfaceVariant;
        hash = 31 * hash + p.outline;
        return hash;
    }

    /** `r g b`, which is the only format the Wine registry takes for these. */
    private static String rgb(int color) {
        return Color.red(color) + " " + Color.green(color) + " " + Color.blue(color);
    }

    /**
     * The whole desktop, coloured from the launcher's theme.
     *
     * The reason this looks like a modern desktop rather than Windows 95 is not
     * the colours — it is that every one of the four bevel slots is set to the
     * same value as the face it borders. Wine's classic renderer draws a raised
     * 3D edge on every button, panel and scrollbar out of `ButtonHilight`,
     * `ButtonLight`, `ButtonShadow` and `ButtonDkShadow`; when they differ from
     * the face you get the chiselled grey of 1995 whatever hue you paint it.
     * Collapse them and the same renderer draws flat surfaces, which is what
     * every desktop made since has looked like.
     *
     * `WindowFrame` and `ActiveBorder` keep a real, quieter contrast, because a
     * window with no edge at all cannot be told from the one behind it.
     */
    private static void applyPaletteColors(
            WineRegistryEditor reg, GuestPalette p, String background) {
        final String key = "Control Panel\\Colors";

        String face = rgb(p.surface);
        String text = rgb(p.onSurface);
        String muted = rgb(p.onSurfaceVariant);
        String accent = rgb(p.primary);
        String edge = rgb(p.outline);

        reg.setStringValue(key, "Background", background);

        // Flat: every bevel slot equals the face it is supposed to be shading.
        reg.setStringValue(key, "ButtonFace", face);
        reg.setStringValue(key, "ButtonAlternateFace", face);
        reg.setStringValue(key, "ButtonHilight", face);
        reg.setStringValue(key, "ButtonLight", face);
        reg.setStringValue(key, "ButtonShadow", face);
        reg.setStringValue(key, "ButtonDkShadow", face);
        reg.setStringValue(key, "ButtonText", text);

        // Title bars in the theme's own surface, not a saturated blue. The
        // active one is a step up the ramp rather than a different colour,
        // which is how a modern window manager distinguishes them.
        reg.setStringValue(key, "ActiveTitle", rgb(p.surfaceElevated));
        reg.setStringValue(key, "GradientActiveTitle", rgb(p.surfaceElevated));
        reg.setStringValue(key, "TitleText", text);
        reg.setStringValue(key, "InactiveTitle", face);
        reg.setStringValue(key, "GradientInactiveTitle", face);
        reg.setStringValue(key, "InactiveTitleText", muted);
        reg.setStringValue(key, "ActiveBorder", edge);
        reg.setStringValue(key, "InactiveBorder", face);
        reg.setStringValue(key, "WindowFrame", edge);

        reg.setStringValue(key, "Window", rgb(p.background));
        reg.setStringValue(key, "WindowText", text);
        reg.setStringValue(key, "GrayText", muted);

        reg.setStringValue(key, "Menu", face);
        reg.setStringValue(key, "MenuBar", face);
        reg.setStringValue(key, "MenuText", text);
        reg.setStringValue(key, "MenuHilight", accent);

        reg.setStringValue(key, "Hilight", accent);
        reg.setStringValue(key, "HilightText", rgb(p.background));
        reg.setStringValue(key, "HotTrackingColor", accent);

        reg.setStringValue(key, "InfoWindow", rgb(p.surfaceElevated));
        reg.setStringValue(key, "InfoText", text);

        reg.setStringValue(key, "Scrollbar", face);
    }

    /**
     * Window furniture, sized like something made this decade.
     *
     * These are the other half of the 1995 look and they are pure numbers, so
     * they cost nothing. Wine stores them in twentieths of a point, negatively
     * signed: -15 is one pixel at 96 DPI.
     *
     *  - A one-pixel border instead of the classic four-pixel raised frame, with
     *    no padded border on top of it.
     *  - A taller title bar, which is what makes a window look like it has a
     *    header rather than a caption strip.
     *  - Narrower scrollbars, because the classic width was sized for a mouse on
     *    a CRT and this is a touchscreen with a trackpad over it.
     */
    /**
     * Generates a visual style in the app's colours and turns it on.
     *
     * This is the thing that replaces the 1995 control set. Without a style,
     * `uxtheme` refuses to open and every control in every Win32 window is drawn
     * by the classic painter, whose bevels *are* the old look — no amount of
     * registry colour work modernises them, because the chisel is the point.
     * The original explicitly switched styling off, clearing `DllName` and writing
     * `ThemeActive=0`, so every button, scrollbar, tab, checkbox and progress bar
     * was coming from that painter.
     *
     * Wine ships a style of its own — `light.msstyles`, 7.5 MB of artwork — and
     * it is the fallback here. But it is fixed, light and blue, and this
     * launcher's theme is generated: the user picks a hue and an OKLCH recipe
     * produces the palette. A shipped style can only ever coincidentally match
     * it. {@link WineVisualStyle} builds one instead, from the same colours the
     * app paints itself with, so the guest desktop matches rather than merely
     * resembles.
     *
     * `ColorName` and `SizeName` have to name a variant the file actually
     * declares. This is worth stating plainly because the value that was here
     * before — `NormalColor`, the near-universal convention — is not one Wine's
     * own style offers: it declares `Blue`, and uxtheme rejects a variant it
     * cannot find rather than falling back to the first. The style was being
     * enabled and then failing to open, every time.
     *
     * @return whether a style was enabled.
     */
    private static boolean applyVisualStyle(
            Context context, WineRegistryEditor reg, WineVisualStyle.Style style) {
        final String key = "Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";

        File rootDir = ImageFs.find(context).getRootDir();
        File generated = new File(rootDir,
                ImageFs.WINEPREFIX + WineVisualStyle.THEME_DIR + "/" + WineVisualStyle.FILE_NAME);

        if (WineVisualStyle.write(generated, style)) {
            reg.setStringValue(key, "DllName", WineVisualStyle.GUEST_PATH);
            reg.setStringValue(key, "ColorName", WineVisualStyle.COLOR_NAME);
            reg.setStringValue(key, "SizeName", WineVisualStyle.SIZE_NAME);
            reg.setStringValue(key, "ThemeActive", "1");
            return true;
        }

        File shipped = new File(rootDir,
                ImageFs.WINEPREFIX + "/drive_c/windows/resources/themes/light/light.msstyles");
        if (shipped.isFile()) {
            reg.setStringValue(key, "DllName",
                    "C:\\windows\\resources\\themes\\light\\light.msstyles");
            reg.setStringValue(key, "ColorName", "Blue");
            reg.setStringValue(key, "SizeName", "NormalSize");
            reg.setStringValue(key, "ThemeActive", "1");
            return true;
        }

        // Classic, but flat and in the app's palette.
        reg.removeValue(key, "DllName");
        reg.setStringValue(key, "ThemeActive", "0");
        return false;
    }

    /**
     * The colours the style is generated from.
     *
     * The launcher's palette when it has published one, which is the case on
     * every install that has opened the app once. The constants are for the
     * window between the image filesystem being installed and the app having
     * resolved a theme — they are Loki's own defaults, so the guest is never
     * momentarily some other product's colours.
     */
    private static WineVisualStyle.Style styleFor(
            GuestPalette palette, ThemeInfo themeInfo, int dpi) {
        if (palette != null) {
            return new WineVisualStyle.Style(
                    palette.background, palette.surface, palette.surfaceElevated,
                    palette.primary, palette.onSurface, palette.onSurfaceVariant,
                    palette.outline, dpi);
        }
        if (themeInfo.theme == Theme.LIGHT) {
            return new WineVisualStyle.Style(
                    0xFFF7F8FA, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF0277BD,
                    0xFF1B1F24, 0xFF5B6570, 0xFFD6DBE1, dpi);
        }
        return new WineVisualStyle.Style(
                0xFF0E1116, 0xFF161B22, 0xFF1F262E, 0xFF4FA3FF,
                0xFFE6EDF3, 0xFF9AA7B4, 0xFF2D3742, dpi);
    }

    /**
     * Antialiased text, which is the other half of looking like this decade.
     *
     * Wine's classic mode renders UI text with no smoothing unless told
     * otherwise, and unsmoothed 8pt type is the single strongest "this is old
     * software" signal there is — stronger than the widgets, because it is in
     * every label, menu and title on screen.
     *
     * Subpixel rather than greyscale: these panels are RGB-striped and the guest
     * is composited without rotation, so the assumption holds. The gamma is
     * Windows' own default; Wine reads it through the same path.
     */
    private static void applyFontRendering(WineRegistryEditor reg) {
        final String key = "Control Panel\\Desktop";
        reg.setStringValue(key, "FontSmoothing", "2");
        reg.setDwordValue(key, "FontSmoothingType", 2);
        reg.setDwordValue(key, "FontSmoothingOrientation", 1);
        reg.setDwordValue(key, "FontSmoothingGamma", 1400);
    }

    /**
     * The metrics, which are most of why a themed container still looks old.
     *
     * The visual style decides how a control is *painted* and these decide how
     * much room it gets. Colour alone never fixes a 1998 interface: what dates
     * the file manager is 16-pixel icons, a 19-pixel menu bar and an 8-point
     * font, and all three are here rather than in the style.
     *
     * <h3>The font</h3>
     *
     * Stored as a {@code LOGFONTW} blob, which is why this needs binary writes
     * at all. Arial because it is what the image actually ships — there is no
     * {@code tahoma.ttf} in the container's font directory, and naming a face
     * that is not there gets a substitution nobody chose. Of what is present it
     * is the closest to a neutral modern interface face; Verdana is wider and
     * Trebuchet has too much character for chrome.
     *
     * <h3>The icons</h3>
     *
     * {@code Shell Small Icon Size} is what the file manager's list asks
     * {@code SHGetFileInfo} for, and at 16 it is both illegible and the reason
     * the rows are as short as they are. Raising it makes the rows taller as a
     * side effect, which is the other half of the problem.
     */
    private static void applyWindowMetrics(WineRegistryEditor reg) {
        final String key = "Control Panel\\Desktop\\WindowMetrics";
        reg.setStringValue(key, "BorderWidth", "-15");
        reg.setStringValue(key, "PaddedBorderWidth", "0");
        // 28 px at 96 DPI, which is what the style's `CaptionBarHeight` says.
        // The two have to agree, or the caption artwork is drawn at one height
        // while the buttons in it are laid out for another.
        reg.setStringValue(key, "CaptionHeight", "-420");
        // 22 px rather than 19: a 10-point menu in a 19-pixel bar has two
        // pixels above the text and one below it.
        reg.setStringValue(key, "MenuHeight", "-330");
        reg.setStringValue(key, "ScrollWidth", "-210");
        reg.setStringValue(key, "ScrollHeight", "-210");

        reg.setStringValue(key, "Shell Icon Size", "32");
        reg.setStringValue(key, "Shell Small Icon Size", "20");

        byte[] ui = logFont(UI_FACE, UI_FONT_HEIGHT, 400);
        reg.setHexValue(key, "MenuFont", ui);
        reg.setHexValue(key, "MessageFont", ui);
        reg.setHexValue(key, "StatusFont", ui);
        reg.setHexValue(key, "IconFont", ui);
        // The caption carries the window's name and is the one piece of text on
        // screen that should be heavier than its surroundings.
        reg.setHexValue(key, "CaptionFont", logFont(UI_FACE, UI_FONT_HEIGHT, 600));
        reg.setHexValue(key, "SmCaptionFont", logFont(UI_FACE, -12, 600));
    }

    /** What the image ships and the chrome is set in; see applyWindowMetrics. */
    private static final String UI_FACE = "Arial";

    /**
     * Ten point at 96 DPI.
     *
     * Negative because a {@code LOGFONT} height is a character height when it is
     * negative and a cell height when it is positive, and every font dialog
     * anyone has ever used means the former.
     */
    private static final int UI_FONT_HEIGHT = -13;

    /**
     * A {@code LOGFONTW}, which the registry holds as 92 raw bytes.
     *
     * Fixed by the structure rather than by choice: five longs, eight bytes of
     * flags, and a 32-character face name that is padded rather than terminated.
     */
    private static byte[] logFont(String face, int height, int weight) {
        ByteBuffer out = ByteBuffer.allocate(92).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(height);
        out.putInt(0);          // lfWidth: let the face pick its own aspect
        out.putInt(0);          // lfEscapement
        out.putInt(0);          // lfOrientation
        out.putInt(weight);
        out.put((byte) 0);      // lfItalic
        out.put((byte) 0);      // lfUnderline
        out.put((byte) 0);      // lfStrikeOut
        out.put((byte) 1);      // lfCharSet: DEFAULT_CHARSET
        out.put((byte) 0);      // lfOutPrecision
        out.put((byte) 0);      // lfClipPrecision
        // CLEARTYPE_QUALITY, to agree with the FontSmoothingType written beside
        // it — asking for subpixel smoothing in one place and leaving the font
        // on default quality in the other is how you get one crisp menu and one
        // fuzzy one.
        out.put((byte) 5);      // lfQuality
        out.put((byte) 0);      // lfPitchAndFamily: DEFAULT_PITCH, FF_DONTCARE
        for (int i = 0; i < 32; i++) out.putChar(i < face.length() ? face.charAt(i) : '\0');
        return out.array();
    }

    private static void createWallpaperBMPFile(Context context, ScreenInfo screenInfo, int accent) {
        final int outputHeight = screenInfo.height;
        int outputWidth = screenInfo.width;

        Bitmap outputBitmap = Bitmap.createBitmap(outputWidth, outputHeight, Bitmap.Config.ARGB_8888);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        Canvas canvas = new Canvas(outputBitmap);

        /*
         * Three sources, in order of how much they were meant for this desktop.
         *
         * A per-container image the user supplied wins. Failing that, the one
         * the app rendered from its own palette and chosen effect — which is
         * what makes the Windows desktop match the launcher rather than merely
         * resemble it. The generated fallback is for the window between the
         * image filesystem being installed and the app having drawn anything.
         */
        Bitmap image = null;

        File userWallpaperFile = getUserWallpaperFile(context);
        if (userWallpaperFile.isFile()) image = BitmapFactory.decodeFile(userWallpaperFile.getPath());

        if (image == null) {
            File shared = getSharedWallpaperFile(context);
            if (shared.isFile()) image = BitmapFactory.decodeFile(shared.getPath());
        }

        if (image != null) {
            Rect srcRect = new Rect(0, 0, image.getWidth(), image.getHeight());
            Rect dstRect = new Rect(0, 0, outputWidth, outputHeight);
            canvas.drawBitmap(image, srcRect, dstRect, paint);
        }
        else {
            drawGeneratedWallpaper(canvas, outputWidth, outputHeight, accent);
        }

        ImageFs imageFs = ImageFs.find(context);
        MSBitmap.create(outputBitmap, new File(imageFs.getRootDir(), ImageFs.CACHE_PATH+"/wallpaper.bmp"));
    }

    /**
     * The desktop background, drawn rather than shipped.
     *
     * Replaces the photograph the fork inherited, which was branded for another
     * project and — being a fixed-size JPEG — was stretched to whatever
     * resolution the container ran at, so a 1920x1080 container got a blurred
     * upscale of it. Drawing it means it is always native, always sharp, and
     * costs no space in the APK.
     *
     * The shape is the app's own: a near-black ground with two soft accent
     * fields low in opacity, which is what `WemuBackground` does on every screen
     * in the launcher. The desktop the container opens on therefore looks like
     * the launcher it opened from instead of like a different product.
     *
     * @param accent the container's chosen background colour, which is the one
     *   knob a user already has over this.
     */
    private static void drawGeneratedWallpaper(Canvas canvas, int width, int height, int accent) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

        /*
         * The ground: the accent taken almost to black, and not to black itself.
         *
         * A neutral charcoal under a coloured glow reads as grey with a stain on
         * it. Keeping a trace of the hue all the way down is what makes the two
         * look like one surface. Loki's own ground does the same thing with its
         * `backgroundDepth` — base blended toward black at the top and toward
         * the accent at the bottom.
         */
        int top = blend(accent, Color.BLACK, GROUND_TOP_MIX);
        int bottom = blend(accent, Color.BLACK, GROUND_BOTTOM_MIX);
        paint.setShader(new LinearGradient(0, 0, 0, height, top, bottom, Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, width, height, paint);

        drawWaves(canvas, paint, width, height, accent);

        /*
         * A vignette, weak enough to be deniable.
         *
         * Icons and their labels sit in the corners of a Windows desktop, and
         * white text on the upper half of a gradient this light is legible only
         * just. Darkening the edges buys the contrast back without touching the
         * middle, where windows go. Loki draws the same pass over every
         * wallpaper, for the same reason plus one more: it hides the straight
         * edge where a procedural layer runs out.
         */
        paint.setShader(new RadialGradient(
                width * 0.5f, height * 0.46f, Math.max(width, height) * 0.78f,
                new int[]{Color.TRANSPARENT, Color.TRANSPARENT, Color.argb(VIGNETTE_ALPHA, 0, 0, 0)},
                new float[]{0f, 0.62f, 1f},
                Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, width, height, paint);

        paint.setShader(null);
    }

    /**
     * Loki's Waves, drawn with `android.graphics` instead of Compose.
     *
     * The geometry is copied from `AnimatedWallpaper.WaveLayer` rather than
     * reinvented — four sampled sines, each filled to the bottom of the canvas
     * so they layer into depth, each fading out below its own baseline. Band
     * count, amplitudes, baselines, frequencies and alphas are the frontend's
     * numbers, at phase zero because a desktop wallpaper is one frame.
     *
     * Written here rather than rendered from the Compose version because the
     * engine is plain Java with no Compose on its classpath, and because this
     * has to produce a bitmap at the *container's* resolution — which is not the
     * size of any surface the app ever composes.
     */
    private static void drawWaves(Canvas canvas, Paint paint, int width, int height, int accent) {
        final int bands = 4;
        int secondary = rotateHue(accent, SECONDARY_HUE_SHIFT);

        for (int index = 0; index < bands; index++) {
            float depth = index / (float) (bands - 1);
            int color = blend(accent, secondary, depth);

            float amplitude = height * (0.05f + 0.03f * (1f - depth));
            float baseline = height * (0.42f + 0.16f * depth);
            float frequency = TWO_PI * (1.2f + 0.5f * index);

            Path path = new Path();
            path.moveTo(0, baseline);
            for (float x = 0; x <= width; x += WAVE_STEP_PX) {
                float t = x / width;
                path.lineTo(x, baseline + amplitude * (float) Math.sin(t * frequency));
            }
            path.lineTo(width, height);
            path.lineTo(0, height);
            path.close();

            int alpha = Math.round((0.20f - 0.03f * index) * 255f);
            paint.setShader(new LinearGradient(
                    0, baseline, 0, height,
                    Color.argb(alpha, Color.red(color), Color.green(color), Color.blue(color)),
                    Color.TRANSPARENT,
                    Shader.TileMode.CLAMP));
            canvas.drawPath(path, paint);
        }

        paint.setShader(null);
    }

    /** `amount` of 1 is entirely `b`. */
    private static int blend(int a, int b, float amount) {
        return Color.rgb(
                Math.round(Color.red(a) * (1 - amount) + Color.red(b) * amount),
                Math.round(Color.green(a) * (1 - amount) + Color.green(b) * amount),
                Math.round(Color.blue(a) * (1 - amount) + Color.blue(b) * amount));
    }

    /** The second accent stop, a short way around the wheel from the first. */
    private static int rotateHue(int color, float degrees) {
        float[] hsv = new float[3];
        Color.colorToHSV(color, hsv);
        hsv[0] = (hsv[0] + degrees + 360f) % 360f;
        return Color.HSVToColor(hsv);
    }

    /** How far toward black the ground is taken, at the top and at the bottom. */
    private static final float GROUND_TOP_MIX = 0.86f;
    private static final float GROUND_BOTTOM_MIX = 0.94f;

    /**
     * How far the second wave colour sits from the first, around the wheel.
     *
     * Loki blends its `primary` into its `secondary` across the bands; there is
     * only one colour to work from here, so the second is derived rather than
     * given. A short rotation keeps the pair related, which is what the
     * frontend's generated palettes produce anyway.
     */
    private static final float SECONDARY_HUE_SHIFT = -38f;

    private static final int VIGNETTE_ALPHA = 90;
    private static final float TWO_PI = 6.2831855f;

    /**
     * How often the sine is sampled, in pixels.
     *
     * The frontend's figure. At this scale the difference from sampling every
     * pixel is invisible and the path is an order of magnitude cheaper — which
     * matters more here than there, because this runs at the container's
     * resolution rather than the panel's.
     */
    private static final float WAVE_STEP_PX = 12f;

    public static File getUserWallpaperFile(Context context) {
        return new File(ImageFs.find(context).getRootDir(), ImageFs.CONFIG_PATH+"/user-wallpaper.png");
    }

    /**
     * Where the app leaves the wallpaper it rendered from the current theme.
     *
     * Under the image's own config directory rather than under `CONFIG_PATH`,
     * which is inside `home/xuser` — a symlink that points at whichever
     * container was last activated. A per-container path would mean the app
     * could only write this while a session was starting, and would write it to
     * whatever container happened to be active at the time.
     */
    public static File getSharedWallpaperFile(Context context) {
        return new File(ImageFs.find(context).getConfigDir(), "theme-wallpaper.png");
    }
}
