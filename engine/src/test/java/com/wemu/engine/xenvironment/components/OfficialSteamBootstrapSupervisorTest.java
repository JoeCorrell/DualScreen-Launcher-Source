package com.wemu.engine.xenvironment.components;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class OfficialSteamBootstrapSupervisorTest {
    @Test
    public void startupGraceAcceptsDelayedReplacement() {
        OfficialSteamBootstrapSupervisor tracker = exitedAt(1_000L, 7);
        assertFalse(tracker.onPrefixSnapshot(
                1_000L + OfficialSteamBootstrapSupervisor.STARTUP_GRACE_MS - 1,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
        assertFalse(tracker.onPrefixSnapshot(
                20_000L,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY).shouldFinish);
    }

    @Test
    public void neverStartedSteamFinishesAfterGraceWithOriginalStatus() {
        OfficialSteamBootstrapSupervisor tracker = exitedAt(1_000L, 9);
        OfficialSteamBootstrapSupervisor.Decision decision = tracker.onPrefixSnapshot(
                1_000L + OfficialSteamBootstrapSupervisor.STARTUP_GRACE_MS,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE);
        assertTrue(decision.shouldFinish);
        assertTrue(decision.exitStatus == 9);
    }

    @Test
    public void updaterRestartGapDoesNotFinishSession() {
        OfficialSteamBootstrapSupervisor tracker = exitedAt(1_000L, 0);
        tracker.onPrefixSnapshot(2_000L,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY);
        assertFalse(tracker.onPrefixSnapshot(
                2_000L + OfficialSteamBootstrapSupervisor.RESTART_GRACE_MS - 1,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
        assertFalse(tracker.onPrefixSnapshot(
                2_000L + OfficialSteamBootstrapSupervisor.RESTART_GRACE_MS,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY).shouldFinish);
    }

    @Test
    public void prerequisiteOrGameKeepsSessionAliveAfterSteamDisappears() {
        OfficialSteamBootstrapSupervisor tracker = exitedAt(1_000L, 0);
        tracker.onPrefixSnapshot(2_000L,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY);

        long afterFirstGrace = 2_000L + OfficialSteamBootstrapSupervisor.RESTART_GRACE_MS;
        assertFalse(tracker.onPrefixSnapshot(afterFirstGrace,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
        assertFalse(tracker.onPrefixSnapshot(afterFirstGrace + 1,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY).shouldFinish);

        // A long-running prerequisite or game remains session activity no
        // matter how long ago the Steam client itself was observed.
        assertFalse(tracker.onPrefixSnapshot(Long.MAX_VALUE - 1,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY).shouldFinish);
        assertFalse(tracker.onPrefixSnapshot(Long.MAX_VALUE,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
    }

    @Test
    public void reliableEmptySamplesEndASeenClient() {
        OfficialSteamBootstrapSupervisor tracker = exitedAt(1_000L, 0);
        tracker.onPrefixSnapshot(2_000L,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY);
        long afterGrace = 2_000L + OfficialSteamBootstrapSupervisor.RESTART_GRACE_MS;
        for (int index = 0;
             index < OfficialSteamBootstrapSupervisor.EMPTY_SAMPLES_TO_FINISH - 1;
             index++) {
            assertFalse(tracker.onPrefixSnapshot(afterGrace + index,
                    OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
        }
        assertTrue(tracker.onPrefixSnapshot(
                afterGrace + OfficialSteamBootstrapSupervisor.EMPTY_SAMPLES_TO_FINISH,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
    }

    @Test
    public void unavailableScanNeverClaimsSteamExited() {
        OfficialSteamBootstrapSupervisor tracker = exitedAt(1_000L, 0);
        assertFalse(tracker.onPrefixSnapshot(Long.MAX_VALUE,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.UNAVAILABLE).shouldFinish);
    }

    @Test
    public void cancellationAndOneShotCompletionAreSafe() {
        OfficialSteamBootstrapSupervisor cancelled = exitedAt(1_000L, 0);
        cancelled.cancel();
        assertFalse(cancelled.onPrefixSnapshot(Long.MAX_VALUE,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);

        OfficialSteamBootstrapSupervisor completed = exitedAt(1_000L, 0);
        assertTrue(completed.onPrefixSnapshot(
                1_000L + OfficialSteamBootstrapSupervisor.STARTUP_GRACE_MS,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
        assertFalse(completed.onPrefixSnapshot(Long.MAX_VALUE,
                OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE).shouldFinish);
    }

    @Test
    public void processClassifierExcludesResidentPrefixInfrastructure() {
        assertResident("/usr/bin/wineserver -p0");
        assertResident("wine-preloader c:\\windows\\system32\\explorer.exe /desktop=shell");
        assertResident("wine-preloader c:\\windows\\system32\\services.exe");
        assertResident("wine-preloader c:\\windows\\system32\\winedevice.exe");
        assertResident("wine-preloader c:\\windows\\system32\\conhost.exe");
        assertResident("wine-preloader c:\\windows\\system32\\rpcss.exe");
        assertResident("wine-preloader c:\\program files (x86)\\steam\\steamerrorreporter64.exe");

        // winhandler retains the exited target in argv. The target text must
        // not turn the resident bridge into permanent session activity.
        assertResident(
                "wine-preloader c:\\windows\\system32\\winhandler.exe "
                        + "/dir c:\\steam steam.exe -applaunch 480");
    }

    @Test
    public void processClassifierIncludesSteamPrerequisitesAndUnknownGames() {
        assertActivity("wine-preloader c:\\program files (x86)\\steam\\steam.exe -silent");
        assertActivity("wine-preloader steamwebhelper.exe --type=renderer");
        assertActivity("wine-preloader d:\\steamapps\\common\\steamworks shared\\dxsetup.exe /silent");
        assertActivity("wine-preloader c:\\temp\\vc_redist.x64.exe /quiet");
        assertActivity("wine-preloader c:\\windows\\system32\\msiexec.exe /i prerequisite.msi");
        assertActivity("wine-preloader d:\\steamapps\\common\\example\\totally-unknown-game.exe");
        assertActivity("wine-preloader\0D:\\SteamApps\\Common\\Example\\Game.exe"
                + "\0-applaunch\0" + "480");
    }

    @Test
    public void processClassifierUsesImageBoundariesAndRejectsUnreadableInput() {
        assertActivity("wine-preloader c:\\games\\myservices.exe");
        assertEquals(OfficialSteamBootstrapSupervisor.ProcessKind.UNAVAILABLE,
                OfficialSteamBootstrapSupervisor.classifyPrefixProcess(null));
        assertEquals(OfficialSteamBootstrapSupervisor.ProcessKind.UNAVAILABLE,
                OfficialSteamBootstrapSupervisor.classifyPrefixProcess("\0\0"));
    }

    private static OfficialSteamBootstrapSupervisor exitedAt(long nowMs, int status) {
        OfficialSteamBootstrapSupervisor tracker = new OfficialSteamBootstrapSupervisor();
        tracker.onLauncherExit(status, nowMs);
        return tracker;
    }

    private static void assertResident(String commandLine) {
        assertEquals(OfficialSteamBootstrapSupervisor.ProcessKind.RESIDENT_INFRASTRUCTURE,
                OfficialSteamBootstrapSupervisor.classifyPrefixProcess(commandLine));
    }

    private static void assertActivity(String commandLine) {
        assertEquals(OfficialSteamBootstrapSupervisor.ProcessKind.SESSION_ACTIVITY,
                OfficialSteamBootstrapSupervisor.classifyPrefixProcess(commandLine));
    }
}
