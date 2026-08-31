package com.wemu.runtime.internal;

import com.wemu.engine.core.StardewCoreClrCompatibility;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;

/** Launch-time translation policy for Valve's official Windows Steam client. */
public final class SteamClientRuntimePolicy {
    private static final String[] CLIENT_PATHS = {
            "drive_c/Program Files (x86)/Steam/steam.exe",
            "drive_c/Program Files/Steam/steam.exe",
    };
    private static final String BOX64_PROFILE_FILE = "official-steam-v2.box64rc";

    /*
     * Keep this deliberately small. These are the current upstream Box64
     * compatibility settings for Steam and steamwebhelper, adapted to the
     * Windows process names Box64 sees through Wine. In particular, this is
     * not a copy of the old community Steam launch recipes: it does not turn
     * off Chromium's sandbox, inject credentials, or alter a game executable.
     */
    private static final String BOX64_PROFILE_PREFIX =
            "# Managed by Wemu for Valve's official Windows Steam client.\n"
            + "[steam.exe]\n"
            + "BOX64_DYNAREC_BIGBLOCK=3\n"
            + "BOX64_DYNAREC_CALLRET=1\n"
            + "BOX64_DYNAREC_STRONGMEM=2\n"
            + "BOX64_AVX=0\n"
            + "BOX64_MAXCPU=8\n";
    private static final String BOX64_WEBHELPER_PROFILE =
            "\n"
            + "[steamwebhelper.exe]\n"
            // Match Box64's pinned ARM64 Windows-Steam profile. In particular,
            // PURGE is not a generic experiment: upstream enables it for this
            // exact guest basename so retired CEF dynablocks do not grow for
            // the life of every renderer generation.
            + "BOX64_DYNAREC_STRONGMEM=1\n"
            + "BOX64_DYNAREC_BIGBLOCK=3\n"
            + "BOX64_DYNAREC_CALLRET=1\n"
            + "BOX64_DYNAREC_FASTROUND=0\n"
            + "BOX64_DYNAREC_SAFEFLAGS=0\n"
            + "BOX64_DYNAREC_PURGE=1\n"
            // Box64's x86 AVX path omits memory barriers that Chromium's
            // lock-free renderer threads rely on; the upstream Steam profile
            // therefore keeps AVX disabled for this process family.
            + "BOX64_AVX=0\n";
    private static final String BOX64_PROFILE_SUFFIX =
            "\n"
            /*
             * Stardew's self-contained CoreCLR runtime emits native code on
             * demand as methods are first called and patches their entry stubs.
             * Its runtime config disables tiered compilation, but that does not
             * create a point at which lazy JIT/code fixups have "settled".
             * Box64's hot-page detection protects pages that are written while
             * translated code is executing, so keep it enabled even if a
             * container preset asks for the unsafe NOHOTPAGE optimization.
             *
             * Box64 documents BIGBLOCK=0 as suitable for programs with many threads and JIT code.
             * A real Thor run still terminated silently with that initial profile, so this policy
             * also forces Box64's safest preset, full x86-style memory ordering, conservative flag
             * and call/return handling, and deterministic block construction. CoreCLR's MinOpts
             * mode reduces the complexity of the x64 code that Box64 must translate a second time.
             * Minimal Box64 signal logging is enabled because CoreCLR/SMAPI may disappear without
             * producing their own managed exception log.
             *
             * Steam can launch either vanilla Stardew or SMAPI, which hosts
             * the game inside StardewModdingAPI.exe. The managed RCFILE is in
             * Steam's environment and its children inherit it, while Box64
             * selects rules by guest basename. Both names therefore need the
             * same policy. ICU is included for both because the selected
             * process is the CoreCLR host that initialises globalization.
             */
             + "[Stardew Valley.exe]\n"
             + StardewCoreClrCompatibility.box64ProfileContents()
             + "\n"
             + "[StardewModdingAPI.exe]\n"
             + StardewCoreClrCompatibility.box64ProfileContents();

    private SteamClientRuntimePolicy() {}

    /**
     * The profile is only meaningful when Box64 is translating x86_64 Wine.
     * ARM64EC uses FEX/wow64 and ordinary game descriptors retain their
     * container preset and the normal no-rc-files policy.
     */
    public static boolean shouldUseOfficialSteamBox64Profile(
            boolean officialSteamLaunch,
            boolean arm64EcWine) {
        return officialSteamLaunch && !arm64EcWine;
    }

    /**
     * Whether this launch must keep Wine's Proton lsteamclient bridge out of
     * the process.
     *
     * <p>Valve Proton normally runs a Windows game below the native Linux
     * Steam client. Its Wine loader therefore tries to replace
     * {@code steamclient.dll}, {@code steamclient64.dll}, and the overlay DLLs
     * with Proton's lsteamclient bridge. Wemu's managed route is deliberately
     * the opposite topology: Valve's genuine <em>Windows</em> Steam client is
     * running inside the same Wine prefix and owns those DLLs. Letting Proton
     * probe or redirect them prevents the Windows client from initialising its
     * ClientAPI on the newer P10/P11 Wine loaders. Disable that bridge for the
     * client and let the value be inherited by games Steam launches, so DRM,
     * cloud, achievements, multiplayer, and the authentic overlay continue to
     * talk to the real Windows client.</p>
     *
     * <p>The legacy ColdClient loader already required the same loader switch;
     * retaining it here is compatibility policy, not an expansion of that
     * emulation path.</p>
     */
    public static boolean shouldDisableProtonLsteamclient(
            boolean officialSteamLaunch,
            String shimMode) {
        return officialSteamLaunch || "COLDCLIENT".equals(shimMode);
    }

    /**
     * Materialises the trusted Box64 profile in an app-private directory.
     *
     * <p>The temporary file is fully written and synced before one atomic
     * rename publishes it. A failure is returned to the caller instead of
     * silently starting Steam with a partial or stale compatibility file.</p>
     */
    public static File ensureOfficialSteamBox64Profile(
            File privateRuntimeDirectory,
            boolean softwareCef)
            throws IOException {
        if (privateRuntimeDirectory == null) {
            throw new IOException("The private runtime directory is unavailable");
        }
        if (!privateRuntimeDirectory.isDirectory()
                && !privateRuntimeDirectory.mkdirs()
                && !privateRuntimeDirectory.isDirectory()) {
            throw new IOException(
                    "Could not create the private runtime directory: "
                            + privateRuntimeDirectory);
        }

        String contents = officialSteamBox64ProfileContents(softwareCef);
        byte[] expected = contents.getBytes(StandardCharsets.UTF_8);
        File destination = new File(
                privateRuntimeDirectory,
                (softwareCef ? "ui-" : "game-") + BOX64_PROFILE_FILE);
        if (destination.isFile()
                && Arrays.equals(Files.readAllBytes(destination.toPath()), expected)) {
            return destination;
        }

        Path temporary = Files.createTempFile(
                privateRuntimeDirectory.toPath(), ".official-steam-", ".tmp");
        boolean published = false;
        try {
            try (FileOutputStream output = new FileOutputStream(temporary.toFile())) {
                output.write(expected);
                output.getFD().sync();
            }
            Files.move(
                    temporary,
                    destination.toPath(),
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            published = true;
        } finally {
            if (!published) Files.deleteIfExists(temporary);
        }

        if (!destination.isFile()
                || !Arrays.equals(Files.readAllBytes(destination.toPath()), expected)) {
            throw new IOException("The official Steam Box64 profile could not be verified");
        }
        return destination;
    }

    static String officialSteamBox64ProfileContents(boolean softwareCef) {
        return BOX64_PROFILE_PREFIX
                + BOX64_WEBHELPER_PROFILE
                + BOX64_PROFILE_SUFFIX;
    }

    /**
     * Current Steam CEF builds can crash under FEX multiblock translation.
     *
     * <p>Official descriptors always launch Steam itself. A bare desktop needs
     * the same policy only when Steam is installed in that session's exact
     * prefix, because Steam may then be opened manually. An ordinary shortcut
     * must retain the container's selected performance preset.</p>
     */
    static boolean shouldDisableFexMultiblock(
            boolean officialSteamLaunch,
            boolean bareContainerSession,
            File winePrefix) {
        if (officialSteamLaunch) return true;
        return bareContainerSession && hasOfficialClient(winePrefix);
    }

    private static boolean hasOfficialClient(File winePrefix) {
        if (winePrefix == null || !winePrefix.isDirectory()) return false;
        for (String relativePath : CLIENT_PATHS) {
            if (new File(winePrefix, relativePath).isFile()) return true;
        }
        return false;
    }
}
