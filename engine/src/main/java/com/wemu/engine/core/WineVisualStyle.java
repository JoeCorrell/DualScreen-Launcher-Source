package com.wemu.engine.core;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * A Windows visual style, generated on the device from the launcher's palette.
 *
 * <h2>Why generate one</h2>
 *
 * Everything a Win32 window draws — every button, checkbox, scrollbar, tab,
 * menu, caption and frame — comes from one of two renderers. Without a visual
 * style, `uxtheme` refuses to open and every control falls back to the classic
 * painter: the chiselled grey 3D look of 1995, which no amount of registry
 * colour work can modernise, because the bevels are the look. With a style
 * active, `uxtheme` draws from artwork and a stylesheet instead, and the
 * controls can be anything.
 *
 * Wine ships one — `light.msstyles`, in the Proton prefix — and pointing at it
 * is a real improvement over classic. But it is fixed, light, and blue, and this
 * launcher's palette is generated: the user picks a hue and an OKLCH recipe
 * produces a theme. A shipped style can only ever coincidentally match it.
 * Generating the style means the guest desktop is not merely modern but is
 * painted in the same colours as the app that launched it, every time the theme
 * changes.
 *
 * <h2>What a .msstyles actually is</h2>
 *
 * A resource-only PE — a DLL with no code, one `.rsrc` section and nothing else.
 * `uxtheme` opens it with `LOAD_LIBRARY_AS_DATAFILE` and reads:
 *
 * <ul>
 *   <li>{@code PACKTHEM_VERSION} — must be 3, or it is rejected outright.</li>
 *   <li>{@code COLORNAMES} / {@code SIZENAMES} — the variants on offer, as
 *       double-NUL-terminated UTF-16 lists. The registry's {@code ColorName} and
 *       {@code SizeName} must name one of these exactly.</li>
 *   <li>{@code FILERESNAMES} — for the selected variant, the name of the
 *       {@code TEXTFILE} resource holding the stylesheet.</li>
 *   <li>{@code TEXTFILE}/{@code THEMES_INI} — documentation.</li>
 *   <li>{@code TEXTFILE}/{@code LOKI_INI} — the stylesheet itself: one section
 *       per class, part and state.</li>
 *   <li>{@code RT_BITMAP} resources — the artwork. The stylesheet refers to them
 *       by filename; uxtheme uppercases the name and turns dots into
 *       underscores, so {@code loki_button.bmp} resolves to
 *       {@code LOKI_BUTTON_BMP}.</li>
 * </ul>
 *
 * <h2>What this build of uxtheme supports</h2>
 *
 * The stylesheet dialect below is not the full Windows schema; it is what Wine's
 * property table actually parses, checked against the shipped `uxtheme.dll`.
 * The consequential absence is {@code Roundness}: {@code BorderType = RoundRect}
 * parses but there is no property to give it a radius, so rounded corners cannot
 * be had from a border-fill and must be drawn into artwork. That single fact
 * decides the shape of everything here — anything with a corner radius is a
 * generated bitmap, and anything genuinely square is a border-fill, which is
 * sharper and costs nothing.
 *
 * <h2>Failure</h2>
 *
 * A malformed style is not fatal. `uxtheme` fails to open it and every control
 * falls back to classic, which is where the desktop was before. The caller
 * therefore still writes a flat classic palette, and does so from this same
 * palette, so the two agree whichever one ends up drawing.
 */
public final class WineVisualStyle {

    private static final String TAG = "WineVisualStyle";

    /**
     * Bumped whenever the generated style changes.
     *
     * Folded into the container's theme key so that changing the generator
     * reaches containers that have already booted, exactly as the wallpaper
     * revision does.
     */
    public static final int REVISION = 1;

    /** Where it goes in the guest, and what Wine is told to load. */
    public static final String THEME_DIR = "/drive_c/windows/resources/themes/loki";
    public static final String FILE_NAME = "loki.msstyles";
    public static final String GUEST_PATH = "C:\\windows\\resources\\themes\\loki\\" + FILE_NAME;

    /** Must match {@code COLORNAMES} and {@code SIZENAMES} exactly or nothing loads. */
    public static final String COLOR_NAME = "Loki";
    public static final String SIZE_NAME = "NormalSize";

    private WineVisualStyle() {}

    // ---------------------------------------------------------------- palette

    /**
     * The colours, and everything derived from them.
     *
     * Seven come from the launcher; the rest are computed here rather than
     * published by the app, because they are consequences of those seven and not
     * separate decisions. A hover that is not a fixed step above its own surface
     * stops being a hover the moment the theme moves.
     */
    public static final class Style {
        final int background, surface, elevated, primary, onSurface, onVariant, outline;

        /** Interaction states, as steps along the surface-to-text ramp. */
        final int hover, pressed;

        /** The face and text of something that cannot be used. */
        final int disabledFace, disabledEdge, disabledText;

        /** What reads on top of the accent. */
        final int onAccent;

        /** Text fields and lists, one step below the control face. */
        final int field;

        /** Close-button hover, and the scrollbar's resting thumb. */
        final int closeHot, thumb, thumbHot;

        /** Pixels per point relative to 96 DPI, and the DPI itself. */
        final float scale;
        final int dpi;

        public Style(int background, int surface, int elevated, int primary,
                     int onSurface, int onVariant, int outline, int dpi) {
            this.background = opaque(background);
            this.surface = opaque(surface);
            this.elevated = opaque(elevated);
            this.primary = opaque(primary);
            this.onSurface = opaque(onSurface);
            this.onVariant = opaque(onVariant);
            this.outline = opaque(outline);
            this.dpi = dpi;

            /*
             * Clamped, and not merely for taste.
             *
             * Sizing margins are in image pixels and do not scale, so artwork
             * generated for a 400 DPI container would carry corners a third of
             * the control wide. Three times is past every panel this runs on.
             */
            this.scale = Math.min(3f, Math.max(1f, dpi / 96f));

            this.hover = blend(this.surface, this.onSurface, 0.10f);
            this.pressed = blend(this.surface, this.onSurface, 0.18f);
            this.disabledFace = blend(this.surface, this.background, 0.55f);
            this.disabledEdge = blend(this.outline, this.background, 0.55f);
            this.disabledText = blend(this.onVariant, this.background, 0.45f);
            this.onAccent = this.background;
            this.field = this.background;

            /*
             * A red picked against the glyph rather than in the abstract.
             *
             * The close glyph keeps one colour across every state, because the
             * artwork is a strip and swapping it per state is free but reading
             * two different close buttons is not. That makes the hover fill the
             * variable: it has to contrast with a glyph whose lightness is
             * already decided by the theme.
             */
            this.closeHot = isLight(this.onSurface) ? 0xFFB3261E : 0xFFF2B8B5;

            this.thumb = blend(this.surface, this.onSurface, 0.26f);
            this.thumbHot = blend(this.surface, this.onSurface, 0.40f);
        }

        /** Device pixels for a figure quoted at 96 DPI, never rounding to nothing. */
        int px(float at96dpi) {
            return Math.max(1, Math.round(at96dpi * scale));
        }

        /**
         * The face of a drawn icon — a page, a drive, a window.
         *
         * A definite step away from the list background rather than a step along
         * the surface ramp, and that is the whole point: an icon is read against
         * the file list it sits in, not against a panel. Moving toward the text
         * colour puts it lighter than the ground on a dark theme and darker on a
         * light one, so the same drawing has contrast either way — which
         * `surface` alone does not, being a step *toward* the ground on half the
         * palettes the generator produces.
         */
        int iconFace() {
            return blend(background, onSurface, 0.22f);
        }

        String rgb(int color) {
            return Color.red(color) + " " + Color.green(color) + " " + Color.blue(color);
        }
    }

    private static int opaque(int color) {
        return color | 0xFF000000;
    }

    /** {@code amount} of 1 is entirely {@code b}. */
    private static int blend(int a, int b, float amount) {
        return Color.rgb(
                Math.round(Color.red(a) * (1 - amount) + Color.red(b) * amount),
                Math.round(Color.green(a) * (1 - amount) + Color.green(b) * amount),
                Math.round(Color.blue(a) * (1 - amount) + Color.blue(b) * amount));
    }

    private static boolean isLight(int color) {
        return (0.299f * Color.red(color) + 0.587f * Color.green(color)
                + 0.114f * Color.blue(color)) > 140f;
    }

    // ------------------------------------------------------------- public API

    /**
     * Writes the style, creating its directory.
     *
     * @return true if the file on disk is now a usable style.
     */
    public static boolean write(File out, Style s) {
        try {
            File dir = out.getParentFile();
            if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
                Log.w(TAG, "Cannot create " + dir);
                return false;
            }

            List<Resource> resources = new ArrayList<>();
            String ini = buildStylesheet(s, resources);

            resources.add(Resource.named("PACKTHEM_VERSION", 1, new byte[]{3, 0}));
            resources.add(Resource.named("COLORNAMES", 1, list(COLOR_NAME)));
            resources.add(Resource.named("SIZENAMES", 1, list(SIZE_NAME)));
            resources.add(Resource.named("FILERESNAMES", 1, list("LOKI_INI")));
            resources.add(Resource.text("THEMES_INI",
                    "[documentation]\r\nDisplayName = Loki\r\nToolTip = Loki Visual Style\r\n"));
            resources.add(Resource.text("LOKI_INI", ini));

            byte[] pe = buildPortableExecutable(resources);
            try (FileOutputStream stream = new FileOutputStream(out)) {
                stream.write(pe);
            }
            Log.d(TAG, "Wrote " + out.getName() + " (" + pe.length + " bytes, "
                    + resources.size() + " resources, " + s.dpi + " DPI)");
            return true;
        } catch (IOException | RuntimeException e) {
            Log.e(TAG, "Could not generate the visual style", e);
            return false;
        }
    }

    /** A double-NUL-terminated UTF-16 list, which is how the variant names are stored. */
    private static byte[] list(String single) {
        return utf16(single + "\0\0");
    }

    private static byte[] utf16(String text) {
        byte[] out = new byte[text.length() * 2];
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            out[i * 2] = (byte) (c & 0xFF);
            out[i * 2 + 1] = (byte) (c >> 8);
        }
        return out;
    }

    // ------------------------------------------------------------ stylesheet

    /**
     * The stylesheet, and the artwork it refers to.
     *
     * Written together because they are one decision each: a part is either a
     * shape with a corner radius, in which case it is drawn and nine-sliced, or
     * it is a rectangle, in which case a border-fill states it exactly in two
     * lines and stays sharp at any size.
     */
    private static String buildStylesheet(Style s, List<Resource> art) {
        Ini ini = new Ini();

        /*
         * Bevels, collapsed.
         *
         * These are the classic renderer's four 3D edge colours, and uxtheme
         * still hands them to anything that asks for an edge — group boxes,
         * client edges, controls with no themed part. Setting all five to the
         * same flat pair is what stops a stray chiselled rectangle appearing in
         * the middle of an otherwise modern dialog.
         */
        ini.section("Globals");
        ini.set("EdgeLightColor", s.rgb(s.outline));
        ini.set("EdgeHighLightColor", s.rgb(s.outline));
        ini.set("EdgeShadowColor", s.rgb(s.outline));
        ini.set("EdgeDkShadowColor", s.rgb(s.outline));
        ini.set("EdgeFillColor", s.rgb(s.surface));

        systemMetrics(ini, s);

        button(ini, s, art);
        comboBox(ini, s, art);
        edit(ini, s, art);
        listsAndTrees(ini, s, art);
        header(ini, s, art);
        menu(ini, s, art);
        progress(ini, s);
        scrollBar(ini, s, art);
        spin(ini, s, art);
        status(ini, s);
        tab(ini, s, art);
        toolbar(ini, s, art);
        tooltip(ini, s);
        trackBar(ini, s, art);
        window(ini, s, art);

        return ini.toString();
    }

    /**
     * The colours and furniture that are not any one control.
     *
     * A themed system takes its system colours from here rather than from
     * `Control Panel\Colors`, so this has to restate the palette — otherwise a
     * dialog's own background, drawn by the application with `COLOR_BTNFACE`,
     * comes out in whatever the registry said while every control on it comes
     * out in the theme's colours.
     */
    private static void systemMetrics(Ini ini, Style s) {
        ini.section("SysMetrics");

        ini.set("Scrollbar", s.rgb(s.background));
        ini.set("Background", s.rgb(s.background));
        ini.set("ActiveCaption", s.rgb(s.elevated));
        ini.set("InactiveCaption", s.rgb(s.surface));
        ini.set("GradientActiveCaption", s.rgb(s.elevated));
        ini.set("GradientInactiveCaption", s.rgb(s.surface));
        ini.set("CaptionText", s.rgb(s.onSurface));
        ini.set("InactiveCaptionText", s.rgb(s.onVariant));
        ini.set("Menu", s.rgb(s.surface));
        ini.set("MenuBar", s.rgb(s.surface));
        ini.set("MenuText", s.rgb(s.onSurface));
        ini.set("MenuHilight", s.rgb(s.primary));
        ini.set("Window", s.rgb(s.field));
        ini.set("WindowText", s.rgb(s.onSurface));
        ini.set("WindowFrame", s.rgb(s.outline));
        ini.set("ActiveBorder", s.rgb(s.outline));
        ini.set("InactiveBorder", s.rgb(s.outline));
        ini.set("AppWorkSpace", s.rgb(s.background));
        ini.set("Highlight", s.rgb(s.primary));
        ini.set("HighlightText", s.rgb(s.onAccent));
        ini.set("HotTracking", s.rgb(s.primary));
        ini.set("BtnFace", s.rgb(s.surface));
        ini.set("BtnText", s.rgb(s.onSurface));
        ini.set("ButtonAlternateFace", s.rgb(s.surface));
        ini.set("GrayText", s.rgb(s.disabledText));
        ini.set("InfoBk", s.rgb(s.elevated));
        ini.set("InfoText", s.rgb(s.onSurface));

        /*
         * The three bevel slots, all equal to the face.
         *
         * `BtnShadow`, `BtnHighlight`, `DkShadow3d` and `Light3d` are what an
         * application reads when it draws its own frame, and a great many still
         * do. Different values chisel; identical values are flat.
         */
        ini.set("BtnShadow", s.rgb(s.surface));
        ini.set("BtnHighlight", s.rgb(s.surface));
        ini.set("DkShadow3d", s.rgb(s.surface));
        ini.set("Light3d", s.rgb(s.surface));

        ini.blank();
        ini.set("ScrollbarWidth", String.valueOf(s.px(14)));
        ini.set("ScrollbarHeight", String.valueOf(s.px(14)));
        ini.set("CaptionBarHeight", String.valueOf(s.px(28)));
        ini.set("SMCaptionBarHeight", String.valueOf(s.px(19)));
        ini.set("SMCaptionBarWidth", String.valueOf(s.px(19)));

        /*
         * Point sizes, deliberately not scaled.
         *
         * Wine scales fonts by `LogPixels`, which the container already sets
         * from its resolution. Scaling here as well would apply the DPI twice.
         */
        ini.blank();
        ini.set("CaptionFont", "Tahoma, 9");
        ini.set("SmallCaptionFont", "Tahoma, 8");
        ini.set("MenuFont", "Tahoma, 9");
        ini.set("StatusFont", "Tahoma, 9");
        ini.set("MsgBoxFont", "Tahoma, 9");
        ini.set("IconTitleFont", "Tahoma, 9");

        /* Menus without the raised border, which is half of the 1995 look. */
        ini.set("FlatMenus", "true");
    }

    // --------------------------------------------------------------- controls

    private static void button(Ini ini, Style s, List<Resource> art) {
        int margin = s.px(8);
        int radius = s.px(6);

        /*
         * Six states, of which the sixth is the fifth.
         *
         * `PBS_DEFAULTED_ANIMATING` exists so a theme can pulse the default
         * button; there is nothing to animate here, and a strip has to be
         * complete or uxtheme reads the wrong cell for every state after the
         * gap.
         */
        int[] fills = {s.surface, s.hover, s.pressed, s.disabledFace, s.primary, s.primary};
        int[] edges = {s.outline, s.primary, s.primary, s.disabledEdge, s.primary, s.primary};
        art.add(roundedStrip("loki_button.bmp", s, margin, radius, fills, edges));

        ini.section("Button.Pushbutton");
        ini.set("BgType", "ImageFile");
        ini.set("ImageFile", "loki_button.bmp");
        ini.set("ImageCount", "6");
        ini.set("ImageLayout", "Vertical");
        ini.set("SizingType", "Stretch");
        ini.set("Transparent", "True");
        ini.set("SizingMargins", margins(margin));
        ini.set("ContentMargins", margins(s.px(4)));
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.set("MinSize", s.px(10) + ", " + s.px(6));

        ini.section("Button.Pushbutton(Hot)");
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.section("Button.Pushbutton(Pressed)");
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.section("Button.Pushbutton(Disabled)");
        ini.set("TextColor", s.rgb(s.disabledText));
        ini.section("Button.Pushbutton(Defaulted)");
        ini.set("TextColor", s.rgb(s.onAccent));

        checkBox(ini, s, art);
        radioButton(ini, s, art);

        /*
         * Group boxes as a bare outline.
         *
         * `BorderOnly` is the whole point: a group box that fills would paint
         * over whatever it groups, and the classic renderer's answer — an etched
         * two-tone line — is the most recognisably old thing on a dialog.
         */
        ini.section("Button.Groupbox");
        ini.set("BgType", "BorderFill");
        ini.set("BorderType", "Rect");
        ini.set("BorderSize", "1");
        ini.set("BorderColor", s.rgb(s.outline));
        ini.set("BorderOnly", "True");
        ini.set("TextColor", s.rgb(s.onVariant));

        ini.section("Button.Groupbox(Disabled)");
        ini.set("BorderColor", s.rgb(s.disabledEdge));
        ini.set("TextColor", s.rgb(s.disabledText));
    }

    /**
     * Twenty cells for four appearances.
     *
     * Unchecked, checked and mixed each have normal/hot/pressed/disabled, and
     * then the whole unchecked set repeats twice more for the implicit and
     * excluded states that tri-state list views use. They are drawn identically;
     * what matters is that the strip has twenty cells so the indices line up.
     */
    private static void checkBox(Ini ini, Style s, List<Resource> art) {
        int box = s.px(15);
        int radius = s.px(4);
        Bitmap strip = Bitmap.createBitmap(box, box * 20, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);

        for (int state = 0; state < 20; state++) {
            int group = state % 4;
            boolean checked = state >= 4 && state < 8;
            boolean mixed = state >= 8 && state < 12;
            boolean disabled = group == 3;
            boolean filled = checked || mixed;

            int fill = disabled ? s.disabledFace : filled ? s.primary
                    : group == 2 ? s.pressed : s.field;
            int edge = disabled ? s.disabledEdge : filled ? s.primary
                    : group == 1 ? s.primary : s.outline;
            int mark = disabled ? s.disabledText : s.onAccent;

            canvas.save();
            canvas.translate(0, (float) state * box);
            roundedCell(canvas, s, box, box, radius, fill, edge);
            if (checked) drawCheck(canvas, s, box, box, mark);
            if (mixed) drawDash(canvas, s, box, box, mark);
            canvas.restore();
        }
        art.add(Resource.bitmap("loki_checkbox.bmp", strip));

        ini.section("Button.Checkbox");
        trueSizeGlyphPart(ini, "loki_checkbox.bmp", 20);
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.section("Button.Checkbox(Uncheckeddisabled)");
        ini.set("TextColor", s.rgb(s.disabledText));
        ini.section("Button.Checkbox(Checkeddisabled)");
        ini.set("TextColor", s.rgb(s.disabledText));
        ini.section("Button.Checkbox(Mixeddisabled)");
        ini.set("TextColor", s.rgb(s.disabledText));
    }

    private static void radioButton(Ini ini, Style s, List<Resource> art) {
        int box = s.px(15);
        Bitmap strip = Bitmap.createBitmap(box, box * 8, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float border = Math.max(1f, s.scale);

        for (int state = 0; state < 8; state++) {
            int group = state % 4;
            boolean checked = state >= 4;
            boolean disabled = group == 3;

            int fill = disabled ? s.disabledFace : checked ? s.primary
                    : group == 2 ? s.pressed : s.field;
            int edge = disabled ? s.disabledEdge : checked ? s.primary
                    : group == 1 ? s.primary : s.outline;

            canvas.save();
            canvas.translate(0, (float) state * box);

            float cx = box / 2f;
            float cy = box / 2f;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(fill);
            canvas.drawCircle(cx, cy, box / 2f - border / 2f, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(border);
            paint.setColor(edge);
            canvas.drawCircle(cx, cy, box / 2f - border / 2f, paint);

            if (checked) {
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(disabled ? s.disabledText : s.onAccent);
                canvas.drawCircle(cx, cy, box * 0.22f, paint);
            }
            canvas.restore();
        }
        art.add(Resource.bitmap("loki_radio.bmp", strip));

        ini.section("Button.Radiobutton");
        trueSizeGlyphPart(ini, "loki_radio.bmp", 8);
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.section("Button.Radiobutton(Uncheckeddisabled)");
        ini.set("TextColor", s.rgb(s.disabledText));
        ini.section("Button.Radiobutton(Checkeddisabled)");
        ini.set("TextColor", s.rgb(s.disabledText));
    }

    private static void comboBox(Ini ini, Style s, List<Resource> art) {
        int margin = s.px(6);
        int radius = s.px(5);

        int[] fills = {s.field, s.field, s.field, s.disabledFace};
        int[] edges = {s.outline, s.primary, s.primary, s.disabledEdge};
        art.add(roundedStrip("loki_field.bmp", s, margin, radius, fills, edges));

        /* Transparent until touched: the button lives inside the field's frame. */
        int[] buttonFills = {Color.TRANSPARENT, s.hover, s.pressed, Color.TRANSPARENT};
        int[] buttonEdges = {Color.TRANSPARENT, Color.TRANSPARENT, Color.TRANSPARENT, Color.TRANSPARENT};
        art.add(roundedStrip("loki_combo_button.bmp", s, s.px(4), s.px(4), buttonFills, buttonEdges));

        int glyph = s.px(9);
        art.add(chevronStrip("loki_combo_glyph.bmp", s, glyph, 4,
                new int[]{s.onVariant, s.onSurface, s.onSurface, s.disabledText},
                new int[]{Chevron.DOWN, Chevron.DOWN, Chevron.DOWN, Chevron.DOWN}));

        ini.section("ComboBox");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "1");
        ini.set("BorderColor", s.rgb(s.outline));
        ini.set("FillColor", s.rgb(s.field));

        ini.section("ComboBox.Border");
        nineSlicePart(ini, "loki_field.bmp", 4, margin);

        ini.section("ComboBox.ReadOnly");
        nineSlicePart(ini, "loki_field.bmp", 4, margin);
        ini.set("TextColor", s.rgb(s.onSurface));

        ini.section("ComboBox.DropDownButton");
        nineSlicePart(ini, "loki_combo_button.bmp", 4, s.px(4));
        ini.set("GlyphType", "ImageGlyph");
        ini.set("GlyphTransparent", "True");
        ini.set("ImageSelectType", "Dpi");
        ini.set("ImageFile1", "loki_combo_glyph.bmp");
        ini.set("MinDpi1", "96");

        ini.section("ComboBox.DropDownItem(Normal)");
        ini.set("BgType", "None");
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.section("ComboBox.DropDownItem(HighLighted)");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.primary));
        ini.set("TextColor", s.rgb(s.onAccent));

        ini.section("ComboBox.CueBanner");
        ini.set("TextColor", s.rgb(s.disabledText));
    }

    private static void edit(Ini ini, Style s, List<Resource> art) {
        int margin = s.px(6);

        ini.section("Edit");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "1");
        ini.set("BorderColor", s.rgb(s.outline));
        ini.set("FillColor", s.rgb(s.field));
        ini.set("TextColor", s.rgb(s.onSurface));

        ini.section("Edit.Edittext(Disabled)");
        ini.set("FillColor", s.rgb(s.disabledFace));
        ini.set("BorderColor", s.rgb(s.disabledEdge));
        ini.set("TextColor", s.rgb(s.disabledText));

        ini.section("Edit.Edittext(ReadOnly)");
        ini.set("FillColor", s.rgb(s.surface));
        ini.set("TextColor", s.rgb(s.onVariant));

        ini.section("Edit.EditText(CueBanner)");
        ini.set("TextColor", s.rgb(s.disabledText));

        ini.section("Edit.Background");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.field));

        /*
         * The four border parts share one strip.
         *
         * They differ only in which scrollbars the edit has, which changes
         * nothing about how the frame looks — but each is a separate part and
         * an unset part falls back to classic, so all four are stated.
         */
        for (String part : new String[]{
                "Edit.EditBorder_NoScroll", "Edit.EditBorder_HScroll",
                "Edit.EditBorder_VScroll", "Edit.EditBorder_HVScroll"}) {
            ini.section(part);
            nineSlicePart(ini, "loki_field.bmp", 4, margin);
        }
    }

    private static void listsAndTrees(Ini ini, Style s, List<Resource> art) {
        for (String cls : new String[]{"ListBox", "ListView", "TreeView", "Explorer::TreeView"}) {
            ini.section(cls);
            ini.set("BgType", "BorderFill");
            ini.set("BorderSize", "1");
            ini.set("BorderColor", s.rgb(s.outline));
            ini.set("FillColor", s.rgb(s.field));
        }

        /* Rows that do not draw a border, so a selection cannot double up on one. */
        String[] rowParts = {"Hot", "Selected", "SelectedNotFocus", "HotSelected"};
        int[] rowFills = {s.hover, s.primary, s.surface, s.primary};
        int[] rowText = {s.onSurface, s.onAccent, s.onSurface, s.onAccent};
        for (int i = 0; i < rowParts.length; i++) {
            ini.section("Explorer::TreeView.TreeItem(" + rowParts[i] + ")");
            ini.set("BgType", "BorderFill");
            ini.set("BorderSize", "0");
            ini.set("FillColor", s.rgb(rowFills[i]));
            ini.set("TextColor", s.rgb(rowText[i]));
        }
        ini.section("Explorer::TreeView.TreeItem(Disabled)");
        ini.set("BgType", "None");
        ini.set("TextColor", s.rgb(s.disabledText));

        /*
         * Expanders as chevrons rather than plus and minus boxes.
         *
         * A boxed +/- is the strongest single tell of a Windows 95 tree, and
         * the rotation from pointing-right to pointing-down says the same thing
         * without the box.
         */
        int glyph = s.px(11);
        art.add(chevronStrip("loki_tree_glyph.bmp", s, glyph, 2,
                new int[]{s.onVariant, s.onVariant},
                new int[]{Chevron.RIGHT, Chevron.DOWN}));
        art.add(chevronStrip("loki_tree_glyph_hot.bmp", s, glyph, 2,
                new int[]{s.primary, s.primary},
                new int[]{Chevron.RIGHT, Chevron.DOWN}));

        ini.section("TreeView.Glyph");
        trueSizeGlyphPart(ini, "loki_tree_glyph.bmp", 2);
        ini.section("Explorer::TreeView.Glyph");
        trueSizeGlyphPart(ini, "loki_tree_glyph.bmp", 2);
        ini.section("Explorer::TreeView.HotGlyph");
        trueSizeGlyphPart(ini, "loki_tree_glyph_hot.bmp", 2);
    }

    private static void header(Ini ini, Style s, List<Resource> art) {
        /*
         * Twelve cells: normal, hot and pressed, then the same three again for
         * each of the sorted-up, sorted-down and dropdown variants that Explorer
         * asks for. All twelve are drawn the same three ways.
         */
        int[] fills = new int[12];
        int[] edges = new int[12];
        for (int i = 0; i < 12; i++) {
            int group = i % 3;
            fills[i] = group == 0 ? s.surface : group == 1 ? s.hover : s.pressed;
            edges[i] = s.outline;
        }
        art.add(headerStrip("loki_header_item.bmp", s, fills, edges));

        ini.section("Header");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.surface));

        ini.section("Header.HeaderItem");
        ini.set("BgType", "ImageFile");
        ini.set("ImageFile", "loki_header_item.bmp");
        ini.set("ImageCount", "12");
        ini.set("ImageLayout", "Vertical");
        ini.set("SizingType", "Stretch");
        ini.set("SizingMargins", "0, " + s.px(2) + ", 0, " + s.px(2));
        ini.set("ContentMargins", s.px(6) + ", " + s.px(6) + ", 0, 0");
        ini.set("TextColor", s.rgb(s.onVariant));
    }

    private static void menu(Ini ini, Style s, List<Resource> art) {
        int radius = s.px(4);
        int margin = s.px(6);

        int[] barFills = {Color.TRANSPARENT, s.hover, s.primary,
                Color.TRANSPARENT, s.hover, s.primary};
        art.add(roundedStrip("loki_menu_bar_item.bmp", s, margin, radius, barFills, transparent(6)));

        int[] itemFills = {Color.TRANSPARENT, s.primary, Color.TRANSPARENT, s.primary};
        art.add(roundedStrip("loki_menu_item.bmp", s, margin, radius, itemFills, transparent(4)));

        int check = s.px(14);
        Bitmap checks = Bitmap.createBitmap(check, check * 4, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(checks);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        for (int state = 0; state < 4; state++) {
            int color = (state % 2 == 0) ? s.primary : s.disabledText;
            canvas.save();
            canvas.translate(0, (float) state * check);
            if (state < 2) {
                drawCheck(canvas, s, check, check, color);
            } else {
                paint.setColor(color);
                paint.setStyle(Paint.Style.FILL);
                canvas.drawCircle(check / 2f, check / 2f, check * 0.18f, paint);
            }
            canvas.restore();
        }
        art.add(Resource.bitmap("loki_menu_check.bmp", checks));

        art.add(chevronStrip("loki_menu_submenu.bmp", s, s.px(9), 2,
                new int[]{s.onVariant, s.disabledText},
                new int[]{Chevron.RIGHT, Chevron.RIGHT}));

        ini.section("Menu");
        ini.set("BgType", "None");

        ini.section("Menu.BarBackground");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.surface));

        ini.section("Menu.BarItem");
        nineSlicePart(ini, "loki_menu_bar_item.bmp", 6, margin);
        ini.set("ContentMargins", s.px(8) + ", " + s.px(8) + ", " + s.px(3) + ", " + s.px(3));
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.section("Menu.BarItem(Pushed)");
        ini.set("TextColor", s.rgb(s.onAccent));
        ini.section("Menu.BarItem(Disabled)");
        ini.set("TextColor", s.rgb(s.disabledText));

        /*
         * The popup is a panel with an outline, not a raised box.
         *
         * `PopupBorders` is a separate part drawn over the background; giving
         * the background the outline and the borders nothing avoids the two
         * disagreeing about where the edge is.
         */
        ini.section("Menu.PopupBackground");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "1");
        ini.set("BorderColor", s.rgb(s.outline));
        ini.set("FillColor", s.rgb(s.elevated));

        ini.section("Menu.PopupBorders");
        ini.set("BgType", "None");

        ini.section("Menu.PopupItem");
        nineSlicePart(ini, "loki_menu_item.bmp", 4, margin);
        ini.set("ContentMargins", "0, 0, " + s.px(2) + ", " + s.px(2));
        ini.set("TextColor", s.rgb(s.onSurface));
        ini.section("Menu.PopupItem(Hot)");
        ini.set("TextColor", s.rgb(s.onAccent));
        ini.section("Menu.PopupItem(Disabled)");
        ini.set("TextColor", s.rgb(s.disabledText));
        ini.section("Menu.PopupItem(DisabledHot)");
        ini.set("TextColor", s.rgb(s.disabledText));

        /* No gutter: the reserved strip down the left is where the old look lives. */
        ini.section("Menu.PopupGutter");
        ini.set("BgType", "None");
        ini.section("Menu.PopupCheckBackground");
        ini.set("BgType", "None");

        ini.section("Menu.PopupCheck");
        trueSizeGlyphPart(ini, "loki_menu_check.bmp", 4);

        ini.section("Menu.PopupSubmenu");
        trueSizeGlyphPart(ini, "loki_menu_submenu.bmp", 2);

        ini.section("Menu.PopupSeparator");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.outline));
        ini.set("ContentMargins", s.px(8) + ", " + s.px(8) + ", 0, 0");
    }

    private static void progress(Ini ini, Style s) {
        ini.section("Progress");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "1");
        ini.set("BorderColor", s.rgb(s.outline));
        ini.set("FillColor", s.rgb(s.field));

        /* One continuous bar: chunk size 1 with no gap is what removes the blocks. */
        ini.set("ProgressChunkSize", "1");
        ini.set("ProgressSpaceSize", "0");

        for (String part : new String[]{"Progress.Bar", "Progress.BarVert",
                "Progress.TransparentBar", "Progress.TransparentBarVert"}) {
            ini.section(part);
            ini.set("BgType", "BorderFill");
            ini.set("BorderSize", "1");
            ini.set("BorderColor", s.rgb(s.outline));
            ini.set("FillColor", s.rgb(s.field));
        }

        for (String part : new String[]{"Progress.Chunk", "Progress.ChunkVert",
                "Progress.Fill", "Progress.FillVert"}) {
            ini.section(part);
            ini.set("BgType", "BorderFill");
            ini.set("BorderSize", "0");
            ini.set("FillColor", s.rgb(s.primary));
        }
    }

    private static void scrollBar(Ini ini, Style s, List<Resource> art) {
        int track = blend(s.background, s.surface, 0.5f);

        int[] arrowFills = new int[20];
        for (int i = 0; i < 20; i++) {
            int group = i % 4;
            arrowFills[i] = i >= 16 ? s.hover
                    : group == 1 ? s.hover : group == 2 ? s.pressed : track;
        }
        art.add(roundedStrip("loki_scroll_arrow.bmp", s, s.px(3), 0, arrowFills, transparent(20)));

        int[] arrowColors = new int[20];
        int[] directions = new int[20];
        for (int i = 0; i < 20; i++) {
            int group = i % 4;
            arrowColors[i] = group == 3 ? s.disabledText : s.onVariant;
            int block = i < 16 ? i / 4 : i - 16;
            directions[i] = block == 0 ? Chevron.UP : block == 1 ? Chevron.DOWN
                    : block == 2 ? Chevron.LEFT : Chevron.RIGHT;
        }
        art.add(chevronStrip("loki_scroll_glyph.bmp", s, s.px(8), 20, arrowColors, directions));

        int[] thumbFills = {s.thumb, s.thumbHot, s.primary, track, s.thumbHot};
        int thumbRadius = s.px(5);
        art.add(roundedStrip("loki_scroll_thumb_v.bmp", s, s.px(7), thumbRadius, thumbFills, transparent(5)));
        art.add(roundedStrip("loki_scroll_thumb_h.bmp", s, s.px(7), thumbRadius, thumbFills, transparent(5)));

        ini.section("ScrollBar");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(track));

        ini.section("ScrollBar.ArrowBtn");
        nineSlicePart(ini, "loki_scroll_arrow.bmp", 20, s.px(3));
        ini.set("GlyphType", "ImageGlyph");
        ini.set("GlyphTransparent", "True");
        ini.set("ImageSelectType", "Dpi");
        ini.set("ImageFile1", "loki_scroll_glyph.bmp");
        ini.set("MinDpi1", "96");

        ini.section("ScrollBar.ThumbBtnVert");
        nineSlicePart(ini, "loki_scroll_thumb_v.bmp", 5, s.px(7));
        ini.section("ScrollBar.ThumbBtnHorz");
        nineSlicePart(ini, "loki_scroll_thumb_h.bmp", 5, s.px(7));

        for (String part : new String[]{"ScrollBar.LowerTrackHorz", "ScrollBar.UpperTrackHorz",
                "ScrollBar.LowerTrackVert", "ScrollBar.UpperTrackVert", "ScrollBar.SizeBox",
                "ScrollBar.SizeBoxBkgnd"}) {
            ini.section(part);
            ini.set("BgType", "BorderFill");
            ini.set("BorderSize", "0");
            ini.set("FillColor", s.rgb(track));
        }

        /* No grippers. The three ridges on a scrollbar thumb are pure 1995. */
        ini.section("ScrollBar.GripperHorz");
        ini.set("BgType", "None");
        ini.section("ScrollBar.GripperVert");
        ini.set("BgType", "None");
    }

    private static void spin(Ini ini, Style s, List<Resource> art) {
        int margin = s.px(4);
        int radius = s.px(3);
        int[] fills = {s.surface, s.hover, s.pressed, s.disabledFace};
        int[] edges = {s.outline, s.primary, s.primary, s.disabledEdge};
        art.add(roundedStrip("loki_spin.bmp", s, margin, radius, fills, edges));

        int[] colors = {s.onVariant, s.onSurface, s.onSurface, s.disabledText};
        art.add(chevronStrip("loki_spin_up.bmp", s, s.px(7), 4, colors, fill(4, Chevron.UP)));
        art.add(chevronStrip("loki_spin_down.bmp", s, s.px(7), 4, colors, fill(4, Chevron.DOWN)));
        art.add(chevronStrip("loki_spin_left.bmp", s, s.px(7), 4, colors, fill(4, Chevron.LEFT)));
        art.add(chevronStrip("loki_spin_right.bmp", s, s.px(7), 4, colors, fill(4, Chevron.RIGHT)));

        String[] parts = {"Spin.Up", "Spin.Down", "Spin.Uphorz", "Spin.Downhorz"};
        String[] glyphs = {"loki_spin_up.bmp", "loki_spin_down.bmp",
                "loki_spin_right.bmp", "loki_spin_left.bmp"};
        for (int i = 0; i < parts.length; i++) {
            ini.section(parts[i]);
            nineSlicePart(ini, "loki_spin.bmp", 4, margin);
            ini.set("GlyphType", "ImageGlyph");
            ini.set("GlyphTransparent", "True");
            ini.set("ImageSelectType", "Dpi");
            ini.set("ImageFile1", glyphs[i]);
            ini.set("MinDpi1", "96");
        }
    }

    private static void status(Ini ini, Style s) {
        ini.section("Status");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.surface));

        ini.section("Status.Pane");
        ini.set("BgType", "None");
        ini.section("Status.GripperPane");
        ini.set("BgType", "None");
        ini.section("Status.Gripper");
        ini.set("BgType", "None");
    }

    private static void tab(Ini ini, Style s, List<Resource> art) {
        int margin = s.px(7);
        int radius = s.px(6);

        /*
         * Five states, and the selected one carries an accent rule.
         *
         * A tab strip has to say which tab is current in a way that survives
         * being next to the pane it belongs to. A filled tab does it by lifting
         * to the pane's own colour, which reads as continuous with it; the rule
         * along the top is what makes that reading unambiguous rather than
         * merely lighter.
         */
        int[] fills = {s.background, s.hover, s.surface, s.disabledFace, s.hover};
        int[] edges = {Color.TRANSPARENT, Color.TRANSPARENT, s.outline, Color.TRANSPARENT, s.primary};
        int[] rules = {Color.TRANSPARENT, Color.TRANSPARENT, s.primary, Color.TRANSPARENT, Color.TRANSPARENT};
        art.add(tabStrip("loki_tab_item.bmp", s, margin, radius, fills, edges, rules));

        for (String part : new String[]{"Tab.TabItem", "Tab.TabItemLeftEdge",
                "Tab.TabItemRightEdge", "Tab.TabItemBothEdge", "Tab.TopTabItem",
                "Tab.TopTabItemLeftEdge", "Tab.TopTabItemRightEdge", "Tab.TopTabItemBothEdge"}) {
            ini.section(part);
            nineSlicePart(ini, "loki_tab_item.bmp", 5, margin);
            ini.set("ContentMargins", margins(s.px(4)));
            ini.set("TextColor", s.rgb(s.onVariant));
        }

        ini.section("Tab.Pane");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "1");
        ini.set("BorderColor", s.rgb(s.outline));
        ini.set("FillColor", s.rgb(s.surface));

        ini.section("Tab.Body");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.surface));
    }

    private static void toolbar(Ini ini, Style s, List<Resource> art) {
        int margin = s.px(6);
        int radius = s.px(5);

        int[] fills = {Color.TRANSPARENT, s.hover, s.pressed, Color.TRANSPARENT,
                s.pressed, s.hover, s.hover, s.hover};
        art.add(roundedStrip("loki_toolbar_button.bmp", s, margin, radius, fills, transparent(8)));

        ini.section("Toolbar");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.surface));

        for (String part : new String[]{"Toolbar.Button", "Toolbar.DropdownButton",
                "Toolbar.SplitButton", "Toolbar.SplitButtonDropdown"}) {
            ini.section(part);
            nineSlicePart(ini, "loki_toolbar_button.bmp", 8, margin);
            ini.set("ContentMargins", margins(s.px(3)));
            ini.set("TextColor", s.rgb(s.onSurface));
        }

        ini.section("Toolbar.Separator");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.outline));
        ini.section("Toolbar.SeparatorVert");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.outline));

        ini.section("Rebar");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.surface));
        ini.section("Rebar.Band");
        ini.set("BgType", "None");
        ini.section("Rebar.Gripper");
        ini.set("BgType", "None");
        ini.section("Rebar.GripperVert");
        ini.set("BgType", "None");
    }

    /**
     * Tooltips, spelled the way uxtheme spells them.
     *
     * Wine's own visual style says `Tooltip.Ballon`, with one L, and uxtheme's
     * part table says `Balloon` — so every balloon tooltip in Wine's own theme
     * silently falls through to the classic renderer. An unrecognised part name
     * is not an error anywhere; it simply never matches.
     */
    private static void tooltip(Ini ini, Style s) {
        for (String part : new String[]{"Tooltip.Standard", "Tooltip.Balloon"}) {
            ini.section(part);
            ini.set("BgType", "BorderFill");
            ini.set("BorderSize", "1");
            ini.set("BorderColor", s.rgb(s.outline));
            ini.set("FillColor", s.rgb(s.elevated));
            ini.set("TextColor", s.rgb(s.onSurface));
            ini.set("ContentMargins", margins(s.px(6)));
        }
        for (String part : new String[]{"Tooltip.StandardTitle", "Tooltip.BalloonTitle"}) {
            ini.section(part);
            ini.set("BgType", "None");
            ini.set("TextColor", s.rgb(s.onSurface));
        }
    }

    private static void trackBar(Ini ini, Style s, List<Resource> art) {
        int thumbWidth = s.px(12);
        int thumbLength = s.px(20);

        int[] fills = {s.primary, blend(s.primary, s.onSurface, 0.15f),
                blend(s.primary, s.background, 0.15f), s.disabledFace, s.primary};
        art.add(pillStrip("loki_track_thumb_h.bmp", s, thumbWidth, thumbLength, fills, false));
        art.add(pillStrip("loki_track_thumb_v.bmp", s, thumbLength, thumbWidth, fills, true));

        ini.section("TrackBar");
        ini.set("BgType", "None");

        ini.section("TrackBar.Track");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.outline));
        ini.section("TrackBar.TrackVert");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.outline));

        for (String part : new String[]{"TrackBar.Thumb", "TrackBar.ThumbTop", "TrackBar.ThumbBottom"}) {
            ini.section(part);
            trueSizeGlyphPart(ini, "loki_track_thumb_h.bmp", 5);
        }
        for (String part : new String[]{"TrackBar.ThumbVert", "TrackBar.ThumbLeft", "TrackBar.ThumbRight"}) {
            ini.section(part);
            trueSizeGlyphPart(ini, "loki_track_thumb_v.bmp", 5);
        }

        /* No tick marks. They are noise on a handheld and they date the control. */
        ini.section("TrackBar.Tics");
        ini.set("BgType", "None");
        ini.section("TrackBar.TicsVert");
        ini.set("BgType", "None");
    }

    /**
     * The window itself: caption, frame and buttons.
     *
     * This is the part that decides whether the desktop looks like this decade,
     * because it is drawn around every window regardless of what the window
     * itself contains. A 1995 caption over a modern dialog still reads as 1995.
     */
    private static void window(Ini ini, Style s, List<Resource> art) {
        int captionRadius = s.px(9);
        int captionMargin = captionRadius + s.px(3);

        /*
         * Rounded at the top, square at the bottom, and transparent outside.
         *
         * The corners are the strongest single cue available, and they are only
         * possible because the caption is drawn with an alpha channel — a
         * border-fill cannot round. The bottom stays square because the caption
         * meets the client area there and a radius would cut into it.
         */
        int[] captionFills = {s.elevated, s.surface, s.surface};
        art.add(captionStrip("loki_caption.bmp", s, captionMargin, captionRadius, captionFills));

        ini.section("Window");
        ini.set("BgType", "None");

        for (String part : new String[]{"Window.Caption", "Window.MaxCaption", "Window.MinCaption"}) {
            ini.section(part);
            ini.set("BgType", "ImageFile");
            ini.set("ImageFile", "loki_caption.bmp");
            ini.set("ImageCount", "3");
            ini.set("ImageLayout", "Vertical");
            ini.set("SizingType", "Stretch");
            ini.set("Transparent", "True");
            ini.set("SizingMargins", captionMargin + ", " + captionMargin + ", "
                    + captionMargin + ", " + s.px(2));
            ini.set("ContentMargins", "0, 0, 0, 0");
            ini.set("CaptionMargins", s.px(10) + ", " + s.px(4) + ", 0, 0");
            ini.set("TextColor", s.rgb(s.onSurface));
        }
        ini.section("Window.Caption(Inactive)");
        ini.set("TextColor", s.rgb(s.onVariant));

        ini.section("Window.SmallCaption");
        ini.set("BgType", "ImageFile");
        ini.set("ImageFile", "loki_caption.bmp");
        ini.set("ImageCount", "3");
        ini.set("ImageLayout", "Vertical");
        ini.set("SizingType", "Stretch");
        ini.set("Transparent", "True");
        ini.set("SizingMargins", captionMargin + ", " + captionMargin + ", "
                + captionMargin + ", " + s.px(2));
        ini.set("CaptionMargins", s.px(8) + ", " + s.px(3) + ", 0, 0");
        ini.set("TextColor", s.rgb(s.onSurface));

        /*
         * Frames as solid strips.
         *
         * One pixel wide, so there is nothing to nine-slice and a border-fill is
         * both exact and immune to the stretching that would blur an image this
         * thin. The frame is the outline colour rather than the caption's, which
         * is what keeps a window separable from the one behind it.
         */
        for (String part : new String[]{"Window.FrameLeft", "Window.FrameRight",
                "Window.FrameBottom", "Window.SmallFrameLeft", "Window.SmallFrameRight",
                "Window.SmallFrameBottom"}) {
            ini.section(part);
            ini.set("BgType", "BorderFill");
            ini.set("BorderSize", "0");
            ini.set("FillColor", s.rgb(s.outline));
        }

        ini.section("Window.Border");
        ini.set("Width", "1");

        ini.section("Window.Dialog");
        ini.set("BgType", "BorderFill");
        ini.set("BorderSize", "0");
        ini.set("FillColor", s.rgb(s.surface));

        captionButtons(ini, s, art);
    }

    /**
     * Minimise, maximise, restore, close and help.
     *
     * Eight cells apiece: normal, hot, pushed and disabled, then the same four
     * again for when the window is not focused. They are drawn identically —
     * what changes with focus is the caption behind them — but the strip has to
     * be the length uxtheme expects or the states index into the wrong cells.
     */
    private static void captionButtons(Ini ini, Style s, List<Resource> art) {
        int margin = s.px(5);
        int radius = s.px(4);
        int glyph = s.px(10);

        int[] fills = new int[8];
        int[] closeFills = new int[8];
        for (int i = 0; i < 8; i++) {
            int group = i % 4;
            fills[i] = group == 1 ? s.hover : group == 2 ? s.pressed : Color.TRANSPARENT;
            closeFills[i] = group == 1 ? s.closeHot
                    : group == 2 ? blend(s.closeHot, s.onSurface, 0.15f) : Color.TRANSPARENT;
        }
        art.add(roundedStrip("loki_caption_button.bmp", s, margin, radius, fills, transparent(8)));
        art.add(roundedStrip("loki_close_button.bmp", s, margin, radius, closeFills, transparent(8)));

        int[] colors = new int[8];
        for (int i = 0; i < 8; i++) colors[i] = (i % 4) == 3 ? s.disabledText : s.onSurface;

        art.add(glyphStrip("loki_min_glyph.bmp", s, glyph, 8, colors, Glyph.MINIMISE));
        art.add(glyphStrip("loki_max_glyph.bmp", s, glyph, 8, colors, Glyph.MAXIMISE));
        art.add(glyphStrip("loki_restore_glyph.bmp", s, glyph, 8, colors, Glyph.RESTORE));
        art.add(glyphStrip("loki_close_glyph.bmp", s, glyph, 8, colors, Glyph.CLOSE));
        art.add(glyphStrip("loki_help_glyph.bmp", s, glyph, 8, colors, Glyph.HELP));

        String[][] buttons = {
                {"Window.MinButton", "loki_caption_button.bmp", "loki_min_glyph.bmp"},
                {"Window.MdiMinButton", "loki_caption_button.bmp", "loki_min_glyph.bmp"},
                {"Window.MaxButton", "loki_caption_button.bmp", "loki_max_glyph.bmp"},
                {"Window.RestoreButton", "loki_caption_button.bmp", "loki_restore_glyph.bmp"},
                {"Window.MdiRestoreButton", "loki_caption_button.bmp", "loki_restore_glyph.bmp"},
                {"Window.HelpButton", "loki_caption_button.bmp", "loki_help_glyph.bmp"},
                {"Window.MdiHelpButton", "loki_caption_button.bmp", "loki_help_glyph.bmp"},
                {"Window.CloseButton", "loki_close_button.bmp", "loki_close_glyph.bmp"},
                {"Window.SmallCloseButton", "loki_close_button.bmp", "loki_close_glyph.bmp"},
                {"Window.MdiCloseButton", "loki_close_button.bmp", "loki_close_glyph.bmp"},
        };
        for (String[] button : buttons) {
            ini.section(button[0]);
            nineSlicePart(ini, button[1], 8, margin);
            ini.set("GlyphType", "ImageGlyph");
            ini.set("GlyphTransparent", "True");
            ini.set("ImageSelectType", "Dpi");
            ini.set("ImageFile1", button[2]);
            ini.set("MinDpi1", "96");
        }

        /* The system menu button is an icon slot; the theme has nothing to add. */
        ini.section("Window.SysButton");
        ini.set("BgType", "None");
        ini.section("Window.MdiSysButton");
        ini.set("BgType", "None");
    }

    // ---------------------------------------------------------- ini plumbing

    /** Shared shape for a nine-sliced, state-strip image part. */
    private static void nineSlicePart(Ini ini, String file, int states, int margin) {
        ini.set("BgType", "ImageFile");
        ini.set("ImageFile", file);
        ini.set("ImageCount", String.valueOf(states));
        ini.set("ImageLayout", "Vertical");
        ini.set("SizingType", "Stretch");
        ini.set("Transparent", "True");
        ini.set("SizingMargins", margins(margin));
        ini.set("ContentMargins", "0, 0, 0, 0");
    }

    /** Shared shape for something drawn at its own size, wherever it is asked for. */
    private static void trueSizeGlyphPart(Ini ini, String file, int states) {
        ini.set("BgType", "ImageFile");
        ini.set("ImageFile", file);
        ini.set("ImageCount", String.valueOf(states));
        ini.set("ImageLayout", "Vertical");
        ini.set("SizingType", "TrueSize");
        ini.set("SizingMargins", "0, 0, 0, 0");
        ini.set("ContentMargins", "0, 0, 0, 0");
        ini.set("Transparent", "True");
        ini.set("UniformSizing", "True");
    }

    private static String margins(int all) {
        return all + ", " + all + ", " + all + ", " + all;
    }

    private static int[] transparent(int count) {
        return fill(count, Color.TRANSPARENT);
    }

    private static int[] fill(int count, int value) {
        int[] out = new int[count];
        for (int i = 0; i < count; i++) out[i] = value;
        return out;
    }

    /** Accumulates the stylesheet, in the CRLF UTF-16 form uxtheme reads. */
    private static final class Ini {
        private final StringBuilder text = new StringBuilder();

        void section(String name) {
            if (text.length() > 0) text.append("\r\n");
            text.append('[').append(name).append("]\r\n");
        }

        void set(String key, String value) {
            text.append(key).append(" = ").append(value).append("\r\n");
        }

        void blank() {
            text.append("\r\n");
        }

        @Override
        public String toString() {
            return text.toString();
        }
    }

    // -------------------------------------------------------------- painting

    private static final class Chevron {
        static final int UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3;
    }

    private static final class Glyph {
        static final int MINIMISE = 0, MAXIMISE = 1, RESTORE = 2, CLOSE = 3, HELP = 4;
    }

    /**
     * A vertical strip of rounded rectangles, one per state.
     *
     * The cell is only as large as the corners plus two pixels of stretchable
     * middle, because a nine-slice takes the corners from the image and
     * everything between them from those two pixels. Drawing a whole button and
     * stretching its centre would be the same picture at several times the size.
     */
    private static Resource roundedStrip(String name, Style s, int margin, int radius,
                                         int[] fills, int[] edges) {
        int size = margin * 2 + 2;
        Bitmap strip = Bitmap.createBitmap(size, size * fills.length, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        for (int state = 0; state < fills.length; state++) {
            canvas.save();
            canvas.translate(0, (float) state * size);
            roundedCell(canvas, s, size, size, radius, fills[state], edges[state]);
            canvas.restore();
        }
        return Resource.bitmap(name, strip);
    }

    /** As above, but rounded only along the top — a tab, or a caption. */
    private static Resource tabStrip(String name, Style s, int margin, int radius,
                                     int[] fills, int[] edges, int[] rules) {
        int size = margin * 2 + 2;
        Bitmap strip = Bitmap.createBitmap(size, size * fills.length, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float border = Math.max(1f, s.scale);

        for (int state = 0; state < fills.length; state++) {
            canvas.save();
            canvas.translate(0, (float) state * size);
            topRoundedCell(canvas, size, size, radius, border, fills[state], edges[state]);

            if (Color.alpha(rules[state]) != 0) {
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(rules[state]);
                canvas.drawRect(0, 0, size, Math.max(2f, s.scale * 2f), paint);
            }
            canvas.restore();
        }
        return Resource.bitmap(name, strip);
    }

    /** A caption: rounded at the top, square at the bottom, nothing outside. */
    private static Resource captionStrip(String name, Style s, int margin, int radius, int[] fills) {
        int size = margin * 2 + 2;
        Bitmap strip = Bitmap.createBitmap(size, size * fills.length, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        for (int state = 0; state < fills.length; state++) {
            canvas.save();
            canvas.translate(0, (float) state * size);
            topRoundedCell(canvas, size, size, radius, Math.max(1f, s.scale),
                    fills[state], s.outline);
            canvas.restore();
        }
        return Resource.bitmap(name, strip);
    }

    /** A column header: a face with a hairline down its right-hand edge. */
    private static Resource headerStrip(String name, Style s, int[] fills, int[] edges) {
        int width = s.px(6);
        int height = s.px(6);
        Bitmap strip = Bitmap.createBitmap(width, height * fills.length, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        Paint paint = new Paint();
        for (int state = 0; state < fills.length; state++) {
            float top = (float) state * height;
            paint.setColor(fills[state]);
            canvas.drawRect(0, top, width, top + height, paint);
            paint.setColor(edges[state]);
            canvas.drawRect(width - Math.max(1f, s.scale), top, width, top + height, paint);
        }
        return Resource.bitmap(name, strip);
    }

    /** A slider thumb, drawn at its true size in each state. */
    private static Resource pillStrip(String name, Style s, int width, int height,
                                      int[] fills, boolean vertical) {
        Bitmap strip = Bitmap.createBitmap(width, height * fills.length, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float radius = (vertical ? height : width) / 2f;

        for (int state = 0; state < fills.length; state++) {
            canvas.save();
            canvas.translate(0, (float) state * height);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(fills[state]);
            canvas.drawRoundRect(new RectF(0, 0, width, height), radius, radius, paint);
            canvas.restore();
        }
        return Resource.bitmap(name, strip);
    }

    /** A strip of chevrons, one per state, each pointing where it is told. */
    private static Resource chevronStrip(String name, Style s, int size, int states,
                                         int[] colors, int[] directions) {
        Bitmap strip = Bitmap.createBitmap(size, size * states, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        for (int state = 0; state < states; state++) {
            canvas.save();
            canvas.translate(0, (float) state * size);
            drawChevron(canvas, s, size, colors[state], directions[state]);
            canvas.restore();
        }
        return Resource.bitmap(name, strip);
    }

    /** A strip of one window-button glyph, in each state's colour. */
    private static Resource glyphStrip(String name, Style s, int size, int states,
                                       int[] colors, int glyph) {
        Bitmap strip = Bitmap.createBitmap(size, size * states, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(strip);
        for (int state = 0; state < states; state++) {
            canvas.save();
            canvas.translate(0, (float) state * size);
            drawWindowGlyph(canvas, s, size, colors[state], glyph);
            canvas.restore();
        }
        return Resource.bitmap(name, strip);
    }

    private static void roundedCell(Canvas canvas, Style s, int width, int height,
                                    int radius, int fill, int edge) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float border = Math.max(1f, s.scale);
        RectF rect = new RectF(border / 2f, border / 2f, width - border / 2f, height - border / 2f);

        if (Color.alpha(fill) != 0) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(fill);
            canvas.drawRoundRect(rect, radius, radius, paint);
        }
        if (Color.alpha(edge) != 0) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(border);
            paint.setColor(edge);
            canvas.drawRoundRect(rect, radius, radius, paint);
        }
    }

    /**
     * Rounded along the top edge only.
     *
     * Drawn as a round rect twice as tall as the cell and then clipped, which is
     * the cheapest way to get two rounded corners and two square ones without
     * building the path by hand.
     */
    private static void topRoundedCell(Canvas canvas, int width, int height, int radius,
                                       float border, int fill, int edge) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        RectF rect = new RectF(border / 2f, border / 2f,
                width - border / 2f, height + radius * 2f);

        canvas.save();
        canvas.clipRect(0, 0, width, height);
        if (Color.alpha(fill) != 0) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(fill);
            canvas.drawRoundRect(rect, radius, radius, paint);
        }
        if (Color.alpha(edge) != 0) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(border);
            paint.setColor(edge);
            canvas.drawRoundRect(rect, radius, radius, paint);
        }
        canvas.restore();
    }

    private static void drawChevron(Canvas canvas, Style s, int size, int color, int direction) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1.4f, s.scale * 1.5f));
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setColor(color);

        float centre = size / 2f;
        float reach = size * 0.24f;
        Path path = new Path();
        switch (direction) {
            case Chevron.UP:
                path.moveTo(centre - reach, centre + reach * 0.6f);
                path.lineTo(centre, centre - reach * 0.6f);
                path.lineTo(centre + reach, centre + reach * 0.6f);
                break;
            case Chevron.DOWN:
                path.moveTo(centre - reach, centre - reach * 0.6f);
                path.lineTo(centre, centre + reach * 0.6f);
                path.lineTo(centre + reach, centre - reach * 0.6f);
                break;
            case Chevron.LEFT:
                path.moveTo(centre + reach * 0.6f, centre - reach);
                path.lineTo(centre - reach * 0.6f, centre);
                path.lineTo(centre + reach * 0.6f, centre + reach);
                break;
            default:
                path.moveTo(centre - reach * 0.6f, centre - reach);
                path.lineTo(centre + reach * 0.6f, centre);
                path.lineTo(centre - reach * 0.6f, centre + reach);
                break;
        }
        canvas.drawPath(path, paint);
    }

    private static void drawWindowGlyph(Canvas canvas, Style s, int size, int color, int glyph) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1.2f, s.scale * 1.3f));
        paint.setStrokeCap(Paint.Cap.SQUARE);
        paint.setColor(color);

        float inset = size * 0.26f;
        float left = inset, top = inset, right = size - inset, bottom = size - inset;
        float centre = size / 2f;

        switch (glyph) {
            case Glyph.MINIMISE:
                canvas.drawLine(left, centre, right, centre, paint);
                break;
            case Glyph.MAXIMISE:
                canvas.drawRect(left, top, right, bottom, paint);
                break;
            case Glyph.RESTORE: {
                float step = size * 0.14f;
                canvas.drawRect(left, top + step, right - step, bottom, paint);
                Path back = new Path();
                back.moveTo(left + step, top + step);
                back.lineTo(left + step, top);
                back.lineTo(right, top);
                back.lineTo(right, bottom - step);
                back.lineTo(right - step, bottom - step);
                canvas.drawPath(back, paint);
                break;
            }
            case Glyph.CLOSE:
                canvas.drawLine(left, top, right, bottom, paint);
                canvas.drawLine(right, top, left, bottom, paint);
                break;
            default: {
                /* A question mark, as an arc over a dot. */
                paint.setStrokeCap(Paint.Cap.ROUND);
                RectF arc = new RectF(left, top, right, top + (bottom - top) * 0.6f);
                canvas.drawArc(arc, 160, 240, false, paint);
                canvas.drawLine(centre, top + (bottom - top) * 0.45f, centre, bottom - size * 0.16f, paint);
                paint.setStyle(Paint.Style.FILL);
                canvas.drawCircle(centre, bottom, Math.max(1f, s.scale * 0.9f), paint);
                break;
            }
        }
    }

    private static void drawCheck(Canvas canvas, Style s, int width, int height, int color) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1.6f, s.scale * 1.8f));
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setColor(color);

        Path path = new Path();
        path.moveTo(width * 0.26f, height * 0.52f);
        path.lineTo(width * 0.44f, height * 0.70f);
        path.lineTo(width * 0.76f, height * 0.32f);
        canvas.drawPath(path, paint);
    }

    private static void drawDash(Canvas canvas, Style s, int width, int height, int color) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1.6f, s.scale * 1.8f));
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setColor(color);
        canvas.drawLine(width * 0.26f, height / 2f, width * 0.74f, height / 2f, paint);
    }

    // -------------------------------------------------------------- resources

    /**
     * One entry in the resource tree.
     *
     * The type is either a string — which is how the theme metadata is named —
     * or the numeric {@code RT_BITMAP}, which is how the artwork is named. The
     * distinction is not cosmetic: named and numeric entries live in separate,
     * separately sorted runs of the same directory.
     */
    private static final class Resource {
        final String typeName;
        final int typeId;
        final String name;
        final int id;
        final byte[] data;

        private Resource(String typeName, int typeId, String name, int id, byte[] data) {
            this.typeName = typeName;
            this.typeId = typeId;
            this.name = name;
            this.id = id;
            this.data = data;
        }

        static Resource named(String type, int id, byte[] data) {
            return new Resource(type, 0, null, id, data);
        }

        static Resource text(String name, String content) {
            return new Resource("TEXTFILE", 0, name, 0, utf16(content));
        }

        /**
         * Artwork, as an {@code RT_BITMAP}.
         *
         * The resource form of a bitmap is a DIB with no file header — uxtheme
         * hands it straight to {@code LoadImage}. Thirty-two bits per pixel with
         * a real alpha channel, because the corners of everything here are
         * round and the pixels outside them have to be nothing rather than a
         * colour that happens to match one background.
         */
        static Resource bitmap(String file, Bitmap bitmap) {
            int width = bitmap.getWidth();
            int height = bitmap.getHeight();
            int[] pixels = new int[width * height];
            bitmap.getPixels(pixels, 0, width, 0, 0, width, height);

            ByteBuffer buffer = ByteBuffer.allocate(40 + width * height * 4)
                    .order(ByteOrder.LITTLE_ENDIAN);
            buffer.putInt(40);              // biSize
            buffer.putInt(width);           // biWidth
            buffer.putInt(height);          // biHeight, positive: bottom-up
            buffer.putShort((short) 1);     // biPlanes
            buffer.putShort((short) 32);    // biBitCount
            buffer.putInt(0);               // biCompression, BI_RGB
            buffer.putInt(width * height * 4);
            buffer.putInt(2835);            // biXPelsPerMeter, 72 DPI
            buffer.putInt(2835);
            buffer.putInt(0);               // biClrUsed
            buffer.putInt(0);               // biClrImportant

            for (int y = height - 1; y >= 0; y--) {
                int row = y * width;
                for (int x = 0; x < width; x++) {
                    int pixel = pixels[row + x];
                    buffer.put((byte) Color.blue(pixel));
                    buffer.put((byte) Color.green(pixel));
                    buffer.put((byte) Color.red(pixel));
                    buffer.put((byte) Color.alpha(pixel));
                }
            }
            bitmap.recycle();

            return new Resource(null, 2, resourceNameOf(file), 0, buffer.array());
        }

        boolean typeIsName() {
            return typeName != null;
        }

        boolean nameIsName() {
            return name != null;
        }
    }

    /**
     * The filename in the stylesheet, as uxtheme will look it up.
     *
     * Uppercased with the separators turned into underscores — the same
     * transformation uxtheme applies before calling {@code FindResource}, so
     * {@code loki_button.bmp} has to be stored as {@code LOKI_BUTTON_BMP}.
     */
    private static String resourceNameOf(String file) {
        StringBuilder out = new StringBuilder(file.length());
        for (int i = 0; i < file.length(); i++) {
            char c = file.charAt(i);
            if (c == '.' || c == '/' || c == '\\') c = '_';
            out.append(Character.toUpperCase(c));
        }
        return out.toString();
    }

    // --------------------------------------------------------- PE generation

    private static final int SECTION_RVA = 0x1000;
    private static final int ALIGNMENT = 0x1000;

    /**
     * A DLL with no code.
     *
     * One section, no imports, no relocations, no entry point: everything a
     * {@code LOAD_LIBRARY_AS_DATAFILE} mapping needs and nothing it does not.
     * The header values are the ones Wine's own style carries, because a style
     * that loads is worth more than a style with tidier fields.
     */
    private static byte[] buildPortableExecutable(List<Resource> resources) {
        byte[] rsrc = buildResourceSection(resources);
        int rawSize = align(rsrc.length, ALIGNMENT);

        ByteBuffer out = ByteBuffer.allocate(SECTION_RVA + rawSize).order(ByteOrder.LITTLE_ENDIAN);

        // MZ header, with the PE header immediately after a short stub.
        out.put((byte) 'M').put((byte) 'Z');
        out.position(0x3C);
        out.putInt(0x60);
        out.position(0x40);
        out.put("Wine builtin DLL\0".getBytes());

        out.position(0x60);
        out.put((byte) 'P').put((byte) 'E').put((byte) 0).put((byte) 0);

        // COFF header.
        out.putShort((short) 0xAA64);   // ARM64. Irrelevant to a datafile mapping,
        out.putShort((short) 1);        // but it costs nothing to match the loader's
        out.putInt(0);                  // expectations for this prefix.
        out.putInt(0);
        out.putInt(0);
        out.putShort((short) 240);      // SizeOfOptionalHeader, PE32+
        out.putShort((short) 0x2022);   // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE | DLL

        // Optional header, PE32+.
        out.putShort((short) 0x20B);
        out.put((byte) 7).put((byte) 10);
        out.putInt(0);                  // SizeOfCode
        out.putInt(rawSize);            // SizeOfInitializedData
        out.putInt(0);                  // SizeOfUninitializedData
        out.putInt(0);                  // AddressOfEntryPoint
        out.putInt(0);                  // BaseOfCode
        out.putLong(0x180000000L);      // ImageBase
        out.putInt(ALIGNMENT);          // SectionAlignment
        out.putInt(ALIGNMENT);          // FileAlignment
        out.putShort((short) 1).putShort((short) 0);   // OS version
        out.putShort((short) 0).putShort((short) 0);   // Image version
        out.putShort((short) 4).putShort((short) 0);   // Subsystem version
        out.putInt(0);                  // Win32VersionValue
        out.putInt(SECTION_RVA + rawSize);             // SizeOfImage
        out.putInt(ALIGNMENT);          // SizeOfHeaders
        out.putInt(0);                  // CheckSum
        out.putShort((short) 3);        // Subsystem
        out.putShort((short) 0x160);    // DllCharacteristics
        out.putLong(0x100000L);         // SizeOfStackReserve
        out.putLong(0x1000L);           // SizeOfStackCommit
        out.putLong(0x100000L);         // SizeOfHeapReserve
        out.putLong(0x1000L);           // SizeOfHeapCommit
        out.putInt(0);                  // LoaderFlags
        out.putInt(16);                 // NumberOfRvaAndSizes

        for (int i = 0; i < 16; i++) {
            if (i == 2) {
                out.putInt(SECTION_RVA);
                out.putInt(rsrc.length);
            } else {
                out.putInt(0);
                out.putInt(0);
            }
        }

        // The one section.
        out.put(".rsrc\0\0\0".getBytes());
        out.putInt(rsrc.length);        // VirtualSize
        out.putInt(SECTION_RVA);        // VirtualAddress
        out.putInt(rawSize);            // SizeOfRawData
        out.putInt(SECTION_RVA);        // PointerToRawData
        out.putInt(0).putInt(0);        // Relocations, line numbers
        out.putShort((short) 0).putShort((short) 0);
        out.putInt(0x40000040);         // INITIALIZED_DATA | MEM_READ

        out.position(SECTION_RVA);
        out.put(rsrc);
        return out.array();
    }

    /**
     * The three-level resource tree: type, then name, then language.
     *
     * Both the sorting and the ordering matter. {@code FindResource} binary
     * searches each directory, so an out-of-order entry is not merely untidy —
     * it is invisible. Named entries come first, sorted by their UTF-16 value,
     * then numeric entries sorted by id.
     */
    private static byte[] buildResourceSection(List<Resource> resources) {
        List<Resource> sorted = new ArrayList<>(resources);
        Collections.sort(sorted, new Comparator<Resource>() {
            @Override
            public int compare(Resource a, Resource b) {
                int byType = compareKeys(a.typeIsName(), a.typeName, a.typeId,
                        b.typeIsName(), b.typeName, b.typeId);
                if (byType != 0) return byType;
                return compareKeys(a.nameIsName(), a.name, a.id,
                        b.nameIsName(), b.name, b.id);
            }
        });

        // Group into types, preserving the sort.
        List<List<Resource>> types = new ArrayList<>();
        for (Resource resource : sorted) {
            if (types.isEmpty() || !sameType(types.get(types.size() - 1).get(0), resource)) {
                types.add(new ArrayList<Resource>());
            }
            types.get(types.size() - 1).add(resource);
        }

        int namedTypes = 0;
        for (List<Resource> type : types) if (type.get(0).typeIsName()) namedTypes++;

        /*
         * Laid out in one pass, because every size is known in advance: the root
         * directory, then one directory per type, then one per name, then a data
         * entry apiece, then the strings, then the blobs.
         */
        int cursor = 16 + 8 * types.size();
        int[] typeDirOffset = new int[types.size()];
        for (int i = 0; i < types.size(); i++) {
            typeDirOffset[i] = cursor;
            cursor += 16 + 8 * types.get(i).size();
        }

        int[][] nameDirOffset = new int[types.size()][];
        for (int i = 0; i < types.size(); i++) {
            nameDirOffset[i] = new int[types.get(i).size()];
            for (int j = 0; j < types.get(i).size(); j++) {
                nameDirOffset[i][j] = cursor;
                cursor += 16 + 8;   // one language entry
            }
        }

        int[][] dataEntryOffset = new int[types.size()][];
        for (int i = 0; i < types.size(); i++) {
            dataEntryOffset[i] = new int[types.get(i).size()];
            for (int j = 0; j < types.get(i).size(); j++) {
                dataEntryOffset[i][j] = cursor;
                cursor += 16;
            }
        }

        // Strings: every named type once, every named resource once.
        List<String> strings = new ArrayList<>();
        int[] typeStringOffset = new int[types.size()];
        int[][] nameStringOffset = new int[types.size()][];
        for (int i = 0; i < types.size(); i++) {
            Resource head = types.get(i).get(0);
            if (head.typeIsName()) {
                typeStringOffset[i] = cursor;
                strings.add(head.typeName);
                cursor += align(2 + head.typeName.length() * 2, 2);
            }
            nameStringOffset[i] = new int[types.get(i).size()];
            for (int j = 0; j < types.get(i).size(); j++) {
                Resource resource = types.get(i).get(j);
                if (resource.nameIsName()) {
                    nameStringOffset[i][j] = cursor;
                    strings.add(resource.name);
                    cursor += align(2 + resource.name.length() * 2, 2);
                }
            }
        }

        cursor = align(cursor, 8);
        int[][] blobOffset = new int[types.size()][];
        for (int i = 0; i < types.size(); i++) {
            blobOffset[i] = new int[types.get(i).size()];
            for (int j = 0; j < types.get(i).size(); j++) {
                blobOffset[i][j] = cursor;
                cursor += align(types.get(i).get(j).data.length, 8);
            }
        }

        ByteBuffer out = ByteBuffer.allocate(cursor).order(ByteOrder.LITTLE_ENDIAN);

        // Root directory.
        putDirectoryHeader(out, namedTypes, types.size() - namedTypes);
        for (int i = 0; i < types.size(); i++) {
            Resource head = types.get(i).get(0);
            out.putInt(head.typeIsName() ? (typeStringOffset[i] | 0x80000000) : head.typeId);
            out.putInt(typeDirOffset[i] | 0x80000000);
        }

        // One directory per type.
        for (int i = 0; i < types.size(); i++) {
            List<Resource> group = types.get(i);
            int named = 0;
            for (Resource resource : group) if (resource.nameIsName()) named++;
            putDirectoryHeader(out, named, group.size() - named);
            for (int j = 0; j < group.size(); j++) {
                Resource resource = group.get(j);
                out.putInt(resource.nameIsName()
                        ? (nameStringOffset[i][j] | 0x80000000) : resource.id);
                out.putInt(nameDirOffset[i][j] | 0x80000000);
            }
        }

        // One directory per name, each holding a single neutral language.
        for (int i = 0; i < types.size(); i++) {
            for (int j = 0; j < types.get(i).size(); j++) {
                putDirectoryHeader(out, 0, 1);
                out.putInt(0);
                out.putInt(dataEntryOffset[i][j]);
            }
        }

        // The data entries, whose offsets are image-relative rather than section-relative.
        for (int i = 0; i < types.size(); i++) {
            for (int j = 0; j < types.get(i).size(); j++) {
                out.putInt(SECTION_RVA + blobOffset[i][j]);
                out.putInt(types.get(i).get(j).data.length);
                out.putInt(0);
                out.putInt(0);
            }
        }

        // The strings, in the order their offsets were assigned.
        for (String value : strings) {
            out.putShort((short) value.length());
            out.put(utf16(value));
            while (out.position() % 2 != 0) out.put((byte) 0);
        }

        for (int i = 0; i < types.size(); i++) {
            for (int j = 0; j < types.get(i).size(); j++) {
                out.position(blobOffset[i][j]);
                out.put(types.get(i).get(j).data);
            }
        }

        return out.array();
    }

    private static void putDirectoryHeader(ByteBuffer out, int named, int ids) {
        out.putInt(0);                  // Characteristics
        out.putInt(0);                  // TimeDateStamp
        out.putShort((short) 0);        // MajorVersion
        out.putShort((short) 0);        // MinorVersion
        out.putShort((short) named);
        out.putShort((short) ids);
    }

    private static boolean sameType(Resource a, Resource b) {
        if (a.typeIsName() != b.typeIsName()) return false;
        return a.typeIsName() ? a.typeName.equals(b.typeName) : a.typeId == b.typeId;
    }

    /** Named before numeric, then by value — the order FindResource searches in. */
    private static int compareKeys(boolean aIsName, String aName, int aId,
                                   boolean bIsName, String bName, int bId) {
        if (aIsName != bIsName) return aIsName ? -1 : 1;
        if (aIsName) return aName.compareTo(bName);
        return Integer.compare(aId, bId);
    }

    private static int align(int value, int to) {
        int remainder = value % to;
        return remainder == 0 ? value : value + (to - remainder);
    }
}
