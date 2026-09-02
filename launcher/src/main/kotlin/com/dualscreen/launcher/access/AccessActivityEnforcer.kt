package com.dualscreen.launcher.access

import android.app.Activity
import android.app.Application
import android.os.Bundle
import android.widget.Toast
import com.dualscreen.launcher.LauncherActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Collections
import java.util.WeakHashMap

/** Revalidates while any app activity is in use, including the embedded emulator. */
class AccessActivityEnforcer(
    private val application: Application,
    private val access: AccessManager,
) : Application.ActivityLifecycleCallbacks {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val resumed = Collections.newSetFromMap(WeakHashMap<Activity, Boolean>())
    private var periodicJob: Job? = null

    override fun onActivityResumed(activity: Activity) {
        resumed += activity
        if (activity !is LauncherActivity || access.state.value is AccessState.Granted) {
            scope.launch {
                val state = access.verifyIfStale(ENTRY_RECHECK_MAX_AGE_MS)
                enforce(state)
            }
        }
        if (periodicJob?.isActive != true) {
            periodicJob = scope.launch {
                while (resumed.isNotEmpty()) {
                    delay(PERIODIC_RECHECK_MS)
                    if (resumed.isNotEmpty()) enforce(access.verifyNow(showChecking = false))
                }
            }
        }
    }

    override fun onActivityPaused(activity: Activity) {
        resumed -= activity
        if (resumed.isEmpty()) {
            periodicJob?.cancel()
            periodicJob = null
        }
    }

    private fun enforce(state: AccessState) {
        if (state is AccessState.Granted || state is AccessState.Checking) return
        resumed.toList().forEach { activity ->
            if (activity !is LauncherActivity && !activity.isFinishing) {
                Toast.makeText(application, state.message, Toast.LENGTH_LONG).show()
                activity.finish()
            }
        }
    }

    override fun onActivityCreated(activity: Activity, state: Bundle?) = Unit
    override fun onActivityStarted(activity: Activity) = Unit
    override fun onActivityStopped(activity: Activity) = Unit
    override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
    override fun onActivityDestroyed(activity: Activity) {
        resumed -= activity
    }

    fun close() = scope.cancel()

    private companion object {
        const val ENTRY_RECHECK_MAX_AGE_MS = 60_000L
        const val PERIODIC_RECHECK_MS = 15 * 60_000L
    }
}
