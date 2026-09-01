package com.dualscreen.launcher.switch

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

enum class SwitchContentKind { BASE, UPDATE, DLC, MOD }

data class SwitchPatch(
    val name: String,
    val version: String,
    val kind: SwitchContentKind,
    val enabled: Boolean,
    val removable: Boolean,
)

data class SwitchGameInstallation(
    val gameId: String,
    val baseUri: String,
    val displayName: String,
    val detectedTitle: String,
    val baseVersion: String,
    val iconPath: String? = null,
    val patches: List<SwitchPatch> = emptyList(),
) {
    val installedUpdate: SwitchPatch?
        get() = patches.firstOrNull { it.kind == SwitchContentKind.UPDATE && it.enabled }
    val dlc: List<SwitchPatch>
        get() = patches.filter { it.kind == SwitchContentKind.DLC }
    val mods: List<SwitchPatch>
        get() = patches.filter { it.kind == SwitchContentKind.MOD }
}

data class SwitchOperationState(
    val gameId: String? = null,
    val running: Boolean = false,
    val kind: SwitchContentKind? = null,
    val fraction: Float = 0f,
    val detail: String = "",
    val error: String? = null,
)

enum class EdenSettingType { BOOLEAN, BYTE, SHORT, INTEGER, LONG, STRING }

data class EdenSettingValue(
    val key: String,
    val label: String,
    val category: String,
    val type: EdenSettingType,
    val value: String,
    val defaultValue: String,
    val useGlobal: Boolean,
    val runtimeModifiable: Boolean,
    val edenOrder: Int = Int.MAX_VALUE,
)

data class SwitchGpuDriver(
    val path: String,
    val name: String,
    val version: String,
    val author: String = "",
) {
    val id: String get() = path.ifBlank { SYSTEM_GPU_DRIVER_ID }

    companion object {
        const val SYSTEM_GPU_DRIVER_ID = "system"
        val System = SwitchGpuDriver("", "System GPU driver", "Android / vendor")
    }
}

data class SwitchDriverArtifact(
    val source: String,
    val release: String,
    val name: String,
    val downloadUrl: String,
    val sizeBytes: Long,
    val installed: Boolean = false,
)

data class SwitchDriverRelease(
    val source: String,
    val title: String,
    val tag: String,
    val description: String,
    val publishedAt: String,
    val prerelease: Boolean,
    val latest: Boolean,
    val artifacts: List<SwitchDriverArtifact>,
)

/** Eden settings read directly from the embedded emulator configuration. */
data class SwitchGameSettings(
    val values: List<EdenSettingValue> = emptyList(),
    val selectedGpuDriverPath: String = "",
)

data class SwitchSystemState(
    val initialized: Boolean = false,
    val keysPresent: Boolean = false,
    val firmwareVersion: String? = null,
    val gpuDrivers: List<SwitchGpuDriver> = emptyList(),
)

internal class SwitchInstallRegistry(context: Context) {
    private val preferences = context.getSharedPreferences("switch_games", Context.MODE_PRIVATE)

    fun all(): Map<String, SwitchGameInstallation> = SwitchCatalog.games.mapNotNull { game ->
        get(game.id)?.let { game.id to it }
    }.toMap()

    fun get(gameId: String): SwitchGameInstallation? {
        val raw = preferences.getString(gameId, null) ?: return null
        return runCatching {
            val json = JSONObject(raw)
            val patches = buildList {
                val array = json.optJSONArray("patches") ?: JSONArray()
                repeat(array.length()) { index ->
                    val patch = array.getJSONObject(index)
                    add(
                        SwitchPatch(
                            name = patch.getString("name"),
                            version = patch.optString("version"),
                            kind = SwitchContentKind.valueOf(patch.getString("kind")),
                            enabled = patch.optBoolean("enabled", true),
                            removable = patch.optBoolean("removable", true),
                        ),
                    )
                }
            }
            SwitchGameInstallation(
                gameId = gameId,
                baseUri = json.getString("baseUri"),
                displayName = json.optString("displayName"),
                detectedTitle = json.optString("detectedTitle"),
                baseVersion = json.optString("baseVersion"),
                iconPath = json.optString("iconPath").takeIf(String::isNotBlank),
                patches = patches,
            )
        }.getOrNull()
    }

    fun put(value: SwitchGameInstallation) {
        val patches = JSONArray()
        value.patches.forEach { patch ->
            patches.put(
                JSONObject()
                    .put("name", patch.name)
                    .put("version", patch.version)
                    .put("kind", patch.kind.name)
                    .put("enabled", patch.enabled)
                    .put("removable", patch.removable),
            )
        }
        val json = JSONObject()
            .put("baseUri", value.baseUri)
            .put("displayName", value.displayName)
            .put("detectedTitle", value.detectedTitle)
            .put("baseVersion", value.baseVersion)
            .put("iconPath", value.iconPath.orEmpty())
            .put("patches", patches)
        check(preferences.edit().putString(value.gameId, json.toString()).commit()) {
            "Could not persist Switch game state"
        }
    }
}
