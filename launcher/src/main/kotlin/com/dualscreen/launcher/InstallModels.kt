package com.dualscreen.launcher

import android.content.Context
import org.json.JSONObject
import kotlin.math.roundToInt

enum class InstallStage(val label: String, val baseProgress: Float) {
    IDLE("Waiting for game files", 0f),
    RUNTIME("Preparing the game runtime", 0.03f),
    COMPONENTS("Installing tested compatibility components", 0.10f),
    CONTAINER("Creating the predefined container", 0.17f),
    VALIDATING("Validating archive", 0.24f),
    EXTRACTING("Extracting game files", 0.30f),
    PATCHING("Applying dual-screen mod", 0.82f),
    CONFIGURING("Configuring launch", 0.90f),
    VERIFYING("Verifying installation", 0.96f),
    COMPLETE("Ready to play", 1f),
    FAILED("Installation failed", 0f),
}

data class InstallProgress(
    val gameId: String,
    val stage: InstallStage = InstallStage.IDLE,
    val fraction: Float = stage.baseProgress,
    val detail: String = stage.label,
    val log: List<String> = emptyList(),
    val running: Boolean = false,
    val error: String? = null,
    val updateSequence: Long = 0L,
)

internal fun latestInstallProgress(states: Collection<InstallProgress>): InstallProgress? =
    states.filter(InstallProgress::running).maxByOrNull(InstallProgress::updateSequence)
        ?: states.maxByOrNull(InstallProgress::updateSequence)

internal fun monotonicInstallFraction(previous: InstallProgress, requested: Float): Float =
    (if (previous.running) maxOf(previous.fraction, requested) else requested).coerceIn(0f, 1f)

internal fun installPercent(fraction: Float): Int = (fraction.coerceIn(0f, 1f) * 100f).roundToInt()

internal fun formatInstallLogLine(fraction: Float, detail: String): String =
    "[OVERALL ${installPercent(fraction).toString().padStart(3)}%] $detail"

data class InstalledGame(
    val gameId: String,
    val containerId: Int,
    val shortcutPath: String,
    val hostDirectory: String,
    val installedBytes: Long,
    val payloadVersion: String,
    val untestedBuild: Boolean = false,
    val compatibilityWarning: String? = null,
    val skippedPayloadTargets: Set<String> = emptySet(),
)

class InstallRegistry(context: Context) {
    private val preferences = context.getSharedPreferences("installed_games", Context.MODE_PRIVATE)

    fun get(gameId: String): InstalledGame? {
        val raw = preferences.getString(gameId, null) ?: return null
        return runCatching {
            val data = JSONObject(raw)
            InstalledGame(
                gameId = gameId,
                containerId = data.getInt("containerId"),
                shortcutPath = data.getString("shortcutPath"),
                hostDirectory = data.getString("hostDirectory"),
                installedBytes = data.optLong("installedBytes"),
                payloadVersion = data.optString("payloadVersion"),
                untestedBuild = data.optBoolean("untestedBuild", false),
                compatibilityWarning = data.optString("compatibilityWarning")
                    .takeIf(String::isNotBlank),
                skippedPayloadTargets = buildSet {
                    val values = data.optJSONArray("skippedPayloadTargets")
                    for (index in 0 until (values?.length() ?: 0)) {
                        add(checkNotNull(values).getString(index))
                    }
                },
            )
        }.getOrNull()
    }

    fun all(): Map<String, InstalledGame> = GameCatalog.games.mapNotNull { game ->
        get(game.id)?.let { game.id to it }
    }.toMap()

    fun put(value: InstalledGame) {
        val data = JSONObject()
            .put("containerId", value.containerId)
            .put("shortcutPath", value.shortcutPath)
            .put("hostDirectory", value.hostDirectory)
            .put("installedBytes", value.installedBytes)
            .put("payloadVersion", value.payloadVersion)
            .put("untestedBuild", value.untestedBuild)
            .put("compatibilityWarning", value.compatibilityWarning ?: "")
            .put(
                "skippedPayloadTargets",
                org.json.JSONArray(value.skippedPayloadTargets.sorted()),
            )
        check(preferences.edit().putString(value.gameId, data.toString()).commit()) {
            "Could not persist the installed game state"
        }
    }
}
