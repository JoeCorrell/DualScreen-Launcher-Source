package com.dualscreen.launcher

import com.wemu.engine.core.WineRegistryEditor
import java.io.File

/**
 * Keeps WFC's UE3 viewport on the Thor's upper monitor.
 *
 * WFC's native PCSettingManager does not use TransEngine.ini as its
 * authoritative display store. The decompiled game loads the REG_SZ value
 * HKCU\Software\Activision\TransformersWFC\ScreenResolution using the exact
 * format "Width:%d Height:%d Fullscreen:%d". Keep that native value, the UE3
 * fallback profile, and the launch arguments synchronized so the game creates
 * a real 1280x720 client instead of reopening its old 640x480 framebuffer.
 */
internal object WfcDisplayConfiguration {
    private const val REQUIRED_WIDTH = 1280
    private const val REQUIRED_HEIGHT = 720
    private const val PROFILE =
        ".wine/drive_c/users/xuser/Documents/My Games/Transformers/TransGame/Config/TransEngine.ini"
    private const val REGISTRY_KEY = "Software\\Activision\\TransformersWFC"
    private const val REGISTRY_VALUE = "ScreenResolution"

    data class ApplyResult(
        val profile: File,
        val userRegistry: File,
        val nativeResolution: String,
        val changed: Boolean,
    )

    fun ensure(containerRoot: File, resolution: GameResolution): ApplyResult {
        requireNativeResolution(resolution)
        require(containerRoot.isDirectory) { "The container root is unavailable" }
        val userRegistry = File(containerRoot, ".wine/user.reg")
        check(userRegistry.isFile) { "The Wine user registry is unavailable" }
        val nativeResolution = nativeResolution(resolution)
        val previousResolution = WineRegistryEditor(userRegistry).use { registry ->
            registry.getStringValue(REGISTRY_KEY, REGISTRY_VALUE)
        }
        WineRegistryEditor(userRegistry).use { registry ->
            registry.setStringValue(REGISTRY_KEY, REGISTRY_VALUE, nativeResolution)
        }
        WineRegistryEditor(userRegistry).use { registry ->
            check(registry.getStringValue(REGISTRY_KEY, REGISTRY_VALUE) == nativeResolution) {
                "WFC's native 1280x720 resolution value could not be verified"
            }
        }

        val profile = File(containerRoot, PROFILE)
        check(profile.parentFile?.let { it.isDirectory || it.mkdirs() } == true) {
            "Could not create WFC's display profile directory"
        }

        val original = profile.takeIf(File::isFile)?.readText().orEmpty()
        var updated = setIniValue(original, "SystemSettings", "ResX", resolution.width.toString())
        updated = setIniValue(updated, "SystemSettings", "ResY", resolution.height.toString())
        updated = setIniValue(updated, "SystemSettings", "Fullscreen", "False")
        updated = setIniValue(updated, "WinDrv.WindowsClient", "StartupFullscreen", "False")
        val profileChanged = updated != original
        if (profileChanged) profile.writeText(updated)
        return ApplyResult(
            profile = profile,
            userRegistry = userRegistry,
            nativeResolution = nativeResolution,
            changed = profileChanged || previousResolution != nativeResolution,
        )
    }

    fun launchArguments(resolution: GameResolution): String {
        requireNativeResolution(resolution)
        return "-windowed -ResX=${resolution.width} -ResY=${resolution.height}"
    }

    internal fun nativeResolution(resolution: GameResolution): String =
        "Width:${resolution.width} Height:${resolution.height} Fullscreen:0"

    internal fun configure(current: String, resolution: GameResolution): String {
        requireNativeResolution(resolution)
        var updated = setIniValue(current, "SystemSettings", "ResX", resolution.width.toString())
        updated = setIniValue(updated, "SystemSettings", "ResY", resolution.height.toString())
        updated = setIniValue(updated, "SystemSettings", "Fullscreen", "False")
        return setIniValue(updated, "WinDrv.WindowsClient", "StartupFullscreen", "False")
    }

    private fun requireNativeResolution(resolution: GameResolution) {
        require(resolution.width == REQUIRED_WIDTH && resolution.height == REQUIRED_HEIGHT) {
            "Transformers WFC requires a native 1280x720 upper viewport"
        }
    }

    private fun setIniValue(current: String, section: String, key: String, value: String): String {
        val sectionHeader = Regex("(?im)^\\s*\\[${Regex.escape(section)}]\\s*$")
        val sectionMatch = sectionHeader.find(current)
        val newline = if (current.contains("\r\n")) "\r\n" else "\n"

        if (sectionMatch != null) {
            val nextHeader = Regex("(?m)^\\s*\\[[^]]+]\\s*$")
                .find(current, sectionMatch.range.last + 1)
            val sectionEnd = nextHeader?.range?.first ?: current.length
            val keyPattern = Regex(
                "(?im)^(\\s*${Regex.escape(key)}\\s*=\\s*)[^\\r\\n]*$",
            )
            val existing = keyPattern.find(current, sectionMatch.range.last + 1)
            if (existing != null && existing.range.first < sectionEnd) {
                return current.replaceRange(
                    existing.range,
                    existing.groupValues[1] + value,
                )
            }
            val insertion = sectionMatch.range.last + 1
            return current.substring(0, insertion) + newline + "$key=$value" +
                current.substring(insertion)
        }

        val prefix = current.trimEnd('\r', '\n')
        return buildString {
            if (prefix.isNotEmpty()) {
                append(prefix)
                append(newline)
                append(newline)
            }
            append('[')
            append(section)
            append(']')
            append(newline)
            append(key)
            append('=')
            append(value)
            append(newline)
        }
    }
}
