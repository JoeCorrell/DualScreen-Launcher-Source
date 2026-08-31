package com.wemu.engine.session;

import android.content.Context;

import com.wemu.engine.container.Container;
import com.wemu.engine.core.WineInfo;
import com.wemu.engine.xenvironment.ImageFs;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Checks that the things a session needs are actually on disk.
 *
 * Every failure this catches used to present identically: the container unpacks,
 * Wine is exec'd, the process exits within a second, and the activity closes
 * with nothing on screen. The exit status is a number, the reason is in a log
 * nobody can reach without adb, and the difference between "Wine was never
 * extracted" and "the Vulkan ICD is missing" is invisible.
 *
 * So this asks the questions directly, before the guest is started, and the
 * answers go on the screen. It cannot catch everything — a driver that loads and
 * then crashes is still a log line — but it catches the whole class of failure
 * where a file the environment points at was never written.
 */
public final class SessionDiagnostics {

    private SessionDiagnostics() {}

    /** One thing that was checked. */
    public static final class Check {
        public final String name;
        public final boolean passed;
        /** What was looked for, or what was found instead. */
        public final String detail;

        Check(String name, boolean passed, String detail) {
            this.name = name;
            this.passed = passed;
            this.detail = detail;
        }
    }

    /**
     * Runs every check. Blocking, but only stats a handful of files.
     *
     * @param wineInfo the resolved Wine build, whose path has already been put
     *   on the image — checking `imageFs.getWinePath()` rather than recomputing
     *   it means this verifies what the launcher will actually use.
     */
    public static List<Check> run(Context context, Container container, ImageFs imageFs, WineInfo wineInfo) {
        List<Check> checks = new ArrayList<>();
        File rootDir = imageFs.getRootDir();

        checks.add(check("Image filesystem", rootDir.isDirectory(), rootDir.getPath()));
        checks.add(check(
                "Image version",
                imageFs.isValid(),
                "v" + imageFs.getVersion() + ", expected v" + com.wemu.engine.xenvironment.ImageFsInstaller.LATEST_VERSION));

        /*
         * The binary that is exec'd, by the path the launcher builds.
         *
         * `GuestProgramLauncherComponent` runs `getWinePath() + "/bin/" + ...`,
         * so this is the one file whose absence guarantees an instant exit.
         */
        File wineBin = new File(imageFs.getWinePath(), "bin/wine");
        checks.add(check(
                "Wine binary",
                wineBin.isFile(),
                wineBin.getPath() + (wineBin.isFile() && !wineBin.canExecute() ? " (not executable)" : "")));

        if (wineBin.isFile() && !wineBin.canExecute()) {
            checks.add(check("Wine is executable", false, "chmod lost on " + wineBin.getName()));
        }

        checks.add(check(
                "Wine build",
                wineInfo != null,
                wineInfo != null ? wineInfo.identifier() : "unresolved"));

        // The wrapper's Vulkan ICD, which VK_ICD_FILENAMES points at.
        File icd = new File(imageFs.getShareDir(), "vulkan/icd.d/wrapper_icd.aarch64.json");
        checks.add(check("Vulkan wrapper ICD", icd.isFile(), icd.getPath()));

        File containerDir = container.getRootDir();
        checks.add(check("Container directory", containerDir.isDirectory(), containerDir.getPath()));

        /*
         * That `home/xuser` actually resolves to *this* container.
         *
         * It is a symlink, repointed per launch by `activateContainer`, and
         * nearly everything writes through it rather than through the container's
         * real path — the Wine prefix, the DXVK extraction, the translation DLL.
         * When it is absent or stale the setup succeeds against the wrong
         * directory and the container is left untouched, which presents as a
         * session that unpacks and then exits instantly.
         *
         * Compared canonically, so the answer is about where the link lands
         * rather than how the path was spelled.
         */
        try {
            File prefix = new File(imageFs.wineprefix).getCanonicalFile();
            File expected = new File(containerDir, ".wine").getCanonicalFile();
            checks.add(check(
                    "Home points at this container",
                    prefix.equals(expected),
                    prefix.equals(expected) ? prefix.getPath() : prefix + " ≠ " + expected));
        } catch (java.io.IOException e) {
            checks.add(check("Home points at this container", false, "unreadable: " + e.getMessage()));
        }

        File driveC = new File(containerDir, ".wine/drive_c/windows");
        checks.add(check("Windows directory", driveC.isDirectory(), driveC.getPath()));

        /*
         * The x86 translation layer, as a DLL inside the container.
         *
         * On an arm64ec build the launcher sets `HODLL` to this and Wine loads
         * it to run x86 code. Missing, Wine starts and dies immediately — which
         * is the same symptom as everything else here, hence checking it.
         */
        File system32 = new File(driveC, "system32");
        File fex = new File(system32, "libwow64fex.dll");
        if (wineInfo != null && wineInfo.isArm64EC()) {
            checks.add(check("FEX translation DLL", fex.isFile(), fex.getPath()));
        } else {
            /*
             * The other half of the same question.
             *
             * A container that is not arm64ec is not exec'd through Wine at all
             * — `GuestProgramLauncherComponent` builds `usr/bin/box64 <wine…>`,
             * so box64 is the process, and its absence is an instant exit with
             * no output whatsoever. It was never checked because the arm64ec
             * path was the one being debugged, which is exactly the sort of gap
             * a preflight is supposed to close.
             */
            File box64 = new File(imageFs.getBinDir(), "box64");
            checks.add(check(
                    "Box64",
                    box64.isFile(),
                    box64.getPath() + (box64.isFile() && !box64.canExecute() ? " (not executable)" : "")));
        }

        return checks;
    }

    /** True when nothing is missing. */
    public static boolean allPassed(List<Check> checks) {
        for (Check c : checks) {
            if (!c.passed) return false;
        }
        return true;
    }

    private static Check check(String name, boolean passed, String detail) {
        return new Check(name, passed, detail);
    }
}
