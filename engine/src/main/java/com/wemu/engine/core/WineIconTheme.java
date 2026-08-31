package com.wemu.engine.core;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.RectF;
import android.graphics.Path;
import android.graphics.Shader;
import android.graphics.drawable.Drawable;
import android.util.Log;

import java.util.EnumMap;

import com.wemu.engine.R;
import com.wemu.engine.xenvironment.ImageFs;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The icons inside the container, repainted in the launcher's colours.
 *
 * The visual style covers everything Wine *draws* — buttons, captions, scroll
 * bars, menus. It cannot touch what Wine *shows*, and in a file manager that is
 * most of the screen: every folder, every drive, every document and every
 * toolbar button is a bitmap compiled into a binary, in whatever palette its
 * author chose. A container themed down to the caption gradient still opens onto
 * a grid of Windows-98 manila folders and glossy blue drives, which is the thing
 * you actually look at.
 *
 * <h2>Patching without rebuilding</h2>
 *
 * An icon in a PE lives in an {@code RT_ICON} resource whose size is recorded in
 * the resource directory. Replacing one normally means rebuilding the whole
 * {@code .rsrc} section and fixing every RVA in it — on a 10 MB {@code
 * shell32.dll}, a great deal of surgery for a change of colour.
 *
 * None of that is necessary here. An icon image is a {@code BITMAPINFOHEADER}
 * followed by pixels and a mask, so an icon of identical width, height and depth
 * occupies *exactly* the same number of bytes. Redrawing one is a write at the
 * same file offset, with the resource tree untouched and the file the same
 * length. Where a replacement is smaller — the PNG-compressed 256px entries —
 * the recorded size in the data entry is lowered to match, which is the one
 * field this touches outside the pixels themselves.
 *
 * <h2>What gets repainted</h2>
 *
 * <ul>
 *   <li>{@code shell32.dll} — folders, documents, drives, the computer, search
 *       and the recycle bin. This is Wine's system image list, which is where
 *       {@code SHGetFileInfo} gets everything the file manager lists.</li>
 *   <li>{@code wfm.exe} — the file manager's own toolbar, its window icon, and
 *       the eight frames of its busy spinner, which is hardcoded blue and was
 *       the one moving thing on screen that never matched.</li>
 *   <li>The loose {@code .ico} files in {@code system32}, which is how the file
 *       manager gets an icon for a known extension.</li>
 * </ul>
 *
 * Only the 32-bit entries and the PNG entries are rewritten. The 1-, 4-, 8- and
 * 24-bit copies of each icon exist for displays that have not been sold this
 * century; the lookup picks the depth closest to the screen's, so on anything
 * this runs on they are never chosen.
 *
 * <h2>What is not here</h2>
 *
 * The Start button. Wine's taskbar draws it as a plain push button with the word
 * "Start" and no icon resource at all, so it takes its appearance from the
 * visual style along with every other button, and there is nothing to replace.
 */
public final class WineIconTheme {

    private static final String TAG = "WineIconTheme";

    /** Bumped whenever the artwork changes, to re-theme containers already booted. */
    public static final int REVISION = 2;

    /** Set for the length of one {@link #apply} pass; see there for why. */
    private static volatile Resources resources;

    private WineIconTheme() {}

    // ------------------------------------------------------------------ entry

    public static void apply(Context context, WineVisualStyle.Style style) {
        ImageFs imageFs = ImageFs.find(context);
        File rootDir = imageFs.getRootDir();

        /*
         * The artwork's resources, parked for the length of one pass.
         *
         * A static is not how anything should normally reach its dependencies,
         * and the alternative here is worse: `render` sits four calls below this
         * one, behind `PeIcons.repaintGroup` and `repaintIcon`, neither of which
         * has any business knowing what a `Resources` is. Threading a context
         * through both to reach the bottom would put it in two signatures that
         * only pass it on.
         *
         * This is a batch that runs from one entry point and finishes before it
         * returns, so the field is live for exactly the duration of the call and
         * cleared in a finally. Nothing reads it afterwards, and a second theme
         * pass sets it again before anything looks.
         */
        resources = context.getResources();
        try {
            themeAll(imageFs, rootDir, style);
        } finally {
            resources = null;
        }
    }

    /**
     * Themes only files owned by the prefix.
     *
     * Downloaded Wine/Proton content is shared and integrity-checked. Patching
     * shell32 under {@code ImageFs.getWinePath()} would mutate that installed
     * package for every container and invalidate the official Steam runtime.
     */
    public static void applyPrefixOnly(Context context, WineVisualStyle.Style style) {
        ImageFs imageFs = ImageFs.find(context);
        File rootDir = imageFs.getRootDir();
        resources = context.getResources();
        try {
            themePrefixFiles(rootDir, style);
        } finally {
            resources = null;
        }
    }

    private static void themeAll(ImageFs imageFs, File rootDir, WineVisualStyle.Style style) {
        try {
            themeShell32(new File(imageFs.getWinePath(), "lib/wine/aarch64-windows/shell32.dll"), style);
            themeShell32(new File(imageFs.getWinePath(), "lib/wine/i386-windows/shell32.dll"), style);
        } catch (Exception e) {
            Log.w(TAG, "Could not theme the shell icons", e);
        }

        themePrefixFiles(rootDir, style);
    }

    private static void themePrefixFiles(File rootDir, WineVisualStyle.Style style) {
        try {
            themeFileManager(new File(rootDir, ImageFs.WINEPREFIX + "/drive_c/windows/wfm.exe"), style);
        } catch (Exception e) {
            Log.w(TAG, "Could not theme the file manager", e);
        }

        try {
            themeFileTypeIcons(rootDir, style);
        } catch (Exception e) {
            Log.w(TAG, "Could not write the file-type icons", e);
        }
    }

    // ----------------------------------------------------------------- shell32

    /**
     * Wine's system image list.
     *
     * Patched from a pristine copy rather than from whatever is on disk, because
     * this runs again on every theme change and repainting a repaint would work
     * but leaves no way back to the original — and because a half-written DLL
     * from an interrupted write is a container that cannot start.
     */
    private static void themeShell32(File dll, WineVisualStyle.Style style) throws IOException {
        if (!dll.isFile()) return;

        File pristine = new File(dll.getPath() + ".loki-orig");
        if (!pristine.isFile()) FileUtils.copy(dll, pristine);

        byte[] image = readAll(pristine);
        PeIcons icons = PeIcons.parse(image);
        if (icons == null) {
            Log.w(TAG, "No icon resources in " + dll.getName());
            return;
        }

        int painted = 0;
        for (Map.Entry<Integer, Art> entry : SHELL_GROUPS.entrySet()) {
            painted += icons.repaintGroup(entry.getKey(), entry.getValue(), style);
        }

        writeAtomically(dll, image);
        Log.d(TAG, "Repainted " + painted + " icons in " + dll.getName());
    }

    /**
     * Which shell icon is which.
     *
     * Identified by rendering the shipped resources and looking at them, because
     * the numbering is Wine's own and is documented nowhere — the constants in
     * `shresdef.h` name a handful and leave the rest as bare ordinals.
     */
    private static final Map<Integer, Art> SHELL_GROUPS = new HashMap<>();
    static {
        for (int id : new int[]{4, 20, 29, 31, 34, 37, 38, 39, 43, 51}) {
            SHELL_GROUPS.put(id, Art.FOLDER);
        }
        SHELL_GROUPS.put(5, Art.FOLDER_OPEN);
        for (int id : new int[]{1, 2, 133}) SHELL_GROUPS.put(id, Art.DOCUMENT);
        for (int id : new int[]{6, 7, 8, 9, 10, 54}) SHELL_GROUPS.put(id, Art.DRIVE);
        SHELL_GROUPS.put(11, Art.DRIVE_OFFLINE);
        SHELL_GROUPS.put(12, Art.OPTICAL);
        SHELL_GROUPS.put(41, Art.OPTICAL);
        SHELL_GROUPS.put(13, Art.MEMORY);
        SHELL_GROUPS.put(16, Art.COMPUTER);
        for (int id : new int[]{23, 46, 134, 135}) SHELL_GROUPS.put(id, Art.SEARCH);
        SHELL_GROUPS.put(33, Art.RECYCLE);
        SHELL_GROUPS.put(3, Art.WINDOW);
        SHELL_GROUPS.put(25, Art.WINDOW);
    }

    // ------------------------------------------------------------ file manager

    /**
     * The file manager's own artwork.
     *
     * Numbered rather than named — `wfm.exe` carries no symbols — so the mapping
     * below is what the shipped resources actually depict, read off a contact
     * sheet: one window icon, twelve toolbar glyphs, and eight frames of a
     * spinner.
     */
    private static final Art[] WFM_ICONS = {
            Art.APP,            // 1  · the window and taskbar icon
            Art.ARROW_RIGHT,    // 2  · go
            Art.REFRESH,        // 3
            Art.ZOOM_IN,        // 4
            Art.ZOOM_OUT,       // 5
            Art.CHEVRON_RIGHT,  // 6  · the breadcrumb separator
            Art.ARROW_UP,       // 7  · parent directory
            Art.COPY,           // 8
            Art.CUT,            // 9
            Art.PASTE,          // 10
            Art.DELETE,         // 11
            Art.NEW_FOLDER,     // 12
            Art.NEW_FILE,       // 13
    };

    private static void themeFileManager(File exe, WineVisualStyle.Style style) throws IOException {
        if (!exe.isFile()) return;

        File pristine = new File(exe.getPath() + ".loki-orig");
        if (!pristine.isFile()) FileUtils.copy(exe, pristine);

        byte[] image = readAll(pristine);
        PeIcons icons = PeIcons.parse(image);
        if (icons == null) return;

        int painted = 0;
        for (int i = 0; i < WFM_ICONS.length; i++) {
            painted += icons.repaintIcon(i + 1, WFM_ICONS[i], style, 0);
        }

        // Eight frames of one spinner, each a twelfth of a turn further round.
        for (int frame = 0; frame < 8; frame++) {
            painted += icons.repaintIcon(14 + frame, Art.SPINNER, style, frame);
        }

        writeAtomically(exe, image);
        Log.d(TAG, "Repainted " + painted + " icons in " + exe.getName());
    }

    // ---------------------------------------------------------- file type icons

    /**
     * One icon per known extension, as loose files.
     *
     * These are the file manager's only route to a per-type icon and they are
     * plain `.ico` files on disk, which makes them the one part of this that
     * needs no patching at all — they are simply written.
     *
     * A document in the theme's colours with a coloured tag and the extension on
     * it. The tag carries the distinction that the shipped set carried with
     * wholly different drawings, so a folder of mixed files still scans by
     * colour without eleven unrelated illustrations in it.
     */
    private static final String[][] FILE_TYPES = {
            {"txt", "TXT"}, {"ini", "INI"}, {"reg", "REG"}, {"bat", "BAT"},
            {"dll", "DLL"}, {"bmp", "BMP"}, {"png", "PNG"}, {"gif", "GIF"},
            {"image", "IMG"}, {"bsp", "BSP"},
    };

    private static void themeFileTypeIcons(File rootDir, WineVisualStyle.Style style) throws IOException {
        File[] dirs = {
                new File(rootDir, ImageFs.WINEPREFIX + "/drive_c/windows/system32"),
                new File(rootDir, ImageFs.WINEPREFIX + "/drive_c/windows/syswow64"),
        };

        for (File dir : dirs) {
            if (!dir.isDirectory()) continue;
            for (int i = 0; i < FILE_TYPES.length; i++) {
                File out = new File(dir, FILE_TYPES[i][0] + ".ico");
                // Only where the shipped set had one: writing icons for types
                // nothing asks about would be dead files in a system directory.
                if (!out.isFile()) continue;
                writeAtomically(out, icoFile(FILE_TYPES[i][1], hueFor(i, style), style));
            }
        }
    }

    /**
     * A tag colour per file type, spread around the wheel from the accent.
     *
     * Derived rather than chosen so that the whole set moves with the theme.
     * Even steps, because the point is that two types are distinguishable, not
     * that any particular type is any particular colour.
     */
    private static int hueFor(int index, WineVisualStyle.Style style) {
        float[] hsv = new float[3];
        Color.colorToHSV(style.primary, hsv);
        hsv[0] = (hsv[0] + index * (360f / FILE_TYPES.length)) % 360f;
        hsv[1] = Math.max(hsv[1], 0.45f);
        hsv[2] = Math.max(hsv[2], 0.70f);
        return Color.HSVToColor(hsv);
    }

    /** An `.ico` holding the three sizes Windows asks for. */
    private static byte[] icoFile(String label, int tag, WineVisualStyle.Style style) throws IOException {
        int[] sizes = {16, 32, 48};
        List<byte[]> images = new ArrayList<>();
        for (int size : sizes) images.add(iconDib(drawTaggedDocument(size, label, tag, style)));

        ByteArrayOutputStream out = new ByteArrayOutputStream();
        ByteBuffer header = ByteBuffer.allocate(6 + 16 * sizes.length).order(ByteOrder.LITTLE_ENDIAN);
        header.putShort((short) 0);     // reserved
        header.putShort((short) 1);     // type: icon
        header.putShort((short) sizes.length);

        int offset = 6 + 16 * sizes.length;
        for (int i = 0; i < sizes.length; i++) {
            byte[] image = images.get(i);
            header.put((byte) sizes[i]);
            header.put((byte) sizes[i]);
            header.put((byte) 0);       // colours in palette: 0 for true colour
            header.put((byte) 0);       // reserved
            header.putShort((short) 1); // planes
            header.putShort((short) 32);
            header.putInt(image.length);
            header.putInt(offset);
            offset += image.length;
        }
        out.write(header.array());
        for (byte[] image : images) out.write(image);
        return out.toByteArray();
    }

    // ----------------------------------------------------------------- artwork

    private enum Art {
        FOLDER, FOLDER_OPEN, DOCUMENT, DRIVE, DRIVE_OFFLINE, OPTICAL, MEMORY,
        COMPUTER, SEARCH, RECYCLE, WINDOW, APP,
        ARROW_RIGHT, ARROW_UP, CHEVRON_RIGHT, REFRESH, ZOOM_IN, ZOOM_OUT,
        COPY, CUT, PASTE, DELETE, NEW_FOLDER, NEW_FILE, SPINNER,
    }

    // ------------------------------------------------------------ drawn artwork

    /**
     * Which of the style's colours a shape wears.
     *
     * The artwork files are silhouettes — solid white, no colour of their own —
     * and the colour arrives here. That is what keeps drawn assets and the
     * launcher's accent compatible: a container themed purple gets purple
     * folders from the same files that give a green one green folders, and the
     * shapes stay editable in a vector tool without anyone having to keep eleven
     * palettes in step.
     */
    private enum Tone { ACCENT, ACCENT_DARK, ACCENT_FLAT, FACE, FACE_DARK, OUTLINE, DETAIL, ALERT }

    /** One silhouette and the colour it wears. */
    private static final class Layer {
        final int drawable;
        final Tone tone;

        Layer(int drawable, Tone tone) {
            this.drawable = drawable;
            this.tone = tone;
        }
    }

    /**
     * The icons that are drawn from artwork rather than from code, back to front.
     *
     * Anything absent from this map falls through to the procedural set below,
     * which is why this can be filled in an icon at a time: an `Art` with no
     * entry is not a hole in the theme, it is the old drawing still doing its
     * job. The two sets share a grid and a palette, so a half-converted set is
     * not a mismatched one.
     */
    private static final EnumMap<Art, Layer[]> ASSETS = new EnumMap<>(Art.class);
    static {
        ASSETS.put(Art.FOLDER, new Layer[]{
                new Layer(R.drawable.wine_ic_folder_back, Tone.ACCENT_DARK),
                new Layer(R.drawable.wine_ic_folder_front, Tone.ACCENT),
        });
        ASSETS.put(Art.FOLDER_OPEN, new Layer[]{
                new Layer(R.drawable.wine_ic_folder_back, Tone.ACCENT_DARK),
                new Layer(R.drawable.wine_ic_folder_front_open, Tone.ACCENT),
        });
        ASSETS.put(Art.DOCUMENT, new Layer[]{
                new Layer(R.drawable.wine_ic_document, Tone.FACE),
                new Layer(R.drawable.wine_ic_document_fold, Tone.ACCENT_FLAT),
        });
        ASSETS.put(Art.DRIVE, new Layer[]{
                new Layer(R.drawable.wine_ic_drive_lower, Tone.FACE_DARK),
                new Layer(R.drawable.wine_ic_drive_upper, Tone.FACE),
                new Layer(R.drawable.wine_ic_drive_lamp, Tone.ACCENT_FLAT),
        });
        ASSETS.put(Art.DRIVE_OFFLINE, new Layer[]{
                new Layer(R.drawable.wine_ic_drive_lower, Tone.FACE_DARK),
                new Layer(R.drawable.wine_ic_drive_upper, Tone.FACE),
                new Layer(R.drawable.wine_ic_drive_lamp, Tone.ALERT),
        });
    }

    /**
     * Composites one icon out of its artwork, or null if it has none.
     *
     * Null rather than an exception or a blank: the caller's next line is the
     * procedural set, so an icon with no artwork and an icon whose artwork
     * failed to load both end up drawn rather than missing. A container with one
     * unreadable resource should look slightly older, not lose its folders.
     */
    private static Bitmap renderAsset(Art art, int size, WineVisualStyle.Style s) {
        Layer[] layers = ASSETS.get(art);
        Resources res = resources;
        if (layers == null || res == null) return null;

        Bitmap bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);

        for (Layer layer : layers) {
            Drawable shape;
            try {
                shape = res.getDrawable(layer.drawable, null);
            } catch (Exception e) {
                Log.w(TAG, "Could not read the artwork for " + art + "; drawing it instead", e);
                bitmap.recycle();
                return null;
            }
            if (shape == null) {
                bitmap.recycle();
                return null;
            }

            // Fresh state per layer: tinting a drawable straight out of
            // `getDrawable` would set the tint on the constant state every copy
            // of it shares, so the next icon to use the same file inherits it.
            shape = shape.mutate();
            shape.setBounds(0, 0, size, size);

            if (layer.tone == Tone.ACCENT) {
                /*
                 * The gradient layer, which cannot be done with a tint.
                 *
                 * A tint is one colour. The accent objects in this set carry the
                 * launcher's two-stop gradient, so the shape is drawn into its
                 * own layer and the gradient poured through it with SRC_IN —
                 * the silhouette becomes a mask for a shader it could not
                 * otherwise hold.
                 */
                int saved = canvas.saveLayer(0, 0, size, size, null);
                shape.draw(canvas);
                paint.setShader(accent(size, s));
                paint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC_IN));
                canvas.drawRect(0, 0, size, size, paint);
                paint.setXfermode(null);
                paint.setShader(null);
                canvas.restoreToCount(saved);
            } else {
                // The default tint mode is SRC_IN, which is why the artwork is
                // white: the tint replaces the colour and keeps the coverage,
                // so the file supplies the shape and the style supplies the rest.
                shape.setTint(toneColor(layer.tone, s));
                shape.draw(canvas);
            }
        }
        return bitmap;
    }

    private static int toneColor(Tone tone, WineVisualStyle.Style s) {
        switch (tone) {
            case ACCENT_DARK: return shade(s.primary, 0.72f);
            case ACCENT_FLAT: return s.primary;
            case FACE:        return s.iconFace();
            case FACE_DARK:   return shade(s.iconFace(), 0.82f);
            case OUTLINE:     return s.outline;
            case DETAIL:      return s.onVariant;
            case ALERT:       return s.closeHot;
            default:          return s.primary;
        }
    }

    /**
     * One icon, drawn at whatever size was asked for.
     *
     * <h3>The rules the set is drawn to</h3>
     *
     * A set of icons is only a set if the drawings agree about something. These
     * agree about five things, and every shape below is built from them rather
     * than composed by eye:
     *
     * <ul>
     *   <li><b>A 48-unit grid with a 3-unit margin.</b> Everything is stated as
     *       a fraction of the canvas, so one drawing serves a 16px list row and
     *       a 256px preview. The margin is what stops a folder touching the edge
     *       of its cell while a disc, being round, looks smaller at the same
     *       bounds — round shapes are drawn a unit larger to compensate, which
     *       is the oldest trick in icon drawing and the one most often skipped.</li>
     *   <li><b>Accent objects carry the gradient; neutral objects carry the
     *       accent as a detail.</b> A folder is the accent. A page, a drive, a
     *       window is the icon face with one accent part — a lamp, a title bar,
     *       a rule. If everything were the accent colour nothing would be.</li>
     *   <li><b>Two stops, always the same two.</b> The launcher's own gradient:
     *       the accent, and the accent rotated a short way round the wheel. Down
     *       and to the right, so every object in a list appears lit from the same
     *       corner.</li>
     *   <li><b>One stroke weight</b>, and only on neutral objects. An accent
     *       object gets a darker edge of itself instead, because a grey outline
     *       around a saturated shape is what makes clip art look like clip art.</li>
     *   <li><b>Detail appears with the pixels to hold it.</b> Below 24px there
     *       are no ruled lines, no chips, no ribs — at that size they are not
     *       simplification, they are noise that reads as dirt.</li>
     * </ul>
     */
    private static Bitmap render(Art art, int size, WineVisualStyle.Style s, int variant) {
        Bitmap fromAsset = renderAsset(art, size, s);
        if (fromAsset != null) return fromAsset;

        Bitmap bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float stroke = Math.max(1f, size * 0.055f);
        boolean detailed = size >= DETAIL_SIZE;

        switch (art) {
            case FOLDER:
                drawFolder(canvas, paint, size, s, false);
                break;

            case FOLDER_OPEN:
                drawFolder(canvas, paint, size, s, true);
                break;

            case DOCUMENT:
                drawDocument(canvas, paint, size, s, s.iconFace(), s.outline);
                break;

            case DRIVE:
            case DRIVE_OFFLINE: {
                /*
                 * A drive as a stack, not a box.
                 *
                 * The shipped one is a beige slab in perspective and reads as a
                 * drive only because everyone learned it in 1995. Two plates
                 * with a lamp says storage without the perspective, and survives
                 * being 16 pixels wide, which the slab does not.
                 */
                float radius = size * 0.09f;
                RectF lower = new RectF(size * 0.10f, size * 0.50f, size * 0.90f, size * 0.78f);
                RectF upper = new RectF(size * 0.10f, size * 0.24f, size * 0.90f, size * 0.52f);

                for (RectF plate : new RectF[]{lower, upper}) {
                    paint.setShader(null);
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(plate == upper ? s.iconFace() : shade(s.iconFace(), 0.82f));
                    canvas.drawRoundRect(plate, radius, radius, paint);
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setStrokeWidth(stroke);
                    paint.setColor(s.outline);
                    canvas.drawRoundRect(plate, radius, radius, paint);
                }

                // The lamp, which is the only part that is the accent, and the
                // only part that changes when the drive is gone.
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(art == Art.DRIVE_OFFLINE ? s.closeHot : s.primary);
                canvas.drawCircle(size * 0.76f, size * 0.38f, Math.max(1.2f, size * 0.052f), paint);

                if (detailed) {
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setStrokeWidth(Math.max(1f, size * 0.035f));
                    paint.setStrokeCap(Paint.Cap.ROUND);
                    paint.setColor(s.onVariant);
                    canvas.drawLine(size * 0.22f, size * 0.38f, size * 0.52f, size * 0.38f, paint);
                    canvas.drawLine(size * 0.22f, size * 0.64f, size * 0.44f, size * 0.64f, paint);
                }
                break;
            }

            case OPTICAL: {
                float centre = size / 2f;
                // A unit larger than the square shapes: a circle at identical
                // bounds always reads smaller than a rectangle beside it.
                float outer = size * 0.42f;

                paint.setStyle(Paint.Style.FILL);
                paint.setShader(accent(size, s));
                canvas.drawCircle(centre, centre, outer, paint);
                paint.setShader(null);

                // A sheen: one quadrant lifted, which is how a disc has been
                // drawn since discs existed and is instantly legible.
                paint.setColor(withAlpha(0xFFFFFFFF, 0.16f));
                canvas.drawArc(
                        new RectF(centre - outer, centre - outer, centre + outer, centre + outer),
                        200, 80, true, paint);

                paint.setColor(s.iconFace());
                canvas.drawCircle(centre, centre, size * 0.17f, paint);
                paint.setColor(s.background);
                canvas.drawCircle(centre, centre, size * 0.065f, paint);

                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(stroke * 0.8f);
                paint.setColor(shade(s.primary, 0.62f));
                canvas.drawCircle(centre, centre, outer, paint);
                break;
            }

            case MEMORY: {
                RectF board = new RectF(size * 0.08f, size * 0.30f, size * 0.92f, size * 0.66f);
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(s.iconFace());
                canvas.drawRoundRect(board, size * 0.05f, size * 0.05f, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(stroke);
                paint.setColor(s.outline);
                canvas.drawRoundRect(board, size * 0.05f, size * 0.05f, paint);

                // Chips in the accent, contacts below in the outline: a memory
                // module is two rows and a notch, and nothing else about it
                // survives to 16 pixels anyway.
                paint.setStyle(Paint.Style.FILL);
                paint.setShader(accent(size, s));
                int chips = detailed ? 3 : 2;
                for (int i = 0; i < chips; i++) {
                    float w = 0.76f / chips;
                    float x = size * (0.12f + i * w);
                    canvas.drawRect(x, size * 0.38f, x + size * (w - 0.06f), size * 0.56f, paint);
                }
                paint.setShader(null);
                paint.setColor(s.outline);
                for (int i = 0; i < 4; i++) {
                    float x = size * (0.20f + i * 0.19f);
                    canvas.drawRect(x, size * 0.66f, x + size * 0.09f, size * 0.74f, paint);
                }
                break;
            }

            case COMPUTER: {
                RectF bezel = new RectF(size * 0.08f, size * 0.16f, size * 0.92f, size * 0.66f);
                float radius = size * 0.07f;

                paint.setStyle(Paint.Style.FILL);
                paint.setColor(s.iconFace());
                canvas.drawRoundRect(bezel, radius, radius, paint);

                // The screen is the accent, inset far enough that the bezel
                // survives at 16px — a screen that meets the frame reads as a
                // coloured rectangle rather than a monitor.
                RectF screen = new RectF(
                        bezel.left + size * 0.07f, bezel.top + size * 0.07f,
                        bezel.right - size * 0.07f, bezel.bottom - size * 0.09f);
                paint.setShader(accent(size, s));
                canvas.drawRoundRect(screen, radius * 0.5f, radius * 0.5f, paint);
                paint.setShader(null);

                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(stroke);
                paint.setColor(s.outline);
                canvas.drawRoundRect(bezel, radius, radius, paint);

                // Neck and foot, drawn as one tapered shape so the join does not
                // have to be antialiased into existence at small sizes.
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(s.outline);
                canvas.drawRect(size * 0.42f, size * 0.66f, size * 0.58f, size * 0.78f, paint);
                canvas.drawRoundRect(
                        new RectF(size * 0.26f, size * 0.78f, size * 0.74f, size * 0.86f),
                        size * 0.04f, size * 0.04f, paint);
                break;
            }

            case SEARCH: {
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(Math.max(1.6f, size * 0.095f));
                paint.setStrokeCap(Paint.Cap.ROUND);
                paint.setShader(accent(size, s));
                canvas.drawCircle(size * 0.42f, size * 0.42f, size * 0.24f, paint);
                paint.setShader(null);
                paint.setColor(s.primary);
                canvas.drawLine(size * 0.60f, size * 0.60f, size * 0.82f, size * 0.82f, paint);
                break;
            }

            case RECYCLE: {
                // A tapered bin with a lid that sits above it. The taper is what
                // distinguishes a bin from a cup at this size.
                Path body = new Path();
                body.moveTo(size * 0.24f, size * 0.34f);
                body.lineTo(size * 0.76f, size * 0.34f);
                body.lineTo(size * 0.68f, size * 0.86f);
                body.lineTo(size * 0.32f, size * 0.86f);
                body.close();

                paint.setStyle(Paint.Style.FILL);
                paint.setColor(s.iconFace());
                canvas.drawPath(body, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(stroke);
                paint.setColor(s.outline);
                canvas.drawPath(body, paint);

                if (detailed) {
                    paint.setStrokeWidth(Math.max(1f, size * 0.035f));
                    paint.setColor(s.onVariant);
                    for (int i = 0; i < 3; i++) {
                        float x = size * (0.40f + i * 0.10f);
                        canvas.drawLine(x, size * 0.44f, x - size * 0.015f, size * 0.76f, paint);
                    }
                }

                paint.setStyle(Paint.Style.FILL);
                paint.setShader(accent(size, s));
                canvas.drawRoundRect(
                        new RectF(size * 0.16f, size * 0.24f, size * 0.84f, size * 0.33f),
                        size * 0.045f, size * 0.045f, paint);
                canvas.drawRoundRect(
                        new RectF(size * 0.40f, size * 0.14f, size * 0.60f, size * 0.23f),
                        size * 0.03f, size * 0.03f, paint);
                paint.setShader(null);
                break;
            }

            case WINDOW:
            case APP: {
                RectF frame = new RectF(size * 0.08f, size * 0.14f, size * 0.92f, size * 0.86f);
                float radius = size * 0.09f;

                paint.setStyle(Paint.Style.FILL);
                paint.setColor(s.iconFace());
                canvas.drawRoundRect(frame, radius, radius, paint);

                /*
                 * The title bar is the accent and is clipped to the frame, so
                 * its top corners are the frame's corners and its bottom ones
                 * are square. Drawing a rounded bar inside a rounded frame
                 * leaves a hairline of face colour along the top that looks like
                 * a rendering fault at every size.
                 */
                canvas.save();
                canvas.clipRect(frame.left, frame.top, frame.right, frame.top + size * 0.22f);
                paint.setShader(accent(size, s));
                canvas.drawRoundRect(frame, radius, radius, paint);
                paint.setShader(null);
                canvas.restore();

                if (detailed) {
                    paint.setColor(s.onVariant);
                    canvas.drawRoundRect(
                            new RectF(size * 0.18f, size * 0.46f, size * 0.62f, size * 0.54f),
                            size * 0.03f, size * 0.03f, paint);
                    canvas.drawRoundRect(
                            new RectF(size * 0.18f, size * 0.62f, size * 0.78f, size * 0.70f),
                            size * 0.03f, size * 0.03f, paint);
                } else {
                    paint.setColor(s.onVariant);
                    canvas.drawRect(size * 0.20f, size * 0.52f, size * 0.66f, size * 0.62f, paint);
                }

                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(stroke);
                paint.setColor(s.outline);
                canvas.drawRoundRect(frame, radius, radius, paint);
                break;
            }

            case SPINNER: {
                /*
                 * Twelve spokes with a lit head, which is the only throbber that
                 * reads at 16 pixels — a rotating arc turns to a grey ring the
                 * moment it is antialiased that small.
                 */
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(size * 0.10f);
                paint.setStrokeCap(Paint.Cap.ROUND);
                for (int i = 0; i < 12; i++) {
                    int position = (i - variant + 12) % 12;
                    float alpha = 0.16f + 0.84f * (position / 11f);
                    paint.setColor(withAlpha(s.primary, alpha));
                    double angle = Math.toRadians(i * 30 - 90);
                    float centre = size / 2f;
                    canvas.drawLine(
                            centre + (float) Math.cos(angle) * size * 0.22f,
                            centre + (float) Math.sin(angle) * size * 0.22f,
                            centre + (float) Math.cos(angle) * size * 0.40f,
                            centre + (float) Math.sin(angle) * size * 0.40f,
                            paint);
                }
                break;
            }

            default:
                drawToolbarGlyph(canvas, paint, size, s, art);
                break;
        }
        paint.setShader(null);
        return bitmap;
    }

    /**
     * A folder: a back plate with a tab, and a front panel over it.
     *
     * Two pieces rather than one outline, because the tab is the whole of what
     * makes a folder recognisable and an outlined single shape loses it the
     * moment the stroke is a pixel wide. The front is the gradient and the back
     * is a darker cut of the same accent, so the two read as one object lit from
     * above rather than as two colours.
     */
    private static void drawFolder(
            Canvas canvas, Paint paint, int size, WineVisualStyle.Style s, boolean open) {
        float radius = size * 0.075f;

        paint.setStyle(Paint.Style.FILL);
        paint.setShader(null);
        paint.setColor(shade(s.primary, 0.66f));

        Path back = new Path();
        back.moveTo(size * 0.07f, size * 0.34f);
        back.lineTo(size * 0.38f, size * 0.34f);
        back.lineTo(size * 0.46f, size * 0.42f);
        back.lineTo(size * 0.93f, size * 0.42f);
        back.lineTo(size * 0.93f, size * 0.80f);
        back.lineTo(size * 0.07f, size * 0.80f);
        back.close();
        canvas.drawPath(back, paint);

        /*
         * Open is the same folder with the front panel dropped and splayed.
         *
         * Wine's open folder is a different drawing in perspective; keeping the
         * silhouette and moving one edge means the two states read as the same
         * folder in two conditions, which is the only reason to have both.
         */
        paint.setShader(accent(size, s));
        if (open) {
            Path front = new Path();
            front.moveTo(size * 0.16f, size * 0.48f);
            front.lineTo(size * 0.98f, size * 0.48f);
            front.lineTo(size * 0.88f, size * 0.84f);
            front.lineTo(size * 0.06f, size * 0.84f);
            front.close();
            canvas.drawPath(front, paint);
        } else {
            canvas.drawRoundRect(
                    new RectF(size * 0.07f, size * 0.44f, size * 0.93f, size * 0.84f),
                    radius, radius, paint);
        }
        paint.setShader(null);

        // A highlight along the top of the front panel: one line, at a tenth
        // strength, which is what keeps a flat shape from looking printed.
        if (size >= DETAIL_SIZE) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(1f, size * 0.03f));
            paint.setColor(withAlpha(0xFFFFFFFF, 0.20f));
            canvas.drawLine(
                    size * (open ? 0.20f : 0.12f), size * 0.475f,
                    size * (open ? 0.94f : 0.88f), size * 0.475f, paint);
        }
    }

    /**
     * A page with a turned corner.
     *
     * The corner is drawn as a filled triangle in a darker cut of the face
     * rather than as two strokes: at 16px a stroked fold is three grey pixels in
     * the corner and reads as damage.
     */
    private static void drawDocument(
            Canvas canvas, Paint paint, int size, WineVisualStyle.Style s, int fill, int edge) {
        float fold = size * 0.26f;
        float stroke = Math.max(1f, size * 0.05f);
        float left = size * 0.16f, right = size * 0.84f;
        float top = size * 0.09f, bottom = size * 0.91f;
        float corner = right - fold;

        Path page = new Path();
        page.moveTo(left, top);
        page.lineTo(corner, top);
        page.lineTo(right, top + fold);
        page.lineTo(right, bottom);
        page.lineTo(left, bottom);
        page.close();

        paint.setShader(null);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(fill);
        canvas.drawPath(page, paint);

        Path turned = new Path();
        turned.moveTo(corner, top);
        turned.lineTo(corner, top + fold);
        turned.lineTo(right, top + fold);
        turned.close();
        paint.setColor(shade(fill, 0.72f));
        canvas.drawPath(turned, paint);

        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(stroke);
        paint.setColor(edge);
        canvas.drawPath(page, paint);

        // Ruled lines, the top one in the accent. One coloured rule is what
        // distinguishes this page from every other page in the set.
        if (size >= DETAIL_SIZE) {
            paint.setStrokeWidth(Math.max(1f, size * 0.045f));
            paint.setStrokeCap(Paint.Cap.ROUND);
            for (int i = 0; i < 3; i++) {
                float y = size * (0.52f + i * 0.13f);
                paint.setColor(i == 0 ? s.primary : s.onVariant);
                canvas.drawLine(size * 0.28f, y, size * (i == 2 ? 0.60f : 0.72f), y, paint);
            }
        }
    }

    /** The document used for a file type: a page with a coloured tag across it. */
    private static Bitmap drawTaggedDocument(
            int size, String label, int tag, WineVisualStyle.Style s) {
        Bitmap bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

        drawDocument(canvas, paint, size, s, s.iconFace(), s.outline);

        // The tag sits over the lower edge and past the left margin, so it is a
        // label attached to the page rather than a box drawn on it.
        RectF band = new RectF(size * 0.06f, size * 0.56f, size * 0.74f, size * 0.80f);
        paint.setShader(null);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(tag);
        canvas.drawRoundRect(band, size * 0.055f, size * 0.055f, paint);

        // The extension only where it can be read. Three characters across two
        // thirds of a 16px icon is four pixels a letter, which is a smudge.
        if (size >= DETAIL_SIZE) {
            paint.setColor(bestTextOn(tag));
            paint.setTextSize(size * 0.20f);
            paint.setFakeBoldText(true);
            paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(label, band.centerX(), band.centerY() + size * 0.072f, paint);
        }
        return bitmap;
    }

    /** The accent plus that marks a "new" button, in the free corner. */
    private static void drawPlus(
            Canvas canvas, Paint paint, int size, WineVisualStyle.Style s) {
        float cx = size * 0.72f, cy = size * 0.74f, arm = size * 0.15f;
        paint.setColor(s.primary);
        paint.setStrokeWidth(Math.max(1.5f, size * 0.11f));
        canvas.drawLine(cx - arm, cy, cx + arm, cy, paint);
        canvas.drawLine(cx, cy - arm, cx, cy + arm, paint);
    }

    /**
     * The launcher's gradient, across the icon.
     *
     * Two stops from the accent to the accent rotated a short way round the
     * wheel — the same derivation the generated wallpaper uses, so a folder and
     * the desktop behind it are lit by the same pair of colours. Down and to the
     * right on every object, so a list of them appears lit from one corner.
     */
    private static Shader accent(int size, WineVisualStyle.Style s) {
        return new LinearGradient(
                0, 0, size, size,
                s.primary, rotateHue(s.primary, SECONDARY_HUE_SHIFT),
                Shader.TileMode.CLAMP);
    }

    /** The second stop, a short way around the wheel from the first. */
    private static final float SECONDARY_HUE_SHIFT = -38f;

    /** Below this, detail is noise rather than simplification. */
    private static final int DETAIL_SIZE = 24;

    private static int rotateHue(int color, float degrees) {
        float[] hsv = new float[3];
        Color.colorToHSV(color, hsv);
        hsv[0] = (hsv[0] + degrees + 360f) % 360f;
        return Color.HSVToColor(hsv);
    }
    /**
     * The toolbar set, as line art.
     *
     * Strokes rather than filled shapes, and all on one weight, because these
     * sit in a row at 16 pixels: at that size the difference between two icons
     * has to be their outline, and anything with an interior turns to mud.
     */
    private static void drawToolbarGlyph(
            Canvas canvas, Paint paint, int size, WineVisualStyle.Style s, Art art) {
        float stroke = Math.max(1.3f, size * 0.10f);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(stroke);
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setColor(art == Art.DELETE ? s.closeHot : s.onSurface);

        float c = size / 2f;
        Path path = new Path();

        switch (art) {
            case ARROW_RIGHT:
                canvas.drawLine(size * 0.20f, c, size * 0.76f, c, paint);
                path.moveTo(size * 0.56f, size * 0.28f);
                path.lineTo(size * 0.80f, c);
                path.lineTo(size * 0.56f, size * 0.72f);
                canvas.drawPath(path, paint);
                break;

            case ARROW_UP:
                canvas.drawLine(c, size * 0.78f, c, size * 0.22f, paint);
                path.moveTo(size * 0.26f, size * 0.46f);
                path.lineTo(c, size * 0.20f);
                path.lineTo(size * 0.74f, size * 0.46f);
                canvas.drawPath(path, paint);
                break;

            case CHEVRON_RIGHT:
                path.moveTo(size * 0.38f, size * 0.22f);
                path.lineTo(size * 0.66f, c);
                path.lineTo(size * 0.38f, size * 0.78f);
                canvas.drawPath(path, paint);
                break;

            case REFRESH: {
                paint.setColor(s.primary);
                RectF arc = new RectF(size * 0.20f, size * 0.20f, size * 0.80f, size * 0.80f);
                canvas.drawArc(arc, -50, 285, false, paint);
                paint.setStyle(Paint.Style.FILL);
                path.moveTo(size * 0.78f, size * 0.10f);
                path.lineTo(size * 0.92f, size * 0.40f);
                path.lineTo(size * 0.58f, size * 0.36f);
                path.close();
                canvas.drawPath(path, paint);
                break;
            }

            case ZOOM_IN:
            case ZOOM_OUT: {
                paint.setColor(s.primary);
                canvas.drawCircle(size * 0.44f, size * 0.44f, size * 0.24f, paint);
                canvas.drawLine(size * 0.62f, size * 0.62f, size * 0.82f, size * 0.82f, paint);
                paint.setColor(s.onSurface);
                canvas.drawLine(size * 0.32f, size * 0.44f, size * 0.56f, size * 0.44f, paint);
                if (art == Art.ZOOM_IN) {
                    canvas.drawLine(size * 0.44f, size * 0.32f, size * 0.44f, size * 0.56f, paint);
                }
                break;
            }

            case COPY: {
                canvas.drawRoundRect(
                        new RectF(size * 0.16f, size * 0.16f, size * 0.62f, size * 0.62f),
                        size * 0.08f, size * 0.08f, paint);
                paint.setColor(s.primary);
                canvas.drawRoundRect(
                        new RectF(size * 0.38f, size * 0.38f, size * 0.84f, size * 0.84f),
                        size * 0.08f, size * 0.08f, paint);
                break;
            }

            case CUT: {
                canvas.drawLine(size * 0.24f, size * 0.18f, size * 0.66f, size * 0.68f, paint);
                canvas.drawLine(size * 0.76f, size * 0.18f, size * 0.34f, size * 0.68f, paint);
                paint.setColor(s.primary);
                canvas.drawCircle(size * 0.30f, size * 0.78f, size * 0.11f, paint);
                canvas.drawCircle(size * 0.70f, size * 0.78f, size * 0.11f, paint);
                break;
            }

            case PASTE: {
                canvas.drawRoundRect(
                        new RectF(size * 0.20f, size * 0.22f, size * 0.80f, size * 0.84f),
                        size * 0.08f, size * 0.08f, paint);
                paint.setColor(s.primary);
                paint.setStyle(Paint.Style.FILL);
                canvas.drawRoundRect(
                        new RectF(size * 0.36f, size * 0.10f, size * 0.64f, size * 0.28f),
                        size * 0.05f, size * 0.05f, paint);
                break;
            }

            case DELETE:
                canvas.drawLine(size * 0.28f, size * 0.28f, size * 0.72f, size * 0.72f, paint);
                canvas.drawLine(size * 0.72f, size * 0.28f, size * 0.28f, size * 0.72f, paint);
                break;

            /*
             * The two "new" buttons, as outlines rather than as objects.
             *
             * They live in a toolbar, next to copy and paste, and a filled
             * folder among ten line drawings reads as the one that is selected.
             * The plus is the accent and sits outside the shape's corner, which
             * is the only place it fits at sixteen pixels without covering the
             * thing it is adding to.
             */
            case NEW_FOLDER: {
                path.moveTo(size * 0.14f, size * 0.72f);
                path.lineTo(size * 0.14f, size * 0.28f);
                path.lineTo(size * 0.40f, size * 0.28f);
                path.lineTo(size * 0.48f, size * 0.40f);
                path.lineTo(size * 0.78f, size * 0.40f);
                path.lineTo(size * 0.78f, size * 0.60f);
                canvas.drawPath(path, paint);
                canvas.drawLine(size * 0.14f, size * 0.72f, size * 0.56f, size * 0.72f, paint);
                drawPlus(canvas, paint, size, s);
                break;
            }

            case NEW_FILE: {
                path.moveTo(size * 0.60f, size * 0.16f);
                path.lineTo(size * 0.26f, size * 0.16f);
                path.lineTo(size * 0.26f, size * 0.84f);
                path.lineTo(size * 0.56f, size * 0.84f);
                canvas.drawPath(path, paint);
                Path fold = new Path();
                fold.moveTo(size * 0.60f, size * 0.16f);
                fold.lineTo(size * 0.78f, size * 0.34f);
                fold.lineTo(size * 0.78f, size * 0.56f);
                canvas.drawPath(fold, paint);
                drawPlus(canvas, paint, size, s);
                break;
            }

            default:
                break;
        }
    }

    // ------------------------------------------------------------ icon encoding

    /**
     * An icon image as a PE resource holds it.
     *
     * A `BITMAPINFOHEADER` whose height is doubled — the format dates from when
     * every icon carried a separate one-bit mask below its pixels — then the
     * colour rows bottom-up, then that mask. Thirty-two-bit icons carry their
     * transparency in the alpha channel and the mask is left empty, but its
     * bytes are still counted, and getting that wrong is how an icon ends up
     * drawn half a row out of place.
     */
    private static byte[] iconDib(Bitmap bitmap) {
        int width = bitmap.getWidth();
        int height = bitmap.getHeight();
        int maskStride = ((width + 31) / 32) * 4;

        ByteBuffer out = ByteBuffer
                .allocate(40 + width * height * 4 + maskStride * height)
                .order(ByteOrder.LITTLE_ENDIAN);

        out.putInt(40);
        out.putInt(width);
        out.putInt(height * 2);
        out.putShort((short) 1);
        out.putShort((short) 32);
        out.putInt(0);
        out.putInt(width * height * 4);
        out.putInt(0).putInt(0).putInt(0).putInt(0);

        int[] pixels = new int[width * height];
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height);
        for (int y = height - 1; y >= 0; y--) {
            int row = y * width;
            for (int x = 0; x < width; x++) {
                int pixel = pixels[row + x];
                out.put((byte) Color.blue(pixel));
                out.put((byte) Color.green(pixel));
                out.put((byte) Color.red(pixel));
                out.put((byte) Color.alpha(pixel));
            }
        }
        // The mask stays zero: every pixel is "not masked", and the alpha
        // channel above decides what is actually seen.
        return out.array();
    }

    private static byte[] iconPng(Bitmap bitmap) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
        return out.toByteArray();
    }

    // --------------------------------------------------------------- PE surgery

    /**
     * The icons in a PE, addressed where they lie.
     *
     * Deliberately not a resource editor. It reads the tree to find where each
     * `RT_ICON` and `RT_GROUP_ICON` sits in the file and then writes over the
     * pixels in place; the tree, the section table and the file's length are
     * never touched. The only structural field it will change is the recorded
     * length of a resource, and only downwards.
     */
    private static final class PeIcons {
        private final byte[] image;
        /** Icon id to where its bytes start and how many it is allowed. */
        private final Map<Integer, int[]> icons = new HashMap<>();
        /** Group id to where its directory sits. */
        private final Map<Integer, int[]> groups = new HashMap<>();

        private PeIcons(byte[] image) {
            this.image = image;
        }

        static PeIcons parse(byte[] image) {
            try {
                ByteBuffer buffer = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN);
                int peOffset = buffer.getInt(0x3C);
                if (buffer.getInt(peOffset) != 0x00004550) return null;

                int coff = peOffset + 4;
                int sections = buffer.getShort(coff + 2) & 0xFFFF;
                int optionalSize = buffer.getShort(coff + 16) & 0xFFFF;
                int optional = coff + 20;
                boolean plus = (buffer.getShort(optional) & 0xFFFF) == 0x20B;

                int[][] table = new int[sections][4];
                int sectionTable = optional + optionalSize;
                for (int i = 0; i < sections; i++) {
                    int entry = sectionTable + i * 40;
                    table[i][0] = buffer.getInt(entry + 12);    // VirtualAddress
                    table[i][1] = buffer.getInt(entry + 8);     // VirtualSize
                    table[i][2] = buffer.getInt(entry + 20);    // PointerToRawData
                    table[i][3] = buffer.getInt(entry + 16);    // SizeOfRawData
                }

                int directories = optional + (plus ? 112 : 96);
                int resourceRva = buffer.getInt(directories + 16);
                if (resourceRva == 0) return null;

                PeIcons peIcons = new PeIcons(image);
                int base = toOffset(table, resourceRva);
                if (base < 0) return null;
                peIcons.walk(buffer, base, table);
                return peIcons;
            } catch (RuntimeException e) {
                Log.w(TAG, "Unreadable resource tree", e);
                return null;
            }
        }

        private static int toOffset(int[][] table, int rva) {
            for (int[] section : table) {
                int size = Math.max(section[1], section[3]);
                if (rva >= section[0] && rva < section[0] + size) {
                    return section[2] + (rva - section[0]);
                }
            }
            return -1;
        }

        /**
         * Type, then id, then language — the three levels a PE always has.
         *
         * Only {@code RT_ICON} and {@code RT_GROUP_ICON} are followed, and only
         * numbered ids: a named icon resource is legal but nothing here refers
         * to one, and treating a name as a number would index into the wrong
         * bytes rather than fail.
         */
        private void walk(ByteBuffer buffer, int base, int[][] table) {
            int types = entryCount(buffer, base);
            for (int t = 0; t < types; t++) {
                int typeEntry = base + 16 + t * 8;
                int typeName = buffer.getInt(typeEntry);
                int typeData = buffer.getInt(typeEntry + 4);
                if ((typeName & 0x80000000) != 0) continue;
                if ((typeData & 0x80000000) == 0) continue;
                if (typeName != 3 && typeName != 14) continue;

                int idDirectory = base + (typeData & 0x7FFFFFFF);
                int count = entryCount(buffer, idDirectory);

                for (int i = 0; i < count; i++) {
                    int entry = idDirectory + 16 + i * 8;
                    int name = buffer.getInt(entry);
                    int data = buffer.getInt(entry + 4);
                    if ((name & 0x80000000) != 0) continue;
                    if ((data & 0x80000000) == 0) continue;

                    int languages = base + (data & 0x7FFFFFFF);
                    if (entryCount(buffer, languages) < 1) continue;

                    int leaf = buffer.getInt(languages + 16 + 4);
                    if ((leaf & 0x80000000) != 0) continue;

                    int dataEntry = base + leaf;
                    int offset = toOffset(table, buffer.getInt(dataEntry));
                    int size = buffer.getInt(dataEntry + 4);
                    if (offset < 0 || size <= 0) continue;

                    Map<Integer, int[]> into = typeName == 3 ? icons : groups;
                    into.put(name, new int[]{offset, size, dataEntry});
                }
            }
        }

        private static int entryCount(ByteBuffer buffer, int directory) {
            return (buffer.getShort(directory + 12) & 0xFFFF)
                    + (buffer.getShort(directory + 14) & 0xFFFF);
        }

        /**
         * Repaints every 32-bit and PNG member of one icon group.
         *
         * The group directory is the only place that records what sizes an icon
         * comes in, so it is read rather than guessed — an icon set with a 20px
         * entry is not unusual and drawing it at 16 would put it on screen
         * stretched.
         */
        int repaintGroup(int groupId, Art art, WineVisualStyle.Style style) {
            int[] group = groups.get(groupId);
            if (group == null) return 0;

            ByteBuffer buffer = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN);
            int count = buffer.getShort(group[0] + 4) & 0xFFFF;
            int painted = 0;

            for (int i = 0; i < count; i++) {
                int entry = group[0] + 6 + i * 14;
                int bits = buffer.getShort(entry + 6) & 0xFFFF;
                int id = buffer.getShort(entry + 12) & 0xFFFF;
                if (bits != 32 && bits != 0) continue;
                painted += repaintIcon(id, art, style, 0);
            }
            return painted;
        }

        /** @return 1 if the icon was replaced. */
        int repaintIcon(int id, Art art, WineVisualStyle.Style style, int variant) {
            int[] icon = icons.get(id);
            if (icon == null) return 0;

            int offset = icon[0];
            int allowed = icon[1];
            ByteBuffer buffer = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN);

            byte[] replacement;
            boolean isPng = allowed > 8
                    && (image[offset] & 0xFF) == 0x89 && image[offset + 1] == 'P'
                    && image[offset + 2] == 'N' && image[offset + 3] == 'G';

            if (isPng) {
                // Size is whatever the PNG says; 256 is the only one in practice.
                Bitmap art256 = render(art, 256, style, variant);
                replacement = iconPng(art256);
                art256.recycle();
            } else {
                if (allowed < 40) return 0;
                int width = buffer.getInt(offset + 4);
                int height = buffer.getInt(offset + 8) / 2;
                int depth = buffer.getShort(offset + 14) & 0xFFFF;
                // Anything but 32-bit is a copy for a display nobody has, and
                // rewriting it would mean quantising to a palette for nothing.
                if (depth != 32 || width <= 0 || height <= 0 || width > 512) return 0;

                Bitmap drawn = render(art, Math.max(width, height), style, variant);
                Bitmap scaled = (drawn.getWidth() == width && drawn.getHeight() == height)
                        ? drawn
                        : Bitmap.createScaledBitmap(drawn, width, height, true);
                replacement = iconDib(scaled);
                if (scaled != drawn) scaled.recycle();
                drawn.recycle();
            }

            if (replacement.length > allowed) {
                Log.w(TAG, "Icon " + id + " would grow from " + allowed
                        + " to " + replacement.length + "; left alone");
                return 0;
            }

            System.arraycopy(replacement, 0, image, offset, replacement.length);
            // Only ever smaller, and only for the compressed entries: a DIB of
            // the same geometry is the same length to the byte.
            if (replacement.length != allowed) {
                buffer.putInt(icon[2] + 4, replacement.length);
            }
            return 1;
        }
    }

    // ------------------------------------------------------------------ helpers

    private static byte[] readAll(File file) throws IOException {
        byte[] data = new byte[(int) file.length()];
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            in.readFully(data);
        }
        return data;
    }

    /**
     * Written beside and moved into place.
     *
     * These are files Wine loads at start-up, and a container that catches one
     * of them half-written does not fail gracefully — it fails as a missing
     * `shell32`, which looks like anything but a theming bug.
     */
    private static void writeAtomically(File target, byte[] data) throws IOException {
        File temporary = new File(target.getPath() + ".loki-tmp");
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(data);
            out.getFD().sync();
        }
        if (!temporary.renameTo(target)) {
            // Rename across the same directory should never fail, but a failed
            // rename that left the original intact is better than a delete that
            // did not.
            FileUtils.copy(temporary, target);
            temporary.delete();
        }
    }

    private static int withAlpha(int color, float alpha) {
        return Color.argb(
                Math.round(255 * Math.max(0f, Math.min(1f, alpha))),
                Color.red(color), Color.green(color), Color.blue(color));
    }

    /** Toward black, for the shaded half of a folder. */
    private static int shade(int color, float amount) {
        return Color.rgb(
                Math.round(Color.red(color) * amount),
                Math.round(Color.green(color) * amount),
                Math.round(Color.blue(color) * amount));
    }

    /** Black or white, whichever can be read on the given fill. */
    private static int bestTextOn(int color) {
        float luminance = 0.299f * Color.red(color)
                + 0.587f * Color.green(color) + 0.114f * Color.blue(color);
        return luminance > 150f ? 0xFF101418 : 0xFFFFFFFF;
    }
}
