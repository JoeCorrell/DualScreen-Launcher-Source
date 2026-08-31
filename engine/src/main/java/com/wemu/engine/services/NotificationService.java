package com.wemu.engine.services;

import android.annotation.SuppressLint;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import com.wemu.engine.R;

/**
 * Keeps the process alive while a container is running.
 *
 * A Wine session is a tree of native processes the platform knows nothing about,
 * so without a foreground service the system is free to reap the lot the moment
 * the activity stops — which, on a two-screen device, happens as soon as an app
 * covers the panel the session was launched from.
 *
 * The channel id, notification id and the activity this notification opens all
 * used to be read off `MainActivity`, so the service could not exist without
 * that specific screen. They are its own now, and the target is resolved from
 * the package manager: whatever the launcher activity is, that is where tapping
 * the notification goes.
 */
public class NotificationService extends Service {

    private static final String TAG = "SessionService";
    public static final String NOTIFICATION_CHANNEL_ID = "com.wemu.engine.session";
    public static final int NOTIFICATION_ID = 1;

    private static volatile boolean isRunning = false;
    private PowerManager.WakeLock wakeLock;

    /**
     * Requests session protection before any native guest process is launched.
     *
     * Kept here instead of in an Activity so every {@code RuntimeSession}
     * receives the same lifetime policy, including sessions started by a new
     * host in the future.
     */
    public static void startForSession(Context context) {
        Context appContext = context.getApplicationContext();
        Intent intent = new Intent(appContext, NotificationService.class);
        ContextCompat.startForegroundService(appContext, intent);
    }

    /** Releases protection after the complete guest environment has stopped. */
    public static void stopForSession(Context context) {
        Context appContext = context.getApplicationContext();
        appContext.stopService(new Intent(appContext, NotificationService.class));
    }

    public static boolean isRunning() {
        return isRunning;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createChannel();
    }

    /**
     * Registers the channel this notification posts on.
     *
     * Done here rather than by the app on startup. A channel that does not exist
     * makes `startForeground` throw on API 26 and above, and the failure is a
     * crash in the service rather than a missing notification — which is a hard
     * thing to attribute when it only reproduces on a cold start.
     */
    private void createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager == null || manager.getNotificationChannel(NOTIFICATION_CHANNEL_ID) != null) return;

        NotificationChannel channel = new NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                "Running session",
                NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Shown while a Windows container is running.");
        channel.setShowBadge(false);
        manager.createNotificationChannel(channel);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // Whatever the app's launcher activity is. The service does not need to
        // know its name, and asking the package manager means it stays correct
        // if the entry point is ever renamed or replaced.
        Intent launchIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent pendingIntent = launchIntent == null ? null : PendingIntent.getActivity(
                this, 0, launchIntent, PendingIntent.FLAG_IMMUTABLE);

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_stat_ab_gear_0011)
                .setContentTitle("Session running")
                .setContentText("Keeping the container alive. Dismissing this ends the session.")
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
                .setOngoing(true);
        if (pendingIntent != null) builder.setContentIntent(pendingIntent);

        Notification notification = builder.build();
        startForeground(NOTIFICATION_ID, notification);

        isRunning = true;
        acquireWakeLock();

        return START_NOT_STICKY;
    }

    /**
     * A foreground service protects process importance, but does not keep the
     * CPU awake after the displays turn off. Steam installs and prerequisite
     * work must be allowed to finish in that state, so the lock deliberately
     * follows the explicit session lifetime rather than using an arbitrary
     * timeout.
     */
    @SuppressLint("WakelockTimeout")
    private void acquireWakeLock() {
        if (wakeLock == null) {
            PowerManager powerManager = (PowerManager) getSystemService(POWER_SERVICE);
            if (powerManager == null) {
                Log.w(TAG, "PowerManager is unavailable; the session may pause with the display off");
                return;
            }
            wakeLock = powerManager.newWakeLock(
                    PowerManager.PARTIAL_WAKE_LOCK,
                    "Wemu::RunningSession");
            wakeLock.setReferenceCounted(false);
        }
        if (!wakeLock.isHeld()) wakeLock.acquire();
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
        isRunning = false;
        releaseWakeLock();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        stopForeground(STOP_FOREGROUND_REMOVE);
        isRunning = false;
        releaseWakeLock();
    }

    private void releaseWakeLock() {
        // Cleared as well as released so a later start creates a fresh lock
        // owned by that service instance.
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        wakeLock = null;
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
