package com.dualscreen.launcher

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class LauncherUpdateReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val app = context.applicationContext as? DualScreenLauncherApplication ?: return
        app.updates.handleInstallStatus(intent)
    }
}
