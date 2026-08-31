package com.wemu.engine.xserver.extensions;

import static com.wemu.engine.xserver.XClientRequestHandler.RESPONSE_CODE_SUCCESS;

import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xconnector.XStreamLock;
import com.wemu.engine.xserver.IDGenerator;
import com.wemu.engine.xserver.ScreenLayout;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.errors.BadImplementation;
import com.wemu.engine.xserver.errors.XRequestError;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * RandR, implemented for one purpose: telling the guest that the Thor has two
 * screens.
 *
 * <h2>Why the guest cannot see the second panel without this</h2>
 *
 * Wine learns its monitor list from exactly one place. {@code winex11.drv} calls
 * {@code XRRGetScreenResources} and walks the outputs and CRTCs it finds; with
 * no RandR extension at all it falls back to a single monitor the size of the
 * root window, and every Windows API that reports displays — {@code
 * EnumDisplayMonitors}, {@code EnumDisplayDevices}, and so everything above them
 * — reports one. A Unity game asking for {@code Display.displays[1]} is asking
 * that question, so no amount of work on the game's side can find a screen the
 * X server never mentioned.
 *
 * <h2>What it reports, and what it refuses</h2>
 *
 * One output and one CRTC per {@link ScreenLayout.Monitor}, each at that
 * monitor's position in the root window, each with a single mode at that
 * monitor's size. Enough for a guest to enumerate the panels and place a window
 * on either.
 *
 * Mode setting is refused. {@link ClientOpcodes#SET_CRTC_CONFIG} answers
 * {@code RRSetConfigFailed} and {@link ClientOpcodes#SET_SCREEN_CONFIG} the
 * same, because the layout here describes physical panels that the guest has no
 * business resizing — the root window is fixed at container start and a CRTC
 * that claimed to move would be describing a screen that does not exist. A
 * guest that asks is told no rather than told yes and quietly ignored, which is
 * the difference between a game falling back to a resolution that works and a
 * game drawing to one that is not there.
 *
 * <h2>Version</h2>
 *
 * 1.4 is advertised deliberately. Wine has three code paths keyed off this
 * number, and only the 1.4 one enumerates outputs individually; 1.0 hands back
 * a single screen with a size list, which is precisely the situation this
 * extension exists to escape. 1.4 obliges us to answer
 * {@link ClientOpcodes#GET_PROVIDERS}, which reports none — Wine treats a
 * driver with no providers as one unnamed GPU owning every output, which is
 * exactly what this is.
 */
public class RandRExtension implements Extension {
    public static final byte MAJOR_OPCODE = -105;

    private static final int VERSION_MAJOR = 1;
    private static final int VERSION_MINOR = 4;

    /** RandR's own reply status codes, used by the config-setting requests. */
    private static final byte SET_CONFIG_SUCCESS = 0;
    private static final byte SET_CONFIG_FAILED = 3;

    private static final byte CONNECTION_CONNECTED = 0;
    private static final byte SUBPIXEL_UNKNOWN = 0;
    private static final short ROTATE_0 = 1;

    /**
     * The gamma ramp size reported for every CRTC.
     *
     * Nothing here applies a ramp — the panels are driven by SurfaceFlinger and
     * the X server never sees their output — but a size of zero is not "no
     * gamma support", it is a malformed answer that makes libXrandr allocate a
     * zero-length ramp and some guests divide by it. 256 with an identity ramp
     * is the honest description of what happens to the colours: nothing.
     */
    private static final int GAMMA_SIZE = 256;

    private static abstract class ClientOpcodes {
        static final byte QUERY_VERSION = 0;
        static final byte SET_SCREEN_CONFIG = 2;
        static final byte SELECT_INPUT = 4;
        static final byte GET_SCREEN_INFO = 5;
        static final byte GET_SCREEN_SIZE_RANGE = 6;
        static final byte SET_SCREEN_SIZE = 7;
        static final byte GET_SCREEN_RESOURCES = 8;
        static final byte GET_OUTPUT_INFO = 9;
        static final byte LIST_OUTPUT_PROPERTIES = 10;
        static final byte GET_OUTPUT_PROPERTY = 15;
        static final byte GET_CRTC_INFO = 20;
        static final byte SET_CRTC_CONFIG = 21;
        static final byte GET_CRTC_GAMMA_SIZE = 22;
        static final byte GET_CRTC_GAMMA = 23;
        static final byte SET_CRTC_GAMMA = 24;
        static final byte GET_SCREEN_RESOURCES_CURRENT = 25;
        static final byte GET_PANNING = 28;
        static final byte SET_PANNING = 29;
        static final byte SET_OUTPUT_PRIMARY = 30;
        static final byte GET_OUTPUT_PRIMARY = 31;
        static final byte GET_PROVIDERS = 32;
    }

    /**
     * One monitor's worth of RandR objects.
     *
     * The three IDs are separate because RandR keeps them separate — a guest
     * reads the output to find the CRTC, and the CRTC to find the mode — and
     * they come from {@link IDGenerator} rather than a private counter so they
     * cannot collide with the root window or any drawable the server makes.
     */
    private static final class Panel {
        final ScreenLayout.Monitor monitor;
        final int outputId;
        final int crtcId;
        final int modeId;
        final byte[] outputName;
        final byte[] modeName;

        Panel(ScreenLayout.Monitor monitor) {
            this.monitor = monitor;
            this.outputId = IDGenerator.generate();
            this.crtcId = IDGenerator.generate();
            this.modeId = IDGenerator.generate();
            this.outputName = monitor.name.getBytes(StandardCharsets.ISO_8859_1);
            this.modeName = (monitor.width + "x" + monitor.height)
                    .getBytes(StandardCharsets.ISO_8859_1);
        }
    }

    private final List<Panel> panels = new ArrayList<>();
    private final ScreenLayout layout;

    /**
     * The layout's timestamp, fixed for the life of the container.
     *
     * RandR clients compare this against the {@code configTimestamp} they were
     * given and re-read everything when it moves. The layout here never
     * changes, so a fixed value is not a shortcut — it is the correct answer,
     * and a value derived from the clock on each reply would make every client
     * re-enumerate on every poll.
     */
    private final int timestamp;

    private final AtomicBoolean announced = new AtomicBoolean(false);

    public RandRExtension(ScreenLayout layout) {
        this.layout = layout;
        this.timestamp = (int)(System.currentTimeMillis() & 0x7fffffffL);
        for (ScreenLayout.Monitor monitor : layout.getMonitors()) panels.add(new Panel(monitor));
    }

    @Override public String getName() { return "RANDR"; }
    @Override public byte getMajorOpcode() { return MAJOR_OPCODE; }
    @Override public byte getFirstErrorId() { return 0; }

    /**
     * The first event code RandR owns, which is 64 rather than the 0 every
     * other extension here reports.
     *
     * The others get away with 0 because they send no events — Present's
     * notifications go out as {@code GenericEvent}, which needs no reserved
     * code. RandR is different not because this server sends its events, but
     * because Wine *registers handlers* for them the moment it finds the
     * extension: it installs entries at {@code first_event} and
     * {@code first_event + 1} in a table indexed by X event type. With a base
     * of 0 those land on types 0 and 1, which are error and reply, and the
     * handler table stops describing what it says it does.
     *
     * 64 is where the X protocol reserves extension events, and nothing else
     * in this server occupies it: core events end at 34 and Present's are
     * generic.
     */
    @Override public byte getFirstEventId() { return 64; }

    /** Bytes of padding needed to bring a run of data back onto a 4-byte boundary. */
    static int paddingFor(int bytes) {
        return (-bytes) & 3;
    }

    /**
     * The {@code length} field of a GetScreenResources reply, in 4-byte units.
     *
     * X counts this from the end of the first 32 bytes, and this reply's fixed
     * part is exactly 32 — so everything counted here is variable data: one
     * word per CRTC, one per output, eight per mode, and the padded block of
     * mode names.
     */
    static int screenResourcesReplyLength(int monitorCount, int nameBytes) {
        return monitorCount            // crtcs
                + monitorCount         // outputs
                + (monitorCount * 8)   // 32 bytes of mode info each
                + ((nameBytes + paddingFor(nameBytes)) / 4);
    }

    /**
     * The {@code length} field of a GetOutputInfo reply, in 4-byte units.
     *
     * The leading 1 is the part that is easy to lose: RandR's reply header here
     * is 36 bytes rather than the 32 every core reply uses, and the length
     * counts only what follows the first 32 — so four bytes of *header* are
     * part of the count before any variable data is added. Then one word for
     * the single CRTC, one for the single mode, none for clones, and the padded
     * output name.
     */
    static int outputInfoReplyLength(int nameLength) {
        return 1                       // the 36-byte header's tail
                + 1                    // one possible crtc
                + 1                    // one mode
                + ((nameLength + paddingFor(nameLength)) / 4);
    }

    private Panel panelByOutput(int outputId) {
        for (Panel panel : panels) if (panel.outputId == outputId) return panel;
        return null;
    }

    private Panel panelByCrtc(int crtcId) {
        for (Panel panel : panels) if (panel.crtcId == crtcId) return panel;
        return null;
    }

    private void queryVersion(XClient client, XInputStream in, XOutputStream out) throws IOException {
        int requestedMajor = in.readInt();
        int requestedMinor = in.readInt();

        /*
         * Logged once, because this single line answers the question the whole
         * feature turns on: whether the guest's X driver has libXrandr at all.
         * A container whose Wine build cannot load it never sends this request,
         * and the two panels then fail to appear for a reason that has nothing
         * to do with anything below.
         */
        if (announced.compareAndSet(false, true)) {
            ProcessHelper.emitDiagnostic("RandR: guest asked for "
                    + requestedMajor + "." + requestedMinor
                    + ", answering " + VERSION_MAJOR + "." + VERSION_MINOR
                    + " with " + panels.size() + " monitor(s): " + layout);
        }

        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeInt(VERSION_MAJOR);
            out.writeInt(VERSION_MINOR);
            out.writePad(16);
        }
    }

    /**
     * The one screen size on offer, which is the root window.
     *
     * Reported as both minimum and maximum on purpose. A guest reads this range
     * to decide what it may ask {@code SetScreenSize} for, and a range with
     * room in it invites requests this server would have to refuse; a range of
     * exactly one size states the constraint up front.
     */
    private void getScreenSizeRange(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.readInt();
        short width = (short)layout.getWidth();
        short height = (short)layout.getHeight();
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeShort(width);
            out.writeShort(height);
            out.writeShort(width);
            out.writeShort(height);
            out.writePad(16);
        }
    }

    /**
     * Every output, CRTC and mode in one reply, which is how a guest builds its
     * monitor list.
     *
     * The names of every mode are concatenated into a single block at the end
     * with one round of padding for the lot; each mode carries only its own
     * length, and readers walk the block in the order the modes were listed.
     */
    private void getScreenResources(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.readInt();

        int nameBytes = 0;
        for (Panel panel : panels) nameBytes += panel.modeName.length;
        int namePadding = paddingFor(nameBytes);

        int count = panels.size();
        int length = screenResourcesReplyLength(count, nameBytes);

        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(length);
            out.writeInt(timestamp);
            out.writeInt(timestamp);
            out.writeShort((short)count);
            out.writeShort((short)count);
            out.writeShort((short)count);
            out.writeShort((short)nameBytes);
            out.writePad(8);

            for (Panel panel : panels) out.writeInt(panel.crtcId);
            for (Panel panel : panels) out.writeInt(panel.outputId);
            for (Panel panel : panels) writeModeInfo(out, panel);
            for (Panel panel : panels) out.write(panel.modeName);
            if (namePadding > 0) out.writePad(namePadding);
        }
    }

    /**
     * A mode as RandR describes it: a resolution plus the timings a real
     * display controller would have been programmed with.
     *
     * The timings are fabricated, and have to be. There is no scanout here —
     * SurfaceFlinger owns the panels — so there is no blanking interval to
     * report, but a mode whose totals are smaller than its visible area is
     * rejected outright by some clients and a dot clock of zero makes anything
     * that computes a refresh rate from it divide by zero. The numbers below
     * are a plain 60Hz mode with conventional porches, which is what the
     * hardware is doing anyway.
     */
    private void writeModeInfo(XOutputStream out, Panel panel) {
        short width = (short)panel.monitor.width;
        short height = (short)panel.monitor.height;
        short hTotal = (short)(panel.monitor.width + 160);
        short vTotal = (short)(panel.monitor.height + 40);
        int dotClock = (panel.monitor.width + 160) * (panel.monitor.height + 40) * 60;

        out.writeInt(panel.modeId);
        out.writeShort(width);
        out.writeShort(height);
        out.writeInt(dotClock);
        out.writeShort((short)(panel.monitor.width + 48));
        out.writeShort((short)(panel.monitor.width + 80));
        out.writeShort(hTotal);
        out.writeShort((short)0);
        out.writeShort((short)(panel.monitor.height + 3));
        out.writeShort((short)(panel.monitor.height + 13));
        out.writeShort(vTotal);
        out.writeShort((short)panel.modeName.length);
        out.writeInt(0);
    }

    private void getOutputInfo(XClient client, XInputStream in, XOutputStream out) throws IOException {
        int outputId = in.readInt();
        in.readInt();

        Panel panel = panelByOutput(outputId);
        if (panel == null) {
            writeUnknownOutputInfo(client, out);
            return;
        }

        int namePadding = paddingFor(panel.outputName.length);
        int length = outputInfoReplyLength(panel.outputName.length);

        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte(SET_CONFIG_SUCCESS);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(length);
            out.writeInt(timestamp);
            out.writeInt(panel.crtcId);
            out.writeInt(panel.monitor.widthInMillimeters());
            out.writeInt(panel.monitor.heightInMillimeters());
            out.writeByte(CONNECTION_CONNECTED);
            out.writeByte(SUBPIXEL_UNKNOWN);
            out.writeShort((short)1);
            out.writeShort((short)1);
            out.writeShort((short)1);
            out.writeShort((short)0);
            out.writeShort((short)panel.outputName.length);

            out.writeInt(panel.crtcId);
            out.writeInt(panel.modeId);
            out.write(panel.outputName);
            if (namePadding > 0) out.writePad(namePadding);
        }
    }

    /**
     * The reply for an output this server never handed out.
     *
     * A protocol error would be the other option and is worse: libXrandr turns
     * one into a call to the client's error handler, and Wine's aborts the
     * process for unexpected errors. An empty, disconnected output says the
     * same thing in a form every client already handles.
     */
    private void writeUnknownOutputInfo(XClient client, XOutputStream out) throws IOException {
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte(SET_CONFIG_FAILED);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(1);
            out.writeInt(timestamp);
            out.writeInt(0);
            out.writeInt(0);
            out.writeInt(0);
            out.writeByte((byte)1);
            out.writeByte(SUBPIXEL_UNKNOWN);
            out.writeShort((short)0);
            out.writeShort((short)0);
            out.writeShort((short)0);
            out.writeShort((short)0);
            out.writeShort((short)0);
        }
    }

    private void getCrtcInfo(XClient client, XInputStream in, XOutputStream out) throws IOException {
        int crtcId = in.readInt();
        in.readInt();

        Panel panel = panelByCrtc(crtcId);
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte(panel != null ? SET_CONFIG_SUCCESS : SET_CONFIG_FAILED);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(panel != null ? 2 : 0);
            out.writeInt(timestamp);
            out.writeShort(panel != null ? (short)panel.monitor.x : 0);
            out.writeShort(panel != null ? (short)panel.monitor.y : 0);
            out.writeShort(panel != null ? (short)panel.monitor.width : 0);
            out.writeShort(panel != null ? (short)panel.monitor.height : 0);
            out.writeInt(panel != null ? panel.modeId : 0);
            out.writeShort(ROTATE_0);
            out.writeShort(ROTATE_0);
            out.writeShort(panel != null ? (short)1 : 0);
            out.writeShort(panel != null ? (short)1 : 0);

            if (panel != null) {
                out.writeInt(panel.outputId);
                out.writeInt(panel.outputId);
            }
        }
    }

    /**
     * Which output the guest should treat as its main screen.
     *
     * Always the first monitor in the layout, which for the Thor is the panel
     * the game is already on. Getting this wrong does not merely reorder a
     * list — Windows places its taskbar and every default-positioned window on
     * the primary monitor, so naming the bottom panel would start every game
     * on the small screen.
     */
    private void getOutputPrimary(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.readInt();
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeInt(panels.isEmpty() ? 0 : panels.get(0).outputId);
            out.writePad(20);
        }
    }

    private void getProviders(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.readInt();
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeInt(timestamp);
            out.writeShort((short)0);
            out.writeShort((short)0);
            out.writePad(16);
        }
    }

    /**
     * RandR 1.0's view of the world, kept because libXrandr uses it to answer
     * questions a 1.4 client still asks — the current refresh rate among them.
     *
     * One size, which is the whole root window. A guest on this path cannot see
     * the second panel, and that is the point of advertising 1.4.
     */
    private void getScreenInfo(XClient client, XInputStream in, XOutputStream out) throws IOException {
        int windowId = in.readInt();

        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)ROTATE_0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(3);
            out.writeInt(windowId);
            out.writeInt(timestamp);
            out.writeInt(timestamp);
            out.writeShort((short)1);
            out.writeShort((short)0);
            out.writeShort(ROTATE_0);
            out.writeShort((short)60);
            // Rate info is counted in CARD16s, and there are two: the count of
            // rates for the single size, and the one rate itself.
            out.writeShort((short)2);
            out.writeShort((short)0);

            out.writeShort((short)layout.getWidth());
            out.writeShort((short)layout.getHeight());
            out.writeShort((short)Math.max(1, layout.getWidth() / 10));
            out.writeShort((short)Math.max(1, layout.getHeight() / 10));

            out.writeShort((short)1);
            out.writeShort((short)60);
        }
    }

    private void listOutputProperties(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.readInt();
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeShort((short)0);
            out.writeShort((short)0);
            out.writePad(20);
        }
    }

    /**
     * Every output property is absent, reported as such rather than as an error.
     *
     * Wine reads {@code non-desktop} on each output to decide whether it is a
     * headset panel to be skipped. "No such property" is the answer that keeps
     * both panels in the list; a protocol error here would reach Wine's error
     * handler for a question it asks about every output on every enumeration.
     */
    private void getOutputProperty(XClient client, XInputStream in, XOutputStream out) throws IOException {
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeInt(0);
            out.writeInt(0);
            out.writeInt(0);
            out.writePad(12);
        }
    }

    private void getCrtcGammaSize(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.readInt();
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeShort((short)GAMMA_SIZE);
            out.writePad(22);
        }
    }

    private void getCrtcGamma(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.readInt();
        int rampBytes = GAMMA_SIZE * 2 * 3;
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(rampBytes / 4);
            out.writeShort((short)GAMMA_SIZE);
            out.writePad(22);

            // Identity, written three times: red, then green, then blue.
            for (int channel = 0; channel < 3; channel++) {
                for (int i = 0; i < GAMMA_SIZE; i++) {
                    out.writeShort((short)((i * 65535) / (GAMMA_SIZE - 1)));
                }
            }
        }
    }

    /** Panning is not supported; the reply describes a CRTC that cannot pan. */
    private void getPanning(XClient client, XInputStream in, XOutputStream out) throws IOException {
        int crtcId = in.readInt();
        Panel panel = panelByCrtc(crtcId);
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte(panel != null ? SET_CONFIG_SUCCESS : SET_CONFIG_FAILED);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(1);
            out.writeInt(timestamp);
            out.writePad(16);
            out.writePad(8);
        }
    }

    /**
     * Refuses a mode change and says the current configuration is unchanged.
     *
     * A guest that gets {@code RRSetConfigFailed} falls back to the mode it
     * already had, which is the truthful outcome: the panels are fixed, and the
     * container's resolution was decided before the guest started.
     */
    private void setCrtcConfig(XClient client, XInputStream in, XOutputStream out) throws IOException {
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte(SET_CONFIG_FAILED);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeInt(timestamp);
            out.writePad(20);
        }
    }

    private void setScreenConfig(XClient client, XInputStream in, XOutputStream out) throws IOException {
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte(SET_CONFIG_FAILED);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeInt(timestamp);
            out.writeInt(timestamp);
            out.writeInt(0);
            out.writeShort((short)0);
            out.writeShort((short)0);
            out.writePad(8);
        }
    }

    @Override
    public void handleRequest(XClient client, XInputStream in, XOutputStream out)
            throws IOException, XRequestError {
        int opcode = client.getRequestData();

        switch (opcode) {
            case ClientOpcodes.QUERY_VERSION:
                queryVersion(client, in, out);
                break;
            case ClientOpcodes.GET_SCREEN_SIZE_RANGE:
                getScreenSizeRange(client, in, out);
                break;
            case ClientOpcodes.GET_SCREEN_RESOURCES:
            case ClientOpcodes.GET_SCREEN_RESOURCES_CURRENT:
                getScreenResources(client, in, out);
                break;
            case ClientOpcodes.GET_OUTPUT_INFO:
                getOutputInfo(client, in, out);
                break;
            case ClientOpcodes.GET_CRTC_INFO:
                getCrtcInfo(client, in, out);
                break;
            case ClientOpcodes.GET_OUTPUT_PRIMARY:
                getOutputPrimary(client, in, out);
                break;
            case ClientOpcodes.GET_PROVIDERS:
                getProviders(client, in, out);
                break;
            case ClientOpcodes.GET_SCREEN_INFO:
                getScreenInfo(client, in, out);
                break;
            case ClientOpcodes.LIST_OUTPUT_PROPERTIES:
                listOutputProperties(client, in, out);
                break;
            case ClientOpcodes.GET_OUTPUT_PROPERTY:
                getOutputProperty(client, in, out);
                break;
            case ClientOpcodes.GET_CRTC_GAMMA_SIZE:
                getCrtcGammaSize(client, in, out);
                break;
            case ClientOpcodes.GET_CRTC_GAMMA:
                getCrtcGamma(client, in, out);
                break;
            case ClientOpcodes.GET_PANNING:
                getPanning(client, in, out);
                break;
            case ClientOpcodes.SET_CRTC_CONFIG:
                setCrtcConfig(client, in, out);
                break;
            case ClientOpcodes.SET_SCREEN_CONFIG:
                setScreenConfig(client, in, out);
                break;

            /*
             * Accepted and ignored, all of which are requests to change
             * something this server does not have. They carry no reply, so a
             * client cannot tell the difference between ignored and applied —
             * except by reading the configuration back, which still describes
             * the panels as they are.
             */
            case ClientOpcodes.SELECT_INPUT:
            case ClientOpcodes.SET_SCREEN_SIZE:
            case ClientOpcodes.SET_CRTC_GAMMA:
            case ClientOpcodes.SET_PANNING:
            case ClientOpcodes.SET_OUTPUT_PRIMARY:
                break;

            default:
                throw new BadImplementation();
        }

        /*
         * Every handler above reads only the fields it needs, and several read
         * none at all. The connector does not rewind for the next request, so
         * anything left of this one has to go or the following request is
         * parsed from the middle of this one.
         */
        client.skipRequest();
    }
}
