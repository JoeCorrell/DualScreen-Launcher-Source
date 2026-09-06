package com.dualscreen.launcher

import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** AC II reads this device name from [Input], rather than auto-switching from keyboard. */
internal object Ac2GamepadConfiguration {
    const val CONTROLLER_NAME = "Controller (XBOX 360 For Windows)"

    data class ApplyResult(val preferencesFile: File, val changed: Boolean)

    fun ensureEnabled(containerRoot: File): ApplyResult {
        val user = File(containerRoot, ".wine/drive_c/users/xuser")
        val roaming = File(user, "AppData/Roaming")
        val legacy = File(user, "Application Data")
        // Wine's older profiles use Application Data; modern ones alias it to Roaming.
        val relative = "Ubisoft/Assassin's Creed 2/Assassin2.ini"
        val preferences = listOf(File(roaming, relative), File(legacy, relative))
            .firstOrNull(File::isFile)
            ?: File(if (roaming.isDirectory || !legacy.isDirectory) roaming else legacy, relative)
        val original = preferences.takeIf(File::isFile)?.readText().orEmpty()
        val updated = selectController(original)
        if (original == updated) return ApplyResult(preferences, false)
        check(preferences.parentFile!!.isDirectory || preferences.parentFile!!.mkdirs()) {
            "Could not create AC II's preferences directory"
        }
        val backup = File(preferences.parentFile, "Assassin2.ini.before-controller")
        if (preferences.isFile && !backup.exists()) preferences.copyTo(backup)
        val temporary = File.createTempFile("ac2-input-", ".tmp", preferences.parentFile)
        try {
            temporary.writeText(updated)
            Files.move(temporary.toPath(), preferences.toPath(), StandardCopyOption.REPLACE_EXISTING)
        } finally {
            temporary.delete()
        }
        return ApplyResult(preferences, true)
    }

    internal fun selectController(current: String): String {
        val newline = if (current.contains("\r\n")) "\r\n" else "\n"
        val header = Regex("(?im)^[ \\t]*\\[Input][ \\t]*(?=\\r?$)").find(current)
        if (header == null) {
            return current + (if (current.isEmpty() || current.endsWith('\n')) "" else newline) +
                "[Input]${newline}SelectedInput=$CONTROLLER_NAME$newline"
        }
        val start = header.range.last + 1
        val next = Regex("(?m)^[ \\t]*\\[").find(current, start)?.range?.first ?: current.length
        val body = current.substring(start, next)
        val key = Regex("(?im)^([ \\t]*SelectedInput[ \\t]*=[ \\t]*)[^\\r\\n]*")
        val replacement = if (key.containsMatchIn(body)) {
            key.replace(body) { it.groupValues[1] + CONTROLLER_NAME }
        } else {
            newline + "SelectedInput=$CONTROLLER_NAME" + (if (body.startsWith(newline)) "" else newline) + body
        }
        return current.substring(0, start) + replacement + current.substring(next)
    }
}
