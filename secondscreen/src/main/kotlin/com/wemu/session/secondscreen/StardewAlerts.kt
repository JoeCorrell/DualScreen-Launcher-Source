package com.wemu.session.secondscreen

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.delay

@Composable
fun StardewAlerts() {
    val state by SecondScreen.stardew.collectAsStateWithLifecycle()
    val prefs by StardewPrefs.state.collectAsStateWithLifecycle()
    val context = LocalContext.current

    var lastMachines by remember { mutableIntStateOf(-1) }
    var warnedLate by remember { mutableStateOf(false) }
    var warnedBirthday by remember { mutableStateOf("") }

    LaunchedEffect(
        state.machinesReady,
        state.timeOfDay,
        state.birthdayToday?.name,
        prefs.alerts,
        prefs.machineAlerts,
        prefs.birthdayAlerts,
        prefs.lateNightAlerts,
        prefs.lateNightAlertTime,
    ) {
        delay(SETTLE)

        val ready = state.machinesReady
        if (prefs.alerts && prefs.machineAlerts && lastMachines >= 0 && ready > lastMachines) {
            Buzz.short(context)
        }
        lastMachines = ready

        if (
            prefs.alerts && prefs.lateNightAlerts &&
            state.timeOfDay >= prefs.lateNightAlertTime && !warnedLate
        ) {
            warnedLate = true
            Buzz.long(context)
        }
        if (state.timeOfDay < prefs.lateNightAlertTime) warnedLate = false

        val birthday = state.birthdayToday?.name.orEmpty()
        if (
            prefs.alerts && prefs.birthdayAlerts &&
            birthday.isNotEmpty() && birthday != warnedBirthday
        ) {
            warnedBirthday = birthday
            Buzz.short(context)
        }
        if (birthday.isEmpty()) warnedBirthday = ""
    }
}

private const val SETTLE = 400L
