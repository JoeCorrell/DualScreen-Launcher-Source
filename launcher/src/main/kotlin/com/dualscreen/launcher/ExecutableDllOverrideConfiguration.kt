package com.dualscreen.launcher

import com.wemu.engine.core.WineRegistryEditor
import java.io.File

/** Applies executable-scoped native proxy and Wine compatibility policies. */
internal object ExecutableDllOverrideConfiguration {
    private val executablePattern = Regex("[A-Za-z0-9 _.'()-]+\\.exe", RegexOption.IGNORE_CASE)
    private val dllPattern = Regex("[A-Za-z0-9_.-]+")

    fun ensure(containerRoot: File, executableName: String, overrides: Map<String, String>): File {
        require(containerRoot.isDirectory) { "The container root is unavailable" }
        require(executablePattern.matches(executableName)) { "Invalid executable override target" }
        require(overrides.isNotEmpty()) { "At least one DLL override is required" }
        require(overrides.all { (dll, value) ->
            dllPattern.matches(dll) && (
                value.equals("native,builtin", ignoreCase = true) ||
                    value.equals("builtin,native", ignoreCase = true)
                )
        }) { "Invalid DLL override policy" }

        val userRegistry = File(containerRoot, ".wine/user.reg")
        check(userRegistry.isFile) { "The Wine user registry is unavailable" }
        val key = "Software\\Wine\\AppDefaults\\$executableName\\DllOverrides"
        WineRegistryEditor(userRegistry).use { registry ->
            overrides.forEach { (dll, value) -> registry.setStringValue(key, dll, value) }
        }
        WineRegistryEditor(userRegistry).use { registry ->
            overrides.forEach { (dll, value) ->
                check(registry.getStringValue(key, dll).equals(value, ignoreCase = true)) {
                    "$executableName's $dll override could not be verified"
                }
            }
        }
        return userRegistry
    }
}
