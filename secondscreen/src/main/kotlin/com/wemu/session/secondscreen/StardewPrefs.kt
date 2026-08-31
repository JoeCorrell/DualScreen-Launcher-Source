package com.wemu.session.secondscreen

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class StardewPrefsState(

    val showGround: Boolean = true,

    val groundShade: Int = 30,

    val gameFrames: Boolean = true,

    val fillPages: Boolean = true,

    val groundChoice: String = "",

    val groundZoom: Int = DEFAULT_GROUND_ZOOM,

    val groundRed: Int = 46,
    val groundGreen: Int = 84,
    val groundBlue: Int = 58,

    val host: String = "",
    val alerts: Boolean = true,
    val machineAlerts: Boolean = true,
    val birthdayAlerts: Boolean = true,
    val lateNightAlerts: Boolean = true,
    val lateNightAlertTime: Int = DEFAULT_LATE_NIGHT_ALERT_TIME,
    val hiddenPages: Set<String> = emptySet(),
) {
    fun hides(page: String): Boolean = page in hiddenPages
}

object StardewPrefs {

    private val _state = MutableStateFlow(StardewPrefsState())
    val state: StateFlow<StardewPrefsState> = _state.asStateFlow()

    private var prefs: android.content.SharedPreferences? = null

    private var save: String = ""

    fun load(context: Context) {
        if (prefs != null) return
        prefs = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        read()
    }

    fun useSave(name: String) {
        val cleaned = name.filter { it.isLetterOrDigit() || it == ' ' }.trim()
        if (cleaned.isEmpty() || cleaned == save) return
        save = cleaned
        read()
    }

    private fun key(name: String): String = if (save.isEmpty()) name else "$save/$name"

    private fun read() {
        val store = prefs ?: return

        fun bool(name: String, fallback: Boolean) =
            store.getBoolean(key(name), store.getBoolean(name, fallback))

        fun int(name: String, fallback: Int) =
            store.getInt(key(name), store.getInt(name, fallback))

        fun text(name: String) =
            store.getString(key(name), store.getString(name, "")).orEmpty()

        _state.value = StardewPrefsState(
            showGround = bool(SHOW_GROUND, true),
            groundShade = int(GROUND_SHADE, 30).coerceIn(0, 70),
            groundChoice = text(GROUND_CHOICE),
            groundZoom = readGroundZoom(store),
            groundRed = int(GROUND_RED, 46).coerceIn(0, 255),
            groundGreen = int(GROUND_GREEN, 84).coerceIn(0, 255),
            groundBlue = int(GROUND_BLUE, 58).coerceIn(0, 255),
            gameFrames = bool(GAME_FRAMES, true),
            host = store.getString(HOST, "").orEmpty(),
            alerts = bool(ALERTS, true),
            machineAlerts = bool(MACHINE_ALERTS, true),
            birthdayAlerts = bool(BIRTHDAY_ALERTS, true),
            lateNightAlerts = bool(LATE_NIGHT_ALERTS, true),
            lateNightAlertTime = normalizeLateNightAlertTime(
                int(LATE_NIGHT_ALERT_TIME, DEFAULT_LATE_NIGHT_ALERT_TIME),
            ),
            fillPages = bool(FILL_PAGES, true),
            hiddenPages = store.getStringSet(key(HIDDEN_PAGES), null)
                ?: store.getStringSet(HIDDEN_PAGES, emptySet()).orEmpty(),
        )
    }

    fun update(change: (StardewPrefsState) -> StardewPrefsState) {
        val changed = change(_state.value)
        val next = changed.copy(
            groundShade = changed.groundShade.coerceIn(0, 70),
            groundZoom = normalizeGroundZoom(changed.groundZoom),
            groundRed = changed.groundRed.coerceIn(0, 255),
            groundGreen = changed.groundGreen.coerceIn(0, 255),
            groundBlue = changed.groundBlue.coerceIn(0, 255),
            lateNightAlertTime = normalizeLateNightAlertTime(changed.lateNightAlertTime),
        )
        _state.value = next
        prefs?.edit()?.apply {
            putBoolean(key(SHOW_GROUND), next.showGround)
            putInt(key(GROUND_SHADE), next.groundShade)
            putString(key(GROUND_CHOICE), next.groundChoice)
            putInt(key(GROUND_ZOOM), normalizeGroundZoom(next.groundZoom))
            putInt(key(GROUND_ZOOM_VERSION), CURRENT_GROUND_ZOOM_VERSION)
            putInt(key(GROUND_RED), next.groundRed)
            putInt(key(GROUND_GREEN), next.groundGreen)
            putInt(key(GROUND_BLUE), next.groundBlue)
            putBoolean(key(GAME_FRAMES), next.gameFrames)
            putBoolean(key(ALERTS), next.alerts)
            putBoolean(key(MACHINE_ALERTS), next.machineAlerts)
            putBoolean(key(BIRTHDAY_ALERTS), next.birthdayAlerts)
            putBoolean(key(LATE_NIGHT_ALERTS), next.lateNightAlerts)
            putInt(
                key(LATE_NIGHT_ALERT_TIME),
                normalizeLateNightAlertTime(next.lateNightAlertTime),
            )
            putBoolean(key(FILL_PAGES), next.fillPages)
            putStringSet(key(HIDDEN_PAGES), HashSet(next.hiddenPages))

            putString(HOST, next.host)
            apply()
        }
    }

    fun reset() = update { StardewPrefsState() }

    /**
     * Before the background picker was redesigned, its default magnified every 16px
     * game tile three times on top of display density. Migrate each scoped value once
     * so existing installs get the smaller pattern instead of keeping the old,
     * oversized appearance forever.
     */
    private fun readGroundZoom(store: android.content.SharedPreferences): Int {
        val scopedZoom = key(GROUND_ZOOM)
        if (!store.contains(scopedZoom)) {
            return normalizeGroundZoom(
                store.getInt(GROUND_ZOOM, DEFAULT_GROUND_ZOOM),
            )
        }

        val raw = store.getInt(scopedZoom, DEFAULT_GROUND_ZOOM)
        val version = store.getInt(key(GROUND_ZOOM_VERSION), 0)
        if (version >= CURRENT_GROUND_ZOOM_VERSION) return normalizeGroundZoom(raw)

        val migrated = migrateLegacyGroundZoom(raw)
        store.edit()
            .putInt(scopedZoom, migrated)
            .putInt(key(GROUND_ZOOM_VERSION), CURRENT_GROUND_ZOOM_VERSION)
            .apply()
        return migrated
    }

    private const val FILE = "stardew_second_screen"
    private const val SHOW_GROUND = "show_ground"
    private const val GROUND_SHADE = "ground_shade"
    private const val GROUND_CHOICE = "ground_choice"
    private const val GROUND_ZOOM = "ground_zoom"
    private const val GROUND_ZOOM_VERSION = "ground_zoom_version"
    private const val GROUND_RED = "ground_red"
    private const val GROUND_GREEN = "ground_green"
    private const val GROUND_BLUE = "ground_blue"

    const val COLOUR = "colour"
    private const val GAME_FRAMES = "game_frames"
    private const val HOST = "host"
    private const val ALERTS = "alerts"
    private const val MACHINE_ALERTS = "machine_alerts"
    private const val BIRTHDAY_ALERTS = "birthday_alerts"
    private const val LATE_NIGHT_ALERTS = "late_night_alerts"
    private const val LATE_NIGHT_ALERT_TIME = "late_night_alert_time"
    private const val FILL_PAGES = "fill_pages"
    private const val HIDDEN_PAGES = "hidden_pages"
}

internal const val DEFAULT_GROUND_ZOOM = 1
internal const val DEFAULT_LATE_NIGHT_ALERT_TIME = 2400
private const val CURRENT_GROUND_ZOOM_VERSION = 2

internal fun normalizeGroundZoom(value: Int): Int = value.coerceIn(1, 4)

internal fun migrateLegacyGroundZoom(value: Int): Int =
    ((value.coerceIn(1, 8) + 2) / 3).coerceIn(1, 4)

internal fun normalizeLateNightAlertTime(value: Int): Int =
    ((value.coerceIn(2200, 2500) / 100) * 100).coerceIn(2200, 2500)
