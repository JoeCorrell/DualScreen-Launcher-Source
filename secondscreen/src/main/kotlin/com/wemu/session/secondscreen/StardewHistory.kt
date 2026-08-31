package com.wemu.session.secondscreen

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class StardewDayRecord(
    val label: String,
    val gold: Int,
    val shipped: Int,
)

object StardewHistory {

    private const val FILE = "stardew_history"
    private const val KEEP = 14

    private val _days = MutableStateFlow<List<StardewDayRecord>>(emptyList())
    val days: StateFlow<List<StardewDayRecord>> = _days.asStateFlow()

    private var prefs: android.content.SharedPreferences? = null
    private var save: String = ""
    private var lastKey: String = ""

    fun load(context: Context) {
        if (prefs != null) return
        prefs = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        read()
    }

    fun useSave(name: String) {
        val cleaned = name.filter { it.isLetterOrDigit() || it == ' ' }.trim()
        if (cleaned.isEmpty() || cleaned == save) return
        save = cleaned
        lastKey = ""
        read()
    }

    fun observe(state: StardewState) {
        if (!state.hasWorld) return
        val key = "${state.year}-${state.season}-${state.day}"
        if (key == lastKey) return

        lastKey = key

        val label = "${state.season.take(2)} ${state.day}"
        val existing = _days.value.lastOrNull()
        _days.value = if (existing?.label == label) {
            _days.value
        } else {
            (_days.value + StardewDayRecord(label, state.gold, 0)).takeLast(KEEP)
        }
        write()
    }

    fun record(state: StardewState) {
        if (!state.hasWorld) return
        val last = _days.value.lastOrNull() ?: return
        val updated = last.copy(shipped = state.shipping.total)
        if (updated == last) return
        _days.value = _days.value.dropLast(1) + updated
        write()
    }

    private fun key(): String = if (save.isEmpty()) "days" else "$save/days"

    private fun read() {
        val stored = prefs?.getString(key(), "").orEmpty()
        _days.value = stored.split('|')
            .filter { it.isNotBlank() }
            .mapNotNull { row ->
                val parts = row.split(';')
                if (parts.size < 3) return@mapNotNull null
                StardewDayRecord(
                    label = parts[0],
                    gold = parts[1].toIntOrNull() ?: 0,
                    shipped = parts[2].toIntOrNull() ?: 0,
                )
            }
            .takeLast(KEEP)
    }

    private fun write() {
        prefs?.edit()?.putString(
            key(),
            _days.value.joinToString("|") { "${it.label};${it.gold};${it.shipped}" },
        )?.apply()
    }
}
