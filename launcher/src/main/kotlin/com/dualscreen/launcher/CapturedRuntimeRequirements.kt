package com.dualscreen.launcher

import org.json.JSONObject

/** A full preset owns fixed assets; a per-game translator only owns its component package. */
internal class CapturedRuntimeRequirements private constructor(
    val componentIds: Set<String>,
    val fixedAssets: FixedAssets?,
) {
    data class FixedAssets(val box64Archive: String, val rendererDriverId: String)

    companion object {
        fun directX(config: String): CapturedRuntimeRequirements =
            CapturedRuntimeRequirements(componentIds(JSONObject().put("dxwrapperConfig", config)), null)

        fun preset(profile: JSONObject): CapturedRuntimeRequirements {
            val box64 = profile.optString("box64Version")
            check(box64.isNotBlank()) { "The captured preset has no Box64 version" }
            val driver = profile.optString("rendererDriverId")
            check(driver.isNotBlank()) { "The captured preset has no renderer driver" }
            return CapturedRuntimeRequirements(
                componentIds(profile),
                FixedAssets("box64/box64-$box64.tzst", driver),
            )
        }

        fun fexCore(version: String): CapturedRuntimeRequirements {
            check(version.startsWith("FEXCore-", ignoreCase = true) && looksLikeComponentId(version)) {
                "The per-game FEXCore override must name an exact component package"
            }
            return CapturedRuntimeRequirements(
                componentIds(JSONObject().put("fexcoreVersion", version)),
                fixedAssets = null,
            )
        }

        private fun componentIds(data: JSONObject): Set<String> {
            val candidates = linkedSetOf<String>()
            listOf("wineVersion", "box64Version").forEach { key ->
                data.optString(key).takeIf(::looksLikeComponentId)?.let(candidates::add)
            }
            data.optString("fexcoreVersion")
                .takeIf(::looksLikeComponentId)
                ?.let { value ->
                    candidates += if (value.startsWith("FEXCore-FEXCore-", ignoreCase = true)) {
                        value
                    } else {
                        "FEXCore-$value"
                    }
                }
            listOf(data.optString("dxwrapperConfig"), data.optString("graphicsDriverConfig"))
                .flatMap { config -> config.split(',', ';') }
                .mapNotNull { token -> token.substringAfter('=', "").takeIf(::looksLikeComponentId) }
                .forEach(candidates::add)
            return candidates
        }

        private fun looksLikeComponentId(value: String): Boolean =
            listOf("Proton-", "Wine-", "DXVK-", "VKD3D-", "Box64-", "WOWBox64-", "FEXCore-")
                .any { value.startsWith(it, ignoreCase = true) } && value.lastOrNull()?.isDigit() == true
    }
}
