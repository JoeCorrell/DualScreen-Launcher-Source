package com.wemu.engine.winhandler;

import android.content.SharedPreferences;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import androidx.preference.PreferenceManager;
import com.wemu.engine.XServerHost;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.core.StardewCoreClrCompatibility;
import com.wemu.engine.core.StringUtils;
import com.wemu.engine.session.Fallout3Compatibility;
import com.wemu.engine.xenvironment.ImageFs;
import com.wemu.engine.inputcontrols.ControlsProfile;
import com.wemu.engine.widget.InputControlsView;
import com.wemu.engine.inputcontrols.ExternalController;
import com.wemu.engine.inputcontrols.FakeInputWriter;
import com.wemu.engine.inputcontrols.GamepadState;
import com.wemu.engine.xserver.XServer;

import android.content.Context;
import android.hardware.input.InputManager;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.VibrationEffect;
import android.os.Vibrator;

import java.io.File;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class WinHandler {

    private static final short SERVER_PORT = 7947;
    private static final short CLIENT_PORT = 7946;
    public static final byte FLAG_INPUT_TYPE_XINPUT = 0x04;
    public static final byte FLAG_INPUT_TYPE_DINPUT = 0x08;
    public static final byte DEFAULT_INPUT_TYPE = FLAG_INPUT_TYPE_XINPUT;

    private volatile DatagramSocket socket;
    /*
     * The pools this bridge runs on, kept so they can be ended.
     *
     * They were anonymous — `Executors.newSingleThreadExecutor().execute(...)`
     * with the pool discarded on the same line — which starts a non-daemon
     * thread that nothing can ever shut down. See `stop`.
     */
    private ExecutorService sendExecutor;
    private ExecutorService receiveExecutor;
    private ExecutorService vibrationExecutor;
    private final ByteBuffer sendData = ByteBuffer.allocate(64).order(ByteOrder.LITTLE_ENDIAN);
    private final ByteBuffer receiveData = ByteBuffer.allocate(64).order(ByteOrder.LITTLE_ENDIAN);
    private final DatagramPacket sendPacket = new DatagramPacket(sendData.array(), 64);
    private final DatagramPacket receivePacket = new DatagramPacket(receiveData.array(), 64);
    private final ArrayDeque<Runnable> actions = new ArrayDeque<>();

    private boolean initReceived = false;
    private volatile boolean running = false;
    private OnGetProcessInfoListener onGetProcessInfoListener;
    private InetAddress localhost;
    private byte inputType = DEFAULT_INPUT_TYPE;
    private final XServerHost host;
    private SharedPreferences preferences;

    private static final int MAX_CONTROLLERS = 4;
    private static final int OSC_DEVICE_ID = -1;
    private FakeInputWriter[] writers = new FakeInputWriter[MAX_CONTROLLERS];
    private Map<Integer, Integer> deviceToSlot = new HashMap<>();
    private Set<Integer> usedSlots = new HashSet<>();
    private final Object controllerLock = new Object();
    private String fakeInputBasePath;
    private volatile LocalServerSocket vibrationServer;
    private volatile LocalSocket vibrationClient;
    private volatile boolean vibrationRunning = false;
    private boolean[] vibrationEnabledSlots = new boolean[MAX_CONTROLLERS];

    private boolean xinputDisabled;
    private boolean xinputDisabledInitialized = false;

    private int fallbackSlot = -1;

    private final Map<Integer, ExternalController> controllers = new HashMap<>();
    private final AtomicInteger forwardedControllerEvents = new AtomicInteger();
    private final InputManager inputManager;
    private final InputManager.InputDeviceListener inputDeviceListener;

    public WinHandler(XServerHost host) {
        this.host = host;
        this.inputManager = (InputManager) host.getHostContext().getSystemService(Context.INPUT_SERVICE);
        this.inputDeviceListener = new InputManager.InputDeviceListener() {
            @Override
            public void onInputDeviceAdded(int deviceId) {
                /*
                 * Wine enumerates evdev devices at process startup.  Creating
                 * eventN only after the first button press is too late for
                 * games such as Skyrim, which cache an empty XInput device list
                 * for the entire session.  Hot-plugged controllers still need
                 * a writer immediately for games which do rescan.
                 */
                InputDevice device = InputDevice.getDevice(deviceId);
                if (fakeInputBasePath != null && ExternalController.isGameController(device)) {
                    assignSlot(deviceId);
                }
            }

            @Override
            public void onInputDeviceRemoved(int deviceId) {
                releaseSlot(deviceId);
            }

            @Override
            public void onInputDeviceChanged(int deviceId) {}
        };
        inputManager.registerInputDeviceListener(inputDeviceListener, null);
        preferences = PreferenceManager.getDefaultSharedPreferences(host.getHostContext());
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            vibrationEnabledSlots[i] = preferences.getBoolean("vibration_slot_" + i, true);
        }
    }

    private boolean sendPacket(int port) {
        try {
            int size = sendData.position();
            if (size == 0) return false;
            sendPacket.setAddress(localhost);
            sendPacket.setPort(port);
            socket.send(sendPacket);
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    public void exec(String command) {
        command = command.trim();
        if (command.isEmpty()) return;

        // The `split` function here should be sensitive to paths with spaces.
        // Instead of splitting, let's assume that command is directly provided in two
        // parts: filename and parameters.
        // Adjust command splitting based on whether it contains quotes.

        String filename;
        String parameters;

        /*
         * Only a leading quote belongs to the executable.
         *
         * The old rule was "if there is a quote anywhere, the filename runs
         * from the first to the last of them", which is right for a lone quoted
         * path and wrong for everything else. A quoted executable with a quoted
         * argument -- "C:\games\game.exe" -cfg "my settings.ini" -- put the
         * whole span into the filename, arguments and inner quotes included,
         * and the launch failed with a path no file has. An unquoted executable
         * with a quoted argument was worse: the first quote is in the argument,
         * so the argument became the program.
         */
        if (command.startsWith("\"")) {
            int closingQuote = command.indexOf('"', 1);
            if (closingQuote == -1) {
                Log.e("WinHandler", "Unterminated executable quote: " + command);
                return;
            }
            filename = command.substring(1, closingQuote);
            parameters = closingQuote + 1 < command.length()
                    ? command.substring(closingQuote + 1).trim()
                    : "";
        } else {
            // Quotes in the parameter half are left where they are; the guest
            // needs them to split its own arguments.
            String[] cmdList = command.split(" ", 2);
            filename = cmdList[0];
            if (cmdList.length > 1) {
                parameters = cmdList[1];
            } else {
                parameters = "";
            }
        }

        /*
         * A long command goes via a batch file, because the wire cannot take it.
         *
         * `sendPacket` is a fixed 64-byte datagram — it is constructed at that
         * length and never resized, so the guest reads 64 bytes whatever we put
         * in the buffer. The header costs 13, which leaves 51 bytes for a
         * filename and its arguments together. A Windows path is routinely
         * longer than that, and what used to happen then was a
         * `BufferOverflowException` on the send thread: an uncaught exception on
         * a pool thread, so a process death, so a game that closed itself.
         *
         * Widening the buffer is not available — the guest's `winhandler.exe`
         * reads a 64-byte packet and is not ours to change. Writing the real
         * command into a batch file and sending a short path to *that* keeps the
         * wire format exactly as it was.
         *
         * It also fixes something that was quietly wrong before: the batch file
         * sets the working directory to the executable's own folder, which is
         * what a game expects when it goes looking for its data.
         */
        /*
         * A Windows path is staged even when it would have fitted on the wire.
         *
         * The batch is the only thing that sets a working directory, and a game
         * started without one runs wherever the shell happened to be sitting.
         * Most find their data anyway because they ask Windows for their own
         * module path; the ones that do not simply fail to load, somewhere deep
         * enough that it reads as a broken install. Paying for a batch file on
         * every launch is the cheaper half of that trade.
         */
        boolean stardewCoreClr = StardewCoreClrCompatibility.isTargetExecutable(filename);
        int wireLength = 13 + filename.getBytes().length + parameters.getBytes().length;
        if (stardewCoreClr) {
            String viaScript = writeStardewLaunchScript(filename, parameters);
            if (viaScript == null) {
                Log.e("WinHandler", "Could not stage the hidden Stardew launcher; not starting it");
                return;
            }
            filename = "wscript.exe";
            parameters = "//B //NoLogo " + viaScript;
        } else if (wireLength > 64 || filename.contains("\\")) {
            String viaBatch = writeLaunchBatch(filename, parameters);
            if (viaBatch == null) {
                Log.e("WinHandler", "Could not stage a launcher for " + filename + "; not starting it");
                return;
            }
            filename = "cmd.exe";
            parameters = "/c " + viaBatch;
        }

        final String fFilename = filename;
        final String fParameters = parameters;
        addAction(() -> {
            byte[] filenameBytes = fFilename.getBytes();
            byte[] parametersBytes = fParameters.getBytes();

            sendData.rewind();
            sendData.put(RequestCodes.EXEC);
            sendData.putInt(filenameBytes.length + parametersBytes.length + 8);
            sendData.putInt(filenameBytes.length);
            sendData.putInt(parametersBytes.length);
            sendData.put(filenameBytes);
            sendData.put(parametersBytes);
            sendPacket(CLIENT_PORT);
        });
    }

    /** Stages a short WScript launcher so the diagnostic waiter has no window. */
    private String writeStardewLaunchScript(String filename, String parameters) {
        try {
            Context context = host != null ? host.getHostContext() : null;
            if (context == null) return null;

            ImageFs imageFs = ImageFs.find(context);
            File driveC = new File(imageFs.getRootDir(), ImageFs.WINEPREFIX + "/drive_c");
            if (!driveC.isDirectory()) return null;

            String contents = StardewCoreClrCompatibility.hiddenLauncherScriptContents(
                    filename, parameters);
            if (contents == null) return null;
            String name = "ws" + (batchCounter.getAndIncrement() & 7) + ".vbs";
            if (!FileUtils.writeString(new File(driveC, name), contents)) return null;
            ProcessHelper.emitDiagnostic(
                    "Stardew hidden launcher staged: W^X=0 fex-tso=1 "
                            + "smc=mtrack child-exit=captured corehost=verbose");
            return "C:\\" + name;
        } catch (Exception e) {
            Log.e("WinHandler", "Could not write the hidden Stardew launcher", e);
            return null;
        }
    }

    /**
     * Stages a command in a batch file the guest can reach by a short path.
     *
     * `drive_c` is an ordinary Android directory, so this is a plain file write
     * on the host — no guest round trip, and nothing to clean up beyond the one
     * file, which is overwritten by the next launch.
     *
     * @return the guest path to run, or null if it could not be written.
     */
    private String writeLaunchBatch(String filename, String parameters) {
        try {
            Context context = host != null ? host.getHostContext() : null;
            if (context == null) return null;

            ImageFs imageFs = ImageFs.find(context);
            File driveC = new File(imageFs.getRootDir(), ImageFs.WINEPREFIX + "/drive_c");
            if (!driveC.isDirectory()) return null;

            String directory = filename.contains("\\")
                    ? filename.substring(0, filename.lastIndexOf('\\'))
                    : "";

            StringBuilder batch = new StringBuilder("@echo off\r\n");
            Fallout3Compatibility.appendBatchEnvironment(batch, filename);
            // Quoted, and `/d` so it crosses drives — a game on F: started from
            // a shell sitting on C: would otherwise not move at all.
            if (!directory.isEmpty()) batch.append("cd /d \"").append(directory).append("\"\r\n");
            batch.append("start \"\" \"").append(filename).append('"');
            if (!parameters.isEmpty()) batch.append(' ').append(parameters);
            batch.append("\r\n");

            String name = nextBatchName();
            File file = new File(driveC, name);
            FileUtils.writeString(file, batch.toString());
            return "C:\\" + name;
        } catch (Exception e) {
            Log.e("WinHandler", "Could not write the launcher batch", e);
            return null;
        }
    }

    /**
     * Rotates the launcher's filename between launches.
     *
     * There was one fixed name, which was safe only because almost nothing used
     * it -- staging was reserved for commands too long for the wire. Now that
     * every Windows path is staged, two launches close together are ordinary:
     * a game's own updater, a launcher handing off to the game proper. Writing
     * the second batch over the first before the guest's cmd has read it makes
     * the first launch silently run the second program, and rerunning it works,
     * which is the worst way for a bug to present.
     *
     * A rotation rather than a unique name per launch, because these sit in
     * drive_c and nothing deletes them: eight is far more than the number that
     * can be in flight, and the set stays bounded.
     */
    private static String nextBatchName() {
        return "loki-run-" + (batchCounter.getAndIncrement() & 7) + ".bat";
    }

    private static final AtomicInteger batchCounter = new AtomicInteger();

    public void execWithDelay(String command, int delaySeconds) {
        if (command == null || command.trim().isEmpty() || delaySeconds < 0) return;
        Executors.newSingleThreadScheduledExecutor().schedule(() -> exec(command), delaySeconds, TimeUnit.SECONDS);
    }

    public void killProcess(final String processName) {
        addAction(() -> {
            sendData.rewind();
            sendData.put(RequestCodes.KILL_PROCESS);
            byte[] bytes = processName.getBytes();
            sendData.putInt(bytes.length);
            sendData.put(bytes);
            sendPacket(CLIENT_PORT);
        });
    }

    public void listProcesses() {
        addAction(() -> {
            sendData.rewind();
            sendData.put(RequestCodes.LIST_PROCESSES);
            sendData.putInt(0);

            if (!sendPacket(CLIENT_PORT) && onGetProcessInfoListener != null) {
                onGetProcessInfoListener.onGetProcessInfo(0, 0, null);
            }
        });
    }

    public void setProcessAffinity(final String processName, final int affinityMask) {
        addAction(() -> {
            byte[] bytes = processName.getBytes();
            sendData.rewind();
            sendData.put(RequestCodes.SET_PROCESS_AFFINITY);
            sendData.putInt(9 + bytes.length);
            sendData.putInt(0);
            sendData.putInt(affinityMask);
            sendData.put((byte) bytes.length);
            sendData.put(bytes);
            sendPacket(CLIENT_PORT);
        });
    }

    public void setProcessAffinity(final int pid, final int affinityMask) {
        addAction(() -> {
            sendData.rewind();
            sendData.put(RequestCodes.SET_PROCESS_AFFINITY);
            sendData.putInt(9);
            sendData.putInt(pid);
            sendData.putInt(affinityMask);
            sendData.put((byte) 0);
            sendPacket(CLIENT_PORT);
        });
    }

    public void mouseEvent(int flags, int dx, int dy, int wheelDelta) {
        if (!initReceived) return;
        addAction(() -> {
            sendData.rewind();
            sendData.put(RequestCodes.MOUSE_EVENT);
            sendData.putInt(10);
            sendData.putInt(flags);
            sendData.putShort((short) dx);
            sendData.putShort((short) dy);
            sendData.putShort((short) wheelDelta);
            sendData.put((byte) ((flags & MouseEventFlags.MOVE) != 0 ? 1 : 0)); // cursor pos feedback
            sendPacket(CLIENT_PORT);
        });
    }

    public void keyboardEvent(byte vkey, int flags) {
        if (!initReceived) return;
        addAction(() -> {
            sendData.rewind();
            sendData.put(RequestCodes.KEYBOARD_EVENT);
            sendData.put(vkey);
            sendData.putInt(flags);
            sendPacket(CLIENT_PORT);
        });
    }

    public void bringToFront(final String processName) {
        bringToFront(processName, 0);
    }

    public void bringToFront(final String processName, final long handle) {
        addAction(() -> {
            sendData.rewind();
            try {
                sendData.put(RequestCodes.BRING_TO_FRONT);
                byte[] bytes = processName.getBytes();
                sendData.putInt(bytes.length);
                // FIXME: Chinese and Japanese got from winhandler.exe are broken, and they
                // cause overflow.
                sendData.put(bytes);
                sendData.putLong(handle);
            } catch (java.nio.BufferOverflowException e) {
                e.printStackTrace();
                sendData.rewind();
            }
            sendPacket(CLIENT_PORT);
        });
    }

    private void addAction(Runnable action) {
        synchronized (actions) {
            actions.add(action);
            actions.notify();
        }
    }

    public OnGetProcessInfoListener getOnGetProcessInfoListener() {
        return onGetProcessInfoListener;
    }

    public void setOnGetProcessInfoListener(OnGetProcessInfoListener onGetProcessInfoListener) {
        synchronized (actions) {
            this.onGetProcessInfoListener = onGetProcessInfoListener;
        }
    }

    private void startSendThread() {
        sendExecutor = Executors.newSingleThreadExecutor();
        sendExecutor.execute(() -> {
            while (running) {
                synchronized (actions) {
                    /*
                     * One bad action must not take the session with it.
                     *
                     * These run on a pool thread, so anything thrown here is an
                     * uncaught exception on a thread with no handler — which is
                     * a process death, and from the user's side a game that
                     * closed itself. A malformed packet is worth a line in the
                     * log; it is not worth the running container.
                     */
                    while (initReceived && !actions.isEmpty()) {
                        try {
                            actions.poll().run();
                        } catch (Throwable t) {
                            Log.e("WinHandler", "Dropped a request to the guest", t);
                        }
                    }
                    /*
                     * Re-checked inside the lock, and this is not belt and
                     * braces.
                     *
                     * `stop` sets running false without holding this monitor and
                     * then notifies. A thread sitting between the `while
                     * (running)` test above and the wait below misses that
                     * notification entirely and parks with nothing left to wake
                     * it — one leaked thread per session, and the one that
                     * carries every request to the guest.
                     */
                    if (!running) break;
                    try {
                        actions.wait();
                    } catch (InterruptedException e) {}
                }
            }
        });
    }

    /**
     * Closes the bridge and everything it opened.
     *
     * A new {@code WinHandler} is built for every session, so anything this
     * does not release is held for the lifetime of the app by an object nobody
     * can reach any more. Three things were being held.
     *
     * <p>The vibration listener is the one that shows. Its socket lives in the
     * abstract namespace under a fixed name, and an abstract name is bound
     * process-wide — so the second session's listener could not bind, logged
     * one line about it, and rumble was dead for every game launched after the
     * first until the app was killed. The first session's accept loop, meanwhile,
     * was still running against a container that no longer existed.
     *
     * <p>The other two are the executors. Each {@code start} made single-thread
     * pools and none of them were ever shut down, so each session left three
     * live non-daemon threads behind — parked, cheap, and permanent.
     *
     * <p>Safe to call twice, and safe to call on a bridge that was never
     * started.
     */
    public void stop() {
        running = false;
        vibrationRunning = false;
        closeFakeInputWriter();

        LocalServerSocket bound = vibrationServer;
        if (bound != null) {
            vibrationServer = null;
            // Unblocks the accept loop, which sees vibrationRunning false and
            // ends without reporting the close as a failure.
            try { bound.close(); } catch (IOException ignored) {}
        }
        LocalSocket activeClient = vibrationClient;
        vibrationClient = null;
        if (activeClient != null) {
            try { activeClient.close(); } catch (IOException ignored) {}
        }

        if (socket != null) {
            socket.close();
            socket = null;
        }

        synchronized (actions) {
            actions.clear();
            // notifyAll, not notify: the send thread is not guaranteed to be the
            // only waiter, and waking the wrong one leaves it parked forever.
            actions.notifyAll();
        }

        shutdown(sendExecutor);
        shutdown(receiveExecutor);
        shutdown(vibrationExecutor);
        sendExecutor = receiveExecutor = vibrationExecutor = null;
    }

    /**
     * Ends a pool, interrupting whatever it is doing.
     *
     * `shutdownNow` rather than `shutdown` because each of these threads spends
     * its life blocked — on `accept`, on `receive`, on `wait` — and a plain
     * shutdown only stops new work being accepted. The blocking calls are all
     * already unblocked by the time this runs; the interrupt is what covers the
     * case where one of them had not reached its block yet.
     */
    private static void shutdown(ExecutorService executor) {
        if (executor != null) executor.shutdownNow();
    }

    public void startVibrationListener() {
        if (vibrationRunning) return;
        vibrationRunning = true;
        vibrationExecutor = Executors.newSingleThreadExecutor();
        vibrationExecutor.execute(() -> {
            try {
                LocalServerSocket server = new LocalServerSocket("wemu_vibration");
                vibrationServer = server;
                // The session can end between this thread being scheduled and
                // the socket being bound, in which case the name has just been
                // taken by a bridge nobody is using.
                if (!vibrationRunning) {
                    vibrationServer = null;
                    try { server.close(); } catch (IOException ignored) {}
                    return;
                }
                while (vibrationRunning) {
                    LocalSocket client = server.accept();
                    vibrationClient = client;
                    try {
                        java.io.InputStream is = client.getInputStream();
                        byte[] buf = new byte[8];
                        int read = 0;
                        while (read < buf.length && vibrationRunning) {
                            int count = is.read(buf, read, buf.length - read);
                            if (count < 0) break;
                            read += count;
                        }
                        if (read == buf.length) {
                            int strong = (buf[0] & 0xFF) | ((buf[1] & 0xFF) << 8);
                            int weak = (buf[2] & 0xFF) | ((buf[3] & 0xFF) << 8);
                            int durationMs = (buf[4] & 0xFF) | ((buf[5] & 0xFF) << 8);
                            int slot = (buf[6] & 0xFF) | ((buf[7] & 0xFF) << 8);
                            triggerVibration(strong, weak, durationMs, slot);
                        }
                    } catch (IOException e) {
                        if (vibrationRunning) {
                            Log.e("WinHandler", "Vibration client error: " + e.getMessage());
                        }
                    } finally {
                        if (vibrationClient == client) vibrationClient = null;
                        try { client.close(); } catch (IOException ignored) {}
                    }
                }
            } catch (IOException e) {
                if (vibrationRunning) {
                    Log.e("WinHandler", "Vibration listener error: " + e.getMessage());
                }
            } finally {
                // The name has to be free by the time the next session binds it,
                // whichever way this thread left.
                LocalServerSocket bound = vibrationServer;
                if (bound != null) {
                    vibrationServer = null;
                    try { bound.close(); } catch (IOException ignored) {}
                }
                LocalSocket active = vibrationClient;
                vibrationClient = null;
                if (active != null) {
                    try { active.close(); } catch (IOException ignored) {}
                }
                vibrationRunning = false;
            }
        });
    }

    private void triggerVibration(int strong, int weak, int durationMs, int slot) {
        if (!isValidSlot(slot)) return;

        boolean shouldCancel = (durationMs == 0 && strong == 0 && weak == 0);
        Vibrator vibrator = null;
        android.os.VibratorManager vibratorManager = null;
        boolean hasMultiMotor = false;

        Integer deviceId = null;
        synchronized (controllerLock) {
            if (!vibrationEnabledSlots[slot]) return;
            for (Map.Entry<Integer, Integer> entry : deviceToSlot.entrySet()) {
                if (entry.getValue() == slot) {
                    deviceId = entry.getKey();
                    break;
                }
            }
        }

        if (deviceId != null && deviceId.equals(OSC_DEVICE_ID)) {
            vibrator = (Vibrator) host.getHostContext().getSystemService(Context.VIBRATOR_SERVICE);
        } else if (deviceId != null) {
            android.view.InputDevice device = android.view.InputDevice.getDevice(deviceId);
            if (device != null) {
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                    vibratorManager = device.getVibratorManager();
                    if (vibratorManager != null && vibratorManager.getVibratorIds().length > 1) {
                        hasMultiMotor = true;
                    }
                }
                if (!hasMultiMotor) {
                    if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S && vibratorManager != null) {
                        int[] ids = vibratorManager.getVibratorIds();
                        if (ids.length > 0) vibrator = vibratorManager.getVibrator(ids[0]);
                    } else {
                        vibrator = device.getVibrator();
                    }
                    if (vibrator == null || !vibrator.hasVibrator()) {
                        synchronized (controllerLock) {
                            if (!deviceToSlot.containsKey(OSC_DEVICE_ID)
                                    && (fallbackSlot == -1 || fallbackSlot == slot)) {
                                vibrator = (Vibrator) host.getHostContext()
                                        .getSystemService(Context.VIBRATOR_SERVICE);
                                fallbackSlot = slot;
                            } else {
                                vibrator = null;
                            }
                        }
                    }
                }
            }
        }

        if (hasMultiMotor && vibratorManager != null && android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
            int[] vibratorIds = vibratorManager.getVibratorIds();
            if (vibratorIds.length >= 1) {
                Vibrator vStrong = vibratorManager.getVibrator(vibratorIds[0]);
                if (!shouldCancel && strong > 0) {
                    vStrong.vibrate(VibrationEffect.createOneShot(Math.max(1, durationMs), clampAmplitude(strong)));
                } else {
                    vStrong.cancel();
                }
            }
            if (vibratorIds.length >= 2) {
                Vibrator vWeak = vibratorManager.getVibrator(vibratorIds[1]);
                if (!shouldCancel && weak > 0) {
                    vWeak.vibrate(VibrationEffect.createOneShot(Math.max(1, durationMs), clampAmplitude(weak)));
                } else {
                    vWeak.cancel();
                }
            }
            return;
        }

        if (vibrator == null || !vibrator.hasVibrator()) return;

        if (!shouldCancel && (strong > 0 || weak > 0)) {
            int amplitude = clampAmplitude(Math.max(strong, weak));
            int duration = Math.max(1, durationMs);
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                vibrator.vibrate(VibrationEffect.createOneShot(duration, amplitude));
            } else {
                vibrator.vibrate(duration);
            }
        } else {
            vibrator.cancel();
        }
    }

    private int clampAmplitude(int value) {
        return Math.min(255, Math.max(1, (int) ((value / 65535.0f) * 255)));
    }

    private boolean isValidSlot(int slot) {
        return slot >= 0 && slot < MAX_CONTROLLERS;
    }

    public boolean isVibrationEnabledForSlot(int slot) {
        synchronized (controllerLock) {
            return isValidSlot(slot) && vibrationEnabledSlots[slot];
        }
    }

    public void setVibrationEnabledForSlot(int slot, boolean enabled) {
        if (isValidSlot(slot)) {
            synchronized (controllerLock) {
                vibrationEnabledSlots[slot] = enabled;
            }
            preferences.edit().putBoolean("vibration_slot_" + slot, enabled).apply();
        }
    }

    public int getMaxControllers() {
        return MAX_CONTROLLERS;
    }

    private void handleRequest(byte requestCode, final int port) {
        switch (requestCode) {
            case RequestCodes.INIT: {
                initReceived = true;

                preferences = PreferenceManager.getDefaultSharedPreferences(host.getHostContext());

                if (!xinputDisabledInitialized) {
                    xinputDisabled = preferences.getBoolean("xinput_toggle", false);
                }
                synchronized (actions) {
                    actions.notify();
                }
                break;
            }

            case RequestCodes.GET_PROCESS: {
                if (onGetProcessInfoListener == null) return;
                if (!hasReceiveBytes(57, requestCode)) return;
                receiveData.position(receiveData.position() + 4);
                int numProcesses = receiveData.getShort();
                int index = receiveData.getShort();
                int pid = receiveData.getInt();
                long memoryUsage = receiveData.getLong();
                int affinityMask = receiveData.getInt();
                boolean wow64Process = receiveData.get() == 1;

                byte[] bytes = new byte[32];
                receiveData.get(bytes);
                String name = StringUtils.fromANSIString(bytes);

                if (numProcesses > 0 && index >= 0 && index < numProcesses) {
                    onGetProcessInfoListener.onGetProcessInfo(index, numProcesses,
                            new ProcessInfo(pid, name, memoryUsage, affinityMask, wow64Process));
                } else {
                    Log.w("WinHandler", "Discarding invalid process packet index="
                            + index + " count=" + numProcesses);
                }
                break;
            }
            case RequestCodes.GET_GAMEPAD: {
                break;
            }
            case RequestCodes.GET_GAMEPAD_STATE: {
                break;
            }
            case RequestCodes.RELEASE_GAMEPAD: {
                // currentController = null; // No longer needed
                // Maybe clear all controllers or reset mapping?
                // For now, doing nothing is safest as mapping is sticky.
                break;
            }
            case RequestCodes.CURSOR_POS_FEEDBACK: {
                if (!hasReceiveBytes(4, requestCode)) return;
                short x = receiveData.getShort();
                short y = receiveData.getShort();
                XServer xServer = host.getXServer();
                xServer.pointer.setX(x);
                xServer.pointer.setY(y);
                host.getXServerView().requestRender();
                break;
            }
            default: {
                // Handle any other request codes if needed
                break;
            }
        }
    }

    private boolean hasReceiveBytes(int required, byte requestCode) {
        if (required >= 0 && receiveData.remaining() >= required) return true;
        Log.w("WinHandler", "Discarding short packet opcode=" + (requestCode & 0xff)
                + " remaining=" + receiveData.remaining() + " required=" + required);
        return false;
    }

    public void start() {
        try {
            localhost = InetAddress.getLocalHost();
        } catch (UnknownHostException e) {
            try {
                localhost = InetAddress.getByName("127.0.0.1");
            } catch (UnknownHostException ex) {}
        }
        running = true;
        startSendThread();
        receiveExecutor = Executors.newSingleThreadExecutor();
        receiveExecutor.execute(() -> {
            try {
                DatagramSocket socket = new DatagramSocket(null);
                this.socket = socket;
                socket.setReuseAddress(true);
                socket.bind(new InetSocketAddress((InetAddress) null, SERVER_PORT));
                // As with the vibration listener: a session that ended while
                // this thread was being scheduled has left a bound port behind
                // that nothing will ever close.
                if (!running) {
                    this.socket = null;
                    socket.close();
                    return;
                }
                while (running) {
                    // DatagramPacket retains the previous datagram's length as
                    // the next receive limit. Restore capacity or one short
                    // packet truncates every packet which follows it.
                    receivePacket.setLength(receiveData.capacity());
                    socket.receive(receivePacket);
                    synchronized (actions) {
                        int packetLength = receivePacket.getLength();
                        if (packetLength <= 0) continue;
                        receiveData.clear();
                        receiveData.limit(Math.min(packetLength, receiveData.capacity()));
                        byte requestCode = receiveData.get();
                        try {
                            handleRequest(requestCode, receivePacket.getPort());
                        } catch (RuntimeException malformedPacket) {
                            // A malformed guest datagram must not kill the only
                            // receive worker for cursor/process/controller data.
                            Log.w("WinHandler", "Discarding malformed packet opcode="
                                    + (requestCode & 0xff), malformedPacket);
                        }
                    }
                }
            } catch (IOException e) {}
        });
    }

    /**
     * The touch-control profile in force, or null when there is no overlay.
     *
     * A host is allowed to have no `InputControlsView` at all — this fork's
     * session puts its controls on the other panel, and a session driven by a
     * gamepad has no on-screen controls to speak of. Every read goes through
     * here so that case is handled once rather than at each of the three call
     * sites that used to dereference the view outright.
     */
    private ControlsProfile currentTouchProfile() {
        final InputControlsView view = host.getInputControlsView();
        return view == null ? null : view.getProfile();
    }

    /** Whether an on-screen control overlay is actually being shown. */
    private boolean isShowingTouchControls() {
        final InputControlsView view = host.getInputControlsView();
        return view != null && view.isShowTouchscreenControls();
    }

    public void sendGamepadState() {
        final ControlsProfile profile = currentTouchProfile();
        if (profile == null) {
            releaseSlot(OSC_DEVICE_ID);
            return;
        }
        final GamepadState gamepadState = profile.getGamepadState();
        final boolean useVirtualGamepad = profile.isVirtualGamepad() && isShowingTouchControls();
        if (useVirtualGamepad) {
            int slot = assignSlot(OSC_DEVICE_ID);
            synchronized (controllerLock) {
                if (slot >= 0 && writers[slot] != null) {
                    writers[slot].writeGamepadState(gamepadState);
                }
            }
        } else {
            releaseSlot(OSC_DEVICE_ID);
        }
    }

    public void sendGamepadState(ExternalController controller) {
        if (controller == null) return;
        ControlsProfile profile = currentTouchProfile();
        if (profile != null) {
            ExternalController profileController = profile.getController(controller.getDeviceId());
            if (profileController != null && profileController.getControllerBindingCount() > 0) {
                int slot = assignSlot(controller.getDeviceId());
                synchronized (controllerLock) {
                    if (slot >= 0 && writers[slot] != null) {
                        writers[slot].writeGamepadState(controller.remappedState);
                    }
                }
                return;
            }
        }
        int slot = assignSlot(controller.getDeviceId());
        synchronized (controllerLock) {
            if (slot >= 0 && writers[slot] != null) {
                writers[slot].writeGamepadState(controller.state);
            }
        }
    }

    private int assignSlot(int deviceId) {
        synchronized (controllerLock) {
            Integer existing = deviceToSlot.get(deviceId);
            if (existing != null) return existing;
            for (int slot = 0; slot < MAX_CONTROLLERS; slot++) {
                if (!usedSlots.contains(slot)) {
                    usedSlots.add(slot);
                    deviceToSlot.put(deviceId, slot);
                    if (fakeInputBasePath != null && writers[slot] == null) {
                        FakeInputWriter writer = new FakeInputWriter(fakeInputBasePath, slot);
                        if (!writer.open()) {
                            deviceToSlot.remove(deviceId);
                            usedSlots.remove(slot);
                            Log.e("WinHandler", "Could not create input writer for device "
                                    + deviceId + " in slot " + slot);
                            return -1;
                        }
                        writers[slot] = writer;
                        Log.d("WinHandler", "Assigned device " + deviceId + " to slot " + slot);
                    }
                    return slot;
                }
            }
            Log.w("WinHandler", "No slots available for device " + deviceId);
            return -1;
        }
    }

    private void releaseSlot(int deviceId) {
        synchronized (controllerLock) {
            Integer slot = deviceToSlot.remove(deviceId);
            if (slot != null) {
                if (fallbackSlot == slot) fallbackSlot = -1;
                if (writers[slot] != null) {
                    writers[slot].destroy();
                    writers[slot] = null;
                }
                usedSlots.remove(slot);
                controllers.remove(deviceId);
                Log.d("WinHandler", "Device " + deviceId + " released slot: " + slot);
            }
        }
    }

    public void setXInputDisabled(boolean disabled) {
        this.xinputDisabled = disabled;
        this.xinputDisabledInitialized = true;
    }

    public void setFakeInputPath(String fakeInputPath) {
        if (fakeInputPath != null && !fakeInputPath.isEmpty()) {
            this.fakeInputBasePath = fakeInputPath;
            Log.d("WinHandler", "FakeInputWriter base path set: " + fakeInputPath);
            clearStaleInputStreams();
            preAssignConnectedControllers();
            startVibrationListener();
        }
    }

    /** Removes regular-file event streams left behind by a killed app process. */
    private void clearStaleInputStreams() {
        synchronized (controllerLock) {
            for (int slot = 0; slot < MAX_CONTROLLERS; ++slot) {
                if (writers[slot] != null) continue;
                File stale = new File(fakeInputBasePath, "event" + slot);
                if (stale.exists() && !stale.delete()) {
                    Log.w("WinHandler", "Could not remove stale input stream " + stale);
                }
            }
        }
    }

    /**
     * Materialises every connected gamepad before Wine starts.
     *
     * WemuSession calls {@link #setFakeInputPath(String)} before it starts the
     * guest environment.  Keeping the scan here makes that ordering explicit:
     * event0..event3 exist when winebus/XInput enumerate them, rather than only
     * after Android delivers the player's first input event.
     */
    private void preAssignConnectedControllers() {
        int assigned = 0;
        for (int deviceId : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(deviceId);
            if (!ExternalController.isGameController(device)) continue;
            if (assignSlot(deviceId) >= 0) assigned++;
            if (assigned >= MAX_CONTROLLERS) break;
        }
        Log.d("WinHandler", "Pre-assigned " + assigned
                + " controller(s) before Wine startup");
    }

    public void closeFakeInputWriter() {
        if (inputManager != null && inputDeviceListener != null) {
            inputManager.unregisterInputDeviceListener(inputDeviceListener);
        }
        synchronized (controllerLock) {
            for (int i = 0; i < MAX_CONTROLLERS; i++) {
                if (writers[i] != null) {
                    writers[i].destroy();
                    writers[i] = null;
                }
            }
            deviceToSlot.clear();
            usedSlots.clear();
            controllers.clear();
            fallbackSlot = -1;
        }
        vibrationRunning = false;
        if (vibrationServer != null) {
            try {
                vibrationServer.close();
            } catch (IOException e) {}
            vibrationServer = null;
        }
    }

    private ExternalController getController(int deviceId) {
        synchronized (controllerLock) {
            if (controllers.containsKey(deviceId)) return controllers.get(deviceId);
            ExternalController controller = ExternalController.getController(deviceId);
            if (controller != null) controllers.put(deviceId, controller);
            return controller;
        }
    }

    public boolean onGenericMotionEvent(MotionEvent event) {
        ExternalController controller = getController(event.getDeviceId());
        if (controller != null) {
            boolean handled = controller.updateStateFromMotionEvent(event);
            if (handled) {
                sendGamepadState(controller);
                logFirstForwardedControllerEvent("axis", event.getDeviceId());
            }
            return handled;
        }
        return false;
    }

    public boolean onKeyEvent(KeyEvent event) {
        ExternalController controller = getController(event.getDeviceId());
        if (controller != null && event.getRepeatCount() == 0) {
            boolean handled = controller.updateStateFromKeyEvent(event);
            if (handled) {
                sendGamepadState(controller);
                logFirstForwardedControllerEvent("key=" + event.getKeyCode(), event.getDeviceId());
            }
            return handled;
        }
        return false;
    }

    private void logFirstForwardedControllerEvent(String kind, int deviceId) {
        if (forwardedControllerEvents.getAndIncrement() == 0) {
            Integer slot;
            synchronized (controllerLock) { slot = deviceToSlot.get(deviceId); }
            Log.i("WinHandler", "First Android controller event forwarded: device="
                    + deviceId + " slot=" + slot + " kind=" + kind
                    + " inputType=" + inputType);
        }
    }

    public byte getInputType() {
        return inputType;
    }

    public void setInputType(byte inputType) {
        this.inputType = inputType;
    }
}
