package com.dualscreen.launcher.session

import android.app.Activity
import android.app.ActivityManager
import android.app.ActivityOptions
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.View
import android.view.WindowManager
import java.lang.ref.WeakReference
import java.util.UUID

/**
 * An opaque activity beneath AC II's non-focusable Presentation. A Presentation
 * alone does not stop the secondary home activity's animations. This activity
 * owns that display's task coverage without taking game input or drawing frames.
 */
class CompanionDisplayCoverActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE)
        @Suppress("DEPRECATION")
        val actualDisplay = windowManager.defaultDisplay.displayId
        if (!CompanionDisplayCover.attach(this, intent.getStringExtra(TOKEN), actualDisplay)) {
            finishAndRemoveTask()
            return
        }
        window.setBackgroundDrawableResource(android.R.color.black)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        setContentView(View(this).apply { setBackgroundColor(Color.BLACK) })
        Log.i(TAG, "Opaque companion cover attached display=$actualDisplay; input focus refused")
    }

    override fun onDestroy() {
        CompanionDisplayCover.detach(this)
        super.onDestroy()
    }

    companion object {
        internal const val TOKEN = "companion.cover.token"
        internal const val TAG = "CompanionDisplayCover"
    }
}

/** Main-thread-only lease. Delayed launches cannot survive session exit/display removal. */
internal object CompanionDisplayCover {
    private var owner = WeakReference<Activity>(null)
    private var cover = WeakReference<CompanionDisplayCoverActivity>(null)
    private var displayId: Int? = null
    private var token: String? = null

    fun update(activity: Activity, requestedDisplay: Int?) {
        if (requestedDisplay == null) {
            if (owner.get() === activity) release()
            return
        }
        if (owner.get() === activity && displayId == requestedDisplay) return
        release()
        if (!activity.packageManager.hasSystemFeature(PackageManager.FEATURE_ACTIVITIES_ON_SECONDARY_DISPLAYS)) return
        val launchToken = UUID.randomUUID().toString()
        val intent = Intent(activity, CompanionDisplayCoverActivity::class.java)
            .putExtra(CompanionDisplayCoverActivity.TOKEN, launchToken)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_ANIMATION)
        if (android.os.Build.VERSION.SDK_INT >= 29 &&
            activity.getSystemService(ActivityManager::class.java)
                ?.isActivityStartAllowedOnDisplay(activity, requestedDisplay, intent) != true) return
        owner = WeakReference(activity)
        displayId = requestedDisplay
        token = launchToken
        try {
            activity.startActivity(intent, ActivityOptions.makeBasic().setLaunchDisplayId(requestedDisplay).toBundle())
        } catch (error: RuntimeException) {
            Log.w(CompanionDisplayCoverActivity.TAG, "Cover unavailable; retaining existing companion output", error)
            release()
        }
    }

    fun attach(activity: CompanionDisplayCoverActivity, launchToken: String?, actualDisplay: Int): Boolean {
        val host = owner.get()
        if (host == null || host.isFinishing || host.isDestroyed || token == null ||
            token != launchToken || displayId != actualDisplay) return false
        cover.get()?.takeIf { it !== activity }?.finishAndRemoveTask()
        cover = WeakReference(activity)
        return true
    }

    fun detach(activity: CompanionDisplayCoverActivity) {
        if (cover.get() === activity) cover.clear()
    }

    private fun release() {
        token = null
        displayId = null
        owner.clear()
        val previous = cover.get()
        cover.clear()
        previous?.finishAndRemoveTask()
    }
}
