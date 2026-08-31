package com.dualscreen.launcher

import java.io.File

/**
 * Installs the Skyrim gamepad preference into the Wine user's real profile.
 *
 * The mod payload lives beside SkyrimSE.exe, but Skyrim reads bGamepadEnable
 * from Documents/My Games/Skyrim Special Edition/SkyrimPrefs.ini.  Keeping the
 * bundled file as a tiny override lets the launcher merge only this setting;
 * replacing SkyrimPrefs.ini would destroy the player's graphics preferences.
 */
internal object SkyrimGamepadConfiguration {
    private const val BUNDLED_OVERRIDE = "SkyrimGamepad.ini"
    private const val PREFS_DIRECTORY = "Documents/My Games/Skyrim Special Edition"
    private const val PREFS_FILE = "SkyrimPrefs.ini"
    private val gamepadSetting = Regex(
        pattern = "(?im)^(\\s*bGamepadEnable\\s*=\\s*)[^\\r\\n]*$",
    )

    data class ApplyResult(
        val preferencesFile: File,
        val changed: Boolean,
    )

    fun ensureEnabled(
        containerRoot: File,
        gameRoot: File,
        fallbackOverride: String? = null,
        resolution: GameResolution = GameResolution.HD,
    ): ApplyResult {
        val bundled = File(gameRoot, BUNDLED_OVERRIDE)
        if (!bundled.isFile && fallbackOverride != null) {
            bundled.writeText(fallbackOverride)
        }
        check(bundled.isFile) { "The Skyrim gamepad configuration is missing" }
        val bundledText = bundled.readText()
        check(gamepadSetting.containsMatchIn(bundledText)) {
            "The bundled Skyrim gamepad configuration is invalid"
        }

        val wineUsers = File(containerRoot, ".wine/drive_c/users")
        val wineUser = wineUsers.listFiles()
            ?.filter(File::isDirectory)
            ?.firstOrNull { it.name.equals("xuser", ignoreCase = true) }
            ?: File(wineUsers, "xuser")
        val preferences = File(wineUser, "$PREFS_DIRECTORY/$PREFS_FILE")
        check(preferences.parentFile?.let { it.isDirectory || it.mkdirs() } == true) {
            "Could not create Skyrim's preferences directory"
        }

        val original = preferences.takeIf(File::isFile)?.readText().orEmpty()
        val updated = configurePreferences(
            current = original,
            bundled = bundledText,
            resolution = resolution,
        )
        if (updated == original) return ApplyResult(preferences, changed = false)

        preferences.writeText(updated)
        return ApplyResult(preferences, changed = true)
    }

    internal fun enableGamepad(current: String, bundled: String): String {
        val directive = gamepadSetting.find(bundled)?.value?.trim()
            ?: error("The bundled Skyrim gamepad configuration is invalid")
        val normalizedDirective = directive.substringBefore('=') + "=1"
        if (gamepadSetting.containsMatchIn(current)) {
            return gamepadSetting.replace(current) { match ->
                match.groupValues[1] + "1"
            }
        }

        val newline = if (current.contains("\r\n")) "\r\n" else "\n"
        val prefix = current.trimEnd('\r', '\n')
        return buildString {
            if (prefix.isNotEmpty()) {
                append(prefix)
                append(newline)
                append(newline)
            }
            append("[MAIN]")
            append(newline)
            append(normalizedDirective)
            append(newline)
        }
    }

    internal fun configurePreferences(
        current: String,
        bundled: String,
        resolution: GameResolution,
    ): String {
        var updated = enableGamepad(current, bundled)
        updated = setIniValue(updated, "Display", "bBorderless", "1")
        // Borderless is the reliable fullscreen mode inside Wine's virtual
        // desktop. Skyrim's exclusive mode can create a second, mismatched X
        // window; borderless fills the selected container exactly.
        updated = setIniValue(updated, "Display", "bFull Screen", "0")
        updated = setIniValue(updated, "Display", "iSize W", resolution.width.toString())
        updated = setIniValue(updated, "Display", "iSize H", resolution.height.toString())
        return updated
    }

    private fun setIniValue(current: String, section: String, key: String, value: String): String {
        val setting = Regex(
            pattern = "(?im)^(\\s*${Regex.escape(key)}\\s*=\\s*)[^\\r\\n]*$",
        )
        if (setting.containsMatchIn(current)) {
            return setting.replace(current) { match -> match.groupValues[1] + value }
        }

        val newline = if (current.contains("\r\n")) "\r\n" else "\n"
        val header = Regex("(?im)^\\s*\\[${Regex.escape(section)}]\\s*$")
        val existingSection = header.find(current)
        if (existingSection != null) {
            val insertion = existingSection.range.last + 1
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
            append("[$section]")
            append(newline)
            append(key)
            append('=')
            append(value)
            append(newline)
        }
    }
}
