package com.wemu.engine.core;

import java.io.File;
import java.util.Locale;

/** Launch-time compatibility policy for Stardew Valley's bundled CoreCLR host. */
public final class StardewCoreClrCompatibility {
    private static final String STARDEW_EXE = "stardew valley.exe";
    private static final String SMAPI_EXE = "stardewmoddingapi.exe";
    public static final String CHILD_LOG_GUEST_PATH = "C:\\wemu-stardew-child.log";
    public static final String COREHOST_LOG_GUEST_PATH = "C:\\wemu-stardew-corehost.log";

    private static final String[] FEX_APP_CONFIG_NAMES = {
            "Stardew Valley.exe.json",
            "StardewModdingAPI.exe.json",
            "Steam_0_Stardew Valley.exe.json",
            "Steam_0_StardewModdingAPI.exe.json",
            "Steam_413150_Stardew Valley.exe.json",
            "Steam_413150_StardewModdingAPI.exe.json",
    };

    /*
     * This is deliberately per executable. A desktop may launch unrelated games
     * from one prefix, so one managed game's conservative settings must not slow
     * or otherwise alter every other process in the session.
     */
    private static final String[][] CORECLR_ENVIRONMENT = {
            {"DOTNET_EnableWriteXorExecute", "0"},
            {"COREHOST_TRACE", "1"},
            {"COREHOST_TRACEFILE", COREHOST_LOG_GUEST_PATH},
            {"COREHOST_TRACE_VERBOSITY", "4"},

            /*
             * FEX's Performance preset disables x86 TSO. Upstream documents
             * that as highly likely to break multithreaded applications; a
             * managed runtime is exactly such an application. Keep vector and
             * memcpy TSO at their upstream defaults because forcing either has
             * made CoreCLR corruption worse in current FEX reports.
             */
            {"FEX_TSOENABLED", "1"},
            {"FEX_VECTORTSOENABLED", "0"},
            {"FEX_MEMCPYSETTSOENABLED", "0"},
            {"FEX_HALFBARRIERTSOENABLED", "1"},
            {"FEX_X87REDUCEDPRECISION", "0"},
            {"FEX_MULTIBLOCK", "0"},
            {"FEX_SMCCHECKS", "1"},

            /* Same conservative policy when this process is routed to Box64. */
            {"BOX64_PROFILE", "safest"},
            {"BOX64_DYNAREC_BIGBLOCK", "0"},
            {"BOX64_DYNAREC_NOHOTPAGE", "0"},
            {"BOX64_DYNAREC_WAIT", "1"},
            {"BOX64_DYNAREC_STRONGMEM", "4"},
            {"BOX64_DYNAREC_SAFEFLAGS", "2"},
            {"BOX64_DYNAREC_CALLRET", "0"},
            {"BOX64_DYNAREC_FORWARD", "0"},
            {"BOX64_DYNAREC_INTERP_SIGNAL", "1"},
            {"BOX64_MAXCPU", "4"},
            {"BOX64_LOG", "1"},
            {"BOX64_SHOWSEGV", "1"},
            {"BOX64_SHOWBT", "1"},
    };

    private StardewCoreClrCompatibility() {}

    /** Returns true for the vanilla and SMAPI CoreCLR host executables only. */
    public static boolean isTargetExecutable(String commandOrPath) {
        if (commandOrPath == null) return false;
        String value = commandOrPath.trim();
        if (value.length() >= 2 && value.charAt(0) == '"') {
            int closing = value.indexOf('"', 1);
            value = closing >= 0 ? value.substring(1, closing) : value.substring(1);
        }
        int slash = Math.max(value.lastIndexOf('/'), value.lastIndexOf('\\'));
        String name = (slash >= 0 ? value.substring(slash + 1) : value)
                .trim().toLowerCase(Locale.ROOT);
        return STARDEW_EXE.equals(name) || name.startsWith(STARDEW_EXE + " ")
                || SMAPI_EXE.equals(name) || name.startsWith(SMAPI_EXE + " ");
    }

    /** Applies the policy after user and performance-preset environment merges. */
    public static boolean applyTo(EnvVars environment, String commandOrPath) {
        if (environment == null || !isTargetExecutable(commandOrPath)) return false;
        for (String[] entry : CORECLR_ENVIRONMENT) {
            environment.put(entry[0], entry[1]);
        }
        return true;
    }

    /** Writes quoted cmd.exe set commands for a dynamically launched target. */
    public static boolean appendBatchEnvironment(StringBuilder batch, String commandOrPath) {
        if (batch == null || !isTargetExecutable(commandOrPath)) return false;
        for (String[] entry : CORECLR_ENVIRONMENT) {
            batch.append("set \"").append(entry[0]).append('=').append(entry[1])
                    .append("\"\r\n");
        }
        return true;
    }

    /**
     * Builds a console-free Windows Script Host launcher. It waits for Stardew
     * and records the exit code without leaving a cmd.exe window over the game.
     */
    public static String hiddenLauncherScriptContents(
            String commandOrPath,
            String parameters) {
        if (!isTargetExecutable(commandOrPath)) return null;
        String directory = commandOrPath.contains("\\")
                ? commandOrPath.substring(0, commandOrPath.lastIndexOf('\\'))
                : "";
        String args = parameters == null ? "" : parameters.trim();
        String command = "\"" + commandOrPath + "\"" + (args.isEmpty() ? "" : " " + args);

        StringBuilder script = new StringBuilder();
        script.append("Option Explicit\r\n");
        script.append("On Error Resume Next\r\n");
        script.append("Dim shell, environment, fso, logFile, exitCode, launchError\r\n");
        script.append("Set shell = CreateObject(\"WScript.Shell\")\r\n");
        script.append("Set environment = shell.Environment(\"Process\")\r\n");
        for (String[] entry : CORECLR_ENVIRONMENT) {
            script.append("environment(\"").append(vbString(entry[0])).append("\") = \"")
                    .append(vbString(entry[1])).append("\"\r\n");
        }
        if (!directory.isEmpty()) {
            script.append("shell.CurrentDirectory = \"").append(vbString(directory))
                    .append("\"\r\n");
        }
        script.append("Set fso = CreateObject(\"Scripting.FileSystemObject\")\r\n");
        script.append("If fso.FileExists(\"").append(vbString(COREHOST_LOG_GUEST_PATH))
                .append("\") Then fso.DeleteFile \"")
                .append(vbString(COREHOST_LOG_GUEST_PATH)).append("\", True\r\n");
        script.append("Set logFile = fso.CreateTextFile(\"")
                .append(vbString(CHILD_LOG_GUEST_PATH)).append("\", True)\r\n");
        script.append("logFile.WriteLine \"[wemu] Stardew child starting\"\r\n");
        script.append("logFile.Close\r\n");
        script.append("launchError = \"\"\r\n");
        script.append("exitCode = shell.Run(\"").append(vbString(command))
                .append("\", 1, True)\r\n");
        script.append("If Err.Number <> 0 Then\r\n");
        script.append("  launchError = \" launcher error \" & CStr(Err.Number) & \": \" & Err.Description\r\n");
        script.append("  exitCode = Err.Number\r\n");
        script.append("  Err.Clear\r\n");
        script.append("End If\r\n");
        script.append("Set logFile = fso.OpenTextFile(\"")
                .append(vbString(CHILD_LOG_GUEST_PATH)).append("\", 8, True)\r\n");
        script.append("logFile.WriteLine \"[wemu] Stardew child exit code \" & CStr(exitCode) & launchError\r\n");
        script.append("logFile.Close\r\n");
        script.append("WScript.Quit exitCode\r\n");
        return script.toString();
    }

    private static String vbString(String value) {
        return value.replace("\r", " ").replace("\n", " ").replace("\"", "\"\"");
    }

    /**
     * FEX reads an application layer before translating a Windows executable.
     * Installing the policy there makes the memory model safe before CoreCLR's
     * first instruction while leaving other programs in the same Wine desktop
     * (notably Skyrim) on the user's selected preset.
     */
    public static int installFexAppConfigs(File imageFsRoot, File containerHome) {
        if (imageFsRoot == null || containerHome == null) return 0;
        File[] directories = {
                new File(containerHome, ".fex-emu/AppConfig"),
                new File(containerHome, ".config/fex-emu/AppConfig"),
                new File(imageFsRoot, "AppConfig"),
        };
        String contents = fexAppConfigContents();
        int installed = 0;
        for (File directory : directories) {
            for (String name : FEX_APP_CONFIG_NAMES) {
                if (FileUtils.writeString(new File(directory, name), contents)) installed++;
            }
        }
        return installed;
    }

    public static String fexAppConfigContents() {
        return "{\n"
                + "  \"Config\": {\n"
                + "    \"TSOEnabled\": \"1\",\n"
                + "    \"VectorTSOEnabled\": \"0\",\n"
                + "    \"MemcpySetTSOEnabled\": \"0\",\n"
                + "    \"HalfBarrierTSOEnabled\": \"1\",\n"
                + "    \"X87ReducedPrecision\": \"0\",\n"
                + "    \"Multiblock\": \"0\",\n"
                + "    \"SMCChecks\": \"1\",\n"
                + "    \"SilentLog\": \"0\",\n"
                + "    \"OutputLog\": \"stderr\"\n"
                + "  }\n"
                + "}\n";
    }

    /** Box64 rc fragment used when Steam, rather than Wemu, creates the child. */
    public static String box64ProfileContents() {
        return "BOX64_ENV=WINEDLLOVERRIDES=icu=n\n"
                + "BOX64_ENV1=DOTNET_EnableWriteXorExecute=0\n"
                + "BOX64_PROFILE=safest\n"
                + "BOX64_DYNAREC_BIGBLOCK=0\n"
                + "BOX64_DYNAREC_NOHOTPAGE=0\n"
                + "BOX64_DYNAREC_WAIT=1\n"
                + "BOX64_DYNAREC_STRONGMEM=4\n"
                + "BOX64_DYNAREC_SAFEFLAGS=2\n"
                + "BOX64_DYNAREC_CALLRET=0\n"
                + "BOX64_DYNAREC_FORWARD=0\n"
                + "BOX64_DYNAREC_INTERP_SIGNAL=1\n"
                + "BOX64_MAXCPU=4\n"
                + "BOX64_LOG=1\n"
                + "BOX64_SHOWSEGV=1\n"
                + "BOX64_SHOWBT=1\n";
    }
}
