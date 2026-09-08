package com.wemu.engine.core;

import android.os.Process;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FilenameFilter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public abstract class ProcessHelper {
    /**
     * Whether every line the guest prints is also written to logcat.
     *
     * Upstream shipped this on with its own {@code FIXME} beside it, and on
     * upstream it was nearly free: nothing registered a debug callback unless
     * the user opened the debug dialog, so the streams went to {@code /dev/null}
     * and this loop never ran.
     *
     * Here it is not free. The console has a Log page and {@code SessionLog}
     * attaches for the whole session, so {@link #debugCallbacks} is never empty
     * — which means the guest's stdout and stderr are read line by line for
     * every container, and each line was costing a {@code System.out.println}.
     * That is a write to the logd socket per line, on a reader thread, next to
     * a running game, to produce output nobody can see on a handheld.
     *
     * Off by default. The callbacks still receive every line, so the Log page is
     * unchanged; what is gone is the second copy going somewhere unreadable.
     *
     * Not a constant, though, and the reason is worth recording. Turning it off
     * also removed the only way to read guest output over `adb` — which was
     * discovered immediately afterwards, while debugging a game that opened a
     * black window. A diagnostic that costs every session something is the wrong
     * trade; a diagnostic that cannot be switched on at all is a worse one. So
     * it follows the Wine debug preference: off for everyone, on for the session
     * that is being investigated, decided by the same switch that already turns
     * on the debug channels the output would be describing.
     */
    private static volatile boolean printDebug = false;

    public static boolean isPrintDebug() { return printDebug; }

    /** Set from the Wine debug preference when a session starts. */
    public static void setPrintDebug(boolean enabled) { printDebug = enabled; }
    private static final ArrayList<Callback<String>> debugCallbacks = new ArrayList<>();

    /**
     * One pool for every reader and waiter, instead of one pool each.
     *
     * {@code Executors.newSingleThreadExecutor()} was called per stream and per
     * process and never shut down, so each {@link #exec} leaked up to three
     * executors whose non-daemon threads outlive the process they were reading.
     * A session that launches several programs accumulates them for as long as
     * the app is alive.
     *
     * Daemon threads, so a leftover reader blocked on a pipe that will never
     * close cannot by itself keep the VM up.
     */
    private static final java.util.concurrent.ExecutorService ioExecutor =
            Executors.newCachedThreadPool(runnable -> {
                Thread thread = new Thread(runnable, "guest-io");
                thread.setDaemon(true);
                return thread;
            });
    private static final byte SIGCONT = 18;
    private static final byte SIGSTOP = 19;
    private static final byte SIGTERM = 15;
    private static final byte SIGKILL = 9;

    /**
     * Pins a process, and everything it goes on to spawn, to a set of cores.
     *
     * The syscall wrapper for this has been in `process_helper.c` since the
     * fork began with nothing in Java declaring it, so the one lever that
     * decides which cores a game runs on was unreachable. What the container's
     * CPU list did instead was set `WINE_CPU_TOPOLOGY`, which changes how many
     * processors Wine *reports* — a different question entirely, and not one
     * the scheduler reads.
     *
     * It matters here more than on a desktop. This is a big.LITTLE phone: the
     * cores are not interchangeable, and a game thread that lands on a little
     * one does not run a bit slower, it runs at a fraction of the speed. The
     * kernel migrates threads on a load signal that a translated x86 workload
     * confuses, so left alone it will happily park the emulator on a small core
     * and leave it there.
     *
     * Children inherit the mask, so this is set once on the process the whole
     * guest tree hangs off.
     *
     * @param mask one bit per core, or 0 to leave the kernel alone.
     * @return true if the kernel accepted it.
     */
    public static boolean setProcessAffinity(int pid, int mask) {
        if (mask == 0) return false;
        try {
            int result = nativeSetProcessAffinity(pid, mask);
            if (result != 0) {
                Log.w("ProcessHelper", "sched_setaffinity refused for " + pid + ": " + result);
                return false;
            }
            Log.d("ProcessHelper", "Pinned " + pid + " to mask 0x" + Integer.toHexString(mask));
            return true;
        } catch (Throwable t) {
            // A refused affinity is a slower game, not a broken one.
            Log.w("ProcessHelper", "Could not set affinity", t);
            return false;
        }
    }

    private static native int nativeSetProcessAffinity(int pid, int mask);
    private static native int nativeGetProcessAffinity(int pid);

    public static void suspendProcess(int pid) {
        Process.sendSignal(pid, SIGSTOP);
        Log.d("ProcessHelper", "Process suspended with pid: " + pid);
    }

    public static void resumeProcess(int pid) {
        Process.sendSignal(pid, SIGCONT);
        Log.d("ProcessHelper", "Process resumed with pid: " + pid);
    }

    public static void terminateProcess(int pid) {
        Process.sendSignal(pid, SIGTERM);
        Log.d("ProcessHelper", "Process terminated with pid: " + pid);
    }

    public static void killProcess(int pid) {
        Process.sendSignal(pid, SIGKILL);
        Log.d("ProcessHelper", "Process killed with pid: " + pid);
    }

    public static void terminateAllWineProcesses() {
        for (String process : listRunningWineProcesses()) {
            terminateProcess(Integer.parseInt(process));
        }
    }

    public static void pauseAllWineProcesses() {
        for (String process : listRunningWineProcesses()) {
            suspendProcess(Integer.parseInt(process));
        }
    }

    public static void resumeAllWineProcesses() {
        for (String process : listRunningWineProcesses()) {
            resumeProcess(Integer.parseInt(process));
        }
    }

    public static int exec(String command) {
        return exec(command, null);
    }

    public static int exec(String command, String[] envp) {
        return exec(command, envp, null);
    }

    public static int exec(String command, String[] envp, File workingDir) {
        return exec(command, envp, workingDir, null);
    }

    /**
     * Runs one already-split helper command and waits for its real exit status.
     *
     * <p>This is intentionally separate from {@link #exec(String[], String[], File, Callback)}:
     * guest applications are long-lived and asynchronous, while prefix maintenance is a
     * launch barrier whose caller must know that it completed before starting the guest. A
     * finite timeout is mandatory so a broken Wine helper cannot permanently wedge session
     * startup.</p>
     *
     * @throws IOException when the helper cannot be started
     * @throws InterruptedException when the session-start thread is interrupted
     * @throws TimeoutException when the helper does not exit before {@code timeout}
     */
    public static int execAndWait(
            String[] argv,
            String[] envp,
            File workingDir,
            long timeout,
            TimeUnit unit)
            throws IOException, InterruptedException, TimeoutException {
        if (argv == null || argv.length == 0) {
            throw new IOException("No helper command was provided");
        }
        if (timeout <= 0L || unit == null) {
            throw new IllegalArgumentException("A positive helper timeout is required");
        }

        Log.d("ProcessHelper", "Starting bounded helper: " + Arrays.toString(argv));
        ProcessBuilder builder = new ProcessBuilder(argv);
        if (workingDir != null) builder.directory(workingDir);
        if (envp != null) {
            for (String entry : envp) {
                if (entry == null) continue;
                int equals = entry.indexOf('=');
                if (equals > 0) {
                    builder.environment().put(
                            entry.substring(0, equals), entry.substring(equals + 1));
                }
            }
        }
        if (debugCallbacks.isEmpty()) {
            File nullFile = new File("/dev/null");
            builder.redirectError(nullFile);
            builder.redirectOutput(nullFile);
        }

        java.lang.Process process = builder.start();
        if (!debugCallbacks.isEmpty()) {
            createDebugThread(process.getInputStream());
            createDebugThread(process.getErrorStream());
        }
        try {
            if (!process.waitFor(timeout, unit)) {
                process.destroyForcibly();
                process.waitFor(250L, TimeUnit.MILLISECONDS);
                throw new TimeoutException(
                        "Helper timed out: " + Arrays.toString(argv));
            }
            return process.exitValue();
        } catch (InterruptedException interrupted) {
            process.destroyForcibly();
            throw interrupted;
        }
    }

    /**
     * Runs an already-split command, without going through {@link #splitCommand}.
     *
     * That splitter keeps the quote characters inside the token it produces, so
     * a quoted program name reaches the guest with its quotes attached. Every
     * caller of the string form talks to `winhandler.exe`, which re-parses its
     * own command line and strips them; anything else receives a program name
     * that includes two `"` characters and cannot be found.
     *
     * A caller that knows its arguments should say so here rather than encode
     * them into a string for this class to guess at again.
     */
    public static int exec(String[] argv, String[] envp, File workingDir, Callback<Integer> terminationCallback) {
        return execArgv(argv, envp, workingDir, terminationCallback);
    }

    /** Opt-in: set only the spawning thread's mask before fork/exec, then restore it. */
    public static int execWithAffinity(String[] argv, String[] envp, File workingDir,
            Callback<Integer> terminationCallback, int mask) {
        return execArgv(argv, envp, workingDir, terminationCallback, mask);
    }

    public static int execWithAffinity(String command, String[] envp, File workingDir,
            Callback<Integer> terminationCallback, int mask) {
        return execArgv(splitCommand(command), envp, workingDir, terminationCallback, mask);
    }

    public static int exec(String command, String[] envp, File workingDir, Callback<Integer> terminationCallback) {
        Log.d("ProcessHelper", "env: " + Arrays.toString(envp) + "\ncmd: " + command);

        // Store env vars for future use
        EnvironmentManager.setEnvVars(envp);

        int pid = -1;
        try {
            Log.d("ProcessHelper", "Splitting command: " + command);
            String[] splitCommand = splitCommand(command);
            Log.d("ProcessHelper", "Split command result: " + Arrays.toString(splitCommand));
            return execArgv(splitCommand, envp, workingDir, terminationCallback);
        }
        catch (Exception e) {
            Log.e("ProcessHelper", "Error executing command: " + command, e);
        }
        return pid;
    }

    private static int execArgv(String[] argv, String[] envp, File workingDir, Callback<Integer> terminationCallback) {
        return execArgv(argv, envp, workingDir, terminationCallback, 0);
    }

    private static int execArgv(String[] argv, String[] envp, File workingDir,
            Callback<Integer> terminationCallback, int spawnMask) {
        EnvironmentManager.setEnvVars(envp);

        int pid = -1;
        java.lang.Process process = null;
        boolean published = false;
        try {
            Log.i("ProcessHelper", "Starting process: argv=" + Arrays.toString(argv)
                    + " cwd=" + workingDir);
            ProcessBuilder pb = new ProcessBuilder(argv);
            pb.directory(workingDir);
            pb.environment().putAll(EnvironmentManager.getEnvVars());
            if (debugCallbacks.isEmpty()) {
                File null_file = new File("/dev/null");
                pb.redirectError(null_file);
                pb.redirectOutput(null_file);
            }
            // Pin before the child can create Wine descendants. Setting only
            // its PID after pb.start() races with start.exe/explorer/winhandler.
            // This opt-in touches the calling TID, not Android's whole process.
            int callingTid = android.os.Process.myTid();
            try (CpuAffinityScope affinity = CpuAffinityScope.open(spawnMask, new CpuAffinityScope.Scheduler() {
                @Override public int get() { return nativeGetProcessAffinity(callingTid); }
                @Override public int set(int mask) { return nativeSetProcessAffinity(callingTid, mask); }
            }, ProcessHelper::emitDiagnostic)) {
                affinity.configureEnvironment(pb.environment());
                process = pb.start();
            }

            /*
             * Android's runtime exposes a process id, but the android.jar used
             * by this project does not declare Java 9's Process.pid(). Resolve
             * the public method when present and retain the long-standing
             * Android ProcessImpl field fallback. Unlike the old code, any
             * lookup failure now reaches finally and destroys the already-
             * started untracked child instead of leaking Wine in the prefix.
             */
            pid = resolvePid(process);
            Log.i("ProcessHelper", "Process creation succeeded: pid=" + pid);

            if (!debugCallbacks.isEmpty()) {
                createDebugThread(process.getInputStream());
                createDebugThread(process.getErrorStream());
            }

            if (terminationCallback != null) createWaitForThread(process, pid, terminationCallback);
            published = true;

        }
        catch (Exception e) {
            Log.e("ProcessHelper", "Error executing " + Arrays.toString(argv), e);
            pid = -1;
        } finally {
            if (process != null && !published) destroyUntrackedProcess(process);
        }
        return pid;
    }

    private static int resolvePid(java.lang.Process process) throws Exception {
        try {
            Object result = java.lang.Process.class.getMethod("pid").invoke(process);
            if (result instanceof Number) {
                long value = ((Number) result).longValue();
                if (value > 0 && value <= Integer.MAX_VALUE) return (int) value;
            }
        } catch (ReflectiveOperationException ignored) {
            // Android's older compile/runtime surface uses ProcessImpl.pid.
        }

        java.lang.reflect.Field pidField = process.getClass().getDeclaredField("pid");
        boolean accessible = pidField.isAccessible();
        try {
            pidField.setAccessible(true);
            int value = pidField.getInt(process);
            if (value <= 0) throw new IOException("Process returned an invalid pid: " + value);
            return value;
        } finally {
            pidField.setAccessible(accessible);
        }
    }

    private static void destroyUntrackedProcess(java.lang.Process process) {
        process.destroy();
        try {
            if (!process.waitFor(250, TimeUnit.MILLISECONDS)) process.destroyForcibly();
        } catch (InterruptedException interrupted) {
            process.destroyForcibly();
            Thread.currentThread().interrupt();
        }
        try { process.getInputStream().close(); } catch (IOException ignored) {}
        try { process.getErrorStream().close(); } catch (IOException ignored) {}
        try { process.getOutputStream().close(); } catch (IOException ignored) {}
    }

    private static void createDebugThread(final InputStream inputStream) {
        ioExecutor.execute(() -> {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if (printDebug) System.out.println(line);
                    synchronized (debugCallbacks) {
                        if (!debugCallbacks.isEmpty()) {
                            for (Callback<String> callback : debugCallbacks) callback.call(line);
                        }
                    }
                }
            }
            catch (IOException e) {
                Log.e("ProcessHelper", "Error in debug thread", e);
            }
        });
    }

    private static void createWaitForThread(java.lang.Process process, int pid,
                                            final Callback<Integer> terminationCallback) {
        ioExecutor.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    int status = process.waitFor();
                    Log.i("ProcessHelper", "Guest launcher exited: pid=" + pid
                            + " status=" + status);
                    terminationCallback.call(status);
                }
                catch (InterruptedException e) {
                    Log.e("ProcessHelper", "Error waiting for process termination", e);
                }
            }
        });
    }

    public static void removeAllDebugCallbacks() {
        synchronized (debugCallbacks) {
            debugCallbacks.clear();
            Log.d("ProcessHelper", "All debug callbacks removed");
        }
    }

    public static void addDebugCallback(Callback<String> callback) {
        synchronized (debugCallbacks) {
            if (!debugCallbacks.contains(callback)) debugCallbacks.add(callback);
            Log.d("ProcessHelper", "Added debug callback: " + callback.toString());
        }
    }

    public static void removeDebugCallback(Callback<String> callback) {
        synchronized (debugCallbacks) {
            debugCallbacks.remove(callback);
            Log.d("ProcessHelper", "Removed debug callback: " + callback.toString());
        }
    }

    /**
     * Publishes one bounded, app-generated diagnostic beside guest output.
     *
     * <p>The session Log page is backed by {@link #debugCallbacks}; writing only
     * to logcat makes a diagnosis unavailable on the handheld where it is most
     * useful.  Components use this entry point for low-rate health summaries,
     * never raw environment blocks or command lines.  The caller is responsible
     * for keeping the message free of credentials and private paths.</p>
     */
    public static void emitDiagnostic(String message) {
        if (message == null || message.isEmpty()) return;
        String line = "[wemu-diag] " + message;
        Log.i("Wemu_Diagnostics", message);
        synchronized (debugCallbacks) {
            for (Callback<String> callback : debugCallbacks) {
                try {
                    callback.call(line);
                } catch (RuntimeException failure) {
                    // A diagnostics consumer must never be able to terminate
                    // the health sampler or a renderer submission path.
                    Log.w("Wemu_Diagnostics", "Diagnostic callback failed", failure);
                }
            }
        }
    }

    public static String[] splitCommand(String command) {
        ArrayList<String> result = new ArrayList<>();
        boolean startedQuotes = false;
        String value = "";
        char currChar, nextChar;
        for (int i = 0, count = command.length(); i < count; i++) {
            currChar = command.charAt(i);

            if (startedQuotes) {
                if (currChar == '"') {
                    startedQuotes = false;
                    if (!value.isEmpty()) {
                        value += '"';
                        result.add(value);
                        value = "";
                    }
                }
                else value += currChar;
            }
            else if (currChar == '"') {
                startedQuotes = true;
                value += '"';
            }
            else {
                nextChar = i < count-1 ? command.charAt(i+1) : '\0';
                if (currChar == ' ' || (currChar == '\\' && nextChar == ' ')) {
                    if (currChar == '\\') {
                        value += ' ';
                        i++;
                    }
                    else if (!value.isEmpty()) {
                        result.add(value);
                        value = "";
                    }
                }
                else {
                    value += currChar;
                    if (i == count-1) {
                        result.add(value);
                        value = "";
                    }
                }
            }
        }

        return result.toArray(new String[0]);
    }

    public static String getAffinityMaskAsHexString(String cpuList) {
        String[] values = cpuList.split(",");
        int affinityMask = 0;
        for (String value : values) {
            byte index = Byte.parseByte(value);
            affinityMask |= (int)Math.pow(2, index);
        }
        return Integer.toHexString(affinityMask);
    }

    public static int getAffinityMask(String cpuList) {
        if (cpuList == null || cpuList.isEmpty()) return 0;
        String[] values = cpuList.split(",");
        int affinityMask = 0;
        for (String value : values) {
            byte index = Byte.parseByte(value);
            affinityMask |= (int)Math.pow(2, index);
        }
        return affinityMask;
    }

    public static int getAffinityMask(boolean[] cpuList) {
        int affinityMask = 0;
        for (int i = 0; i < cpuList.length; i++) {
            if (cpuList[i]) affinityMask |= (int)Math.pow(2, i);
        }
        return affinityMask;
    }

    public static int getAffinityMask(int from, int to) {
        int affinityMask = 0;
        for (int i = from; i < to; i++) affinityMask |= (int)Math.pow(2, i);
        return affinityMask;
    }

    public static ArrayList<String> listRunningWineProcesses(){
        File proc = new File("/proc");
        String[] filters = {"wine", "exe"};
        String[] allPids;
        ArrayList<String> filteredPids = new ArrayList<String>();
        List<String> filterList = Arrays.asList(filters);
        allPids = proc.list(new FilenameFilter(){
            public boolean accept(File proc, String filename){
                return new File(proc, filename).isDirectory() && filename.matches("[0-9]+");
            }
        });

        for (int index = 0; index < allPids.length; index++){
            String data = "";
            try {
                FileInputStream fr = new FileInputStream(proc + "/" + allPids[index] + "/stat");
                BufferedReader br = new BufferedReader(new InputStreamReader(fr));
                data = br.readLine();
            }
            catch (IOException e) {}
            for (String filter : filterList) {
                if (data.contains(filter))
                    filteredPids.add(allPids[index]);
            }
        }
        return filteredPids;
    }

    public static int getProcessAffinityMask(int pid) {
        try (BufferedReader br = new BufferedReader(
                new InputStreamReader(new FileInputStream("/proc/" + pid + "/status")))) {
            String line;
            while ((line = br.readLine()) != null) {
                if (line.startsWith("Cpus_allowed:")) {
                    String hex = line.substring("Cpus_allowed:".length()).trim();
                    return (int) Long.parseLong(hex, 16);
                }
            }
        } catch (Exception e) {}
        return 0;
    }

}
