package com.wemu.engine.core;

import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.Toast;

import com.wemu.engine.R;

import java.lang.ref.WeakReference;

/**
 * Odds and ends the engine needs from the platform.
 *
 * Trimmed hard. The original's version carried twenty-six methods, nineteen of which
 * existed to drive its own interface — filling spinners, positioning popup
 * windows, wiring a `TabLayout`, walking a `ViewGroup` looking for children of a
 * given class. Those describe screens this fork does not have, and one of them
 * was the last thing in the engine that referred to the VR activity.
 *
 * What remains is what code below the interface actually calls.
 */
public abstract class AppUtils {

    private static WeakReference<Toast> globalToastReference = null;

    public static void keepScreenOn(Activity activity) {
        activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    /**
     * The guest architecture name, as the container's scripts spell it.
     *
     * Only arm64 is shipped — see the ABI filter in `:engine`'s build — but the
     * mapping is kept whole because it is what the unpacked filesystem's paths
     * are named after, and hard-coding "arm64" here would hide the reason.
     */
    public static String getArchName() {
        for (String arch : Build.SUPPORTED_ABIS) {
            switch (arch) {
                case "arm64-v8a": return "arm64";
                case "armeabi-v7a": return "armhf";
                case "x86_64": return "x86_64";
                case "x86": return "x86";
            }
        }
        return "armhf";
    }

    public static void hideSystemUI(final Activity activity) {
        Window window = activity.getWindow();
        final View decorView = window.getDecorView();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            final WindowInsetsController insetsController = decorView.getWindowInsetsController();
            if (insetsController != null) {
                insetsController.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                insetsController.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            final int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;

            decorView.setSystemUiVisibility(flags);
            decorView.setOnSystemUiVisibilityChangeListener((visibility) -> {
                if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                    decorView.setSystemUiVisibility(flags);
                }
            });
        }
    }

    public static boolean isUiThread() {
        return Looper.getMainLooper().getThread() == Thread.currentThread();
    }

    public static int getScreenWidth() {
        return Resources.getSystem().getDisplayMetrics().widthPixels;
    }

    public static int getScreenHeight() {
        return Resources.getSystem().getDisplayMetrics().heightPixels;
    }

    public static Toast showToast(Context context, int textResId) {
        return showToast(context, context.getString(textResId));
    }

    public static Toast showToast(final Context context, final String text) {
        if (!isUiThread()) {
            if (context instanceof Activity) {
                ((Activity) context).runOnUiThread(() -> showToast(context, text));
            }
            return null;
        }

        if (globalToastReference != null) {
            Toast toast = globalToastReference.get();
            if (toast != null) toast.cancel();
            globalToastReference = null;
        }

        View view = LayoutInflater.from(context).inflate(R.layout.custom_toast, null);
        ((TextView) view.findViewById(R.id.TextView)).setText(text);

        Toast toast = new Toast(context);
        toast.setGravity(Gravity.CENTER | Gravity.BOTTOM, 0, 50);
        toast.setDuration(text.length() >= 40 ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT);
        toast.setView(view);
        toast.show();
        globalToastReference = new WeakReference<>(toast);
        return toast;
    }

    public static int getVersionCode(Context context) {
        try {
            PackageInfo pInfo = context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0);
            return pInfo.versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            return 0;
        }
    }

    /** Where the packaged .so files were unpacked to on disk. */
    public static String getNativeLibDir(Context context) {
        return context.getApplicationInfo().nativeLibraryDir;
    }

    /**
     * Runs something after a delay, on the main thread.
     *
     * The original started a fresh `java.util.Timer` per call — a thread each
     * time, never cancelled, and firing on that thread rather than the main one,
     * so anything touching a view from the callback was a race. A `Handler` on
     * the main looper is what every caller here actually wanted.
     */
    public static void runDelayed(Runnable callback, long delay) {
        if (callback == null) return;
        new Handler(Looper.getMainLooper()).postDelayed(callback, delay);
    }
}
