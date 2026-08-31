package com.dualscreen.launcher

import com.wemu.engine.core.FileUtils
import com.wemu.engine.core.WineRegistryEditor
import com.wemu.engine.session.Fallout3Compatibility
import java.io.File

/** Installs the Fallout launcher prerequisites into the app-private Wine prefix. */
internal object Fallout3LaunchConfiguration {
    private const val INSTALLED_PATH_KEY =
        "Software\\Wow6432Node\\Bethesda Softworks\\Fallout3"
    private const val INSTALLED_PATH_VALUE = "Installed Path"
    private const val SETTINGS_PATH =
        ".wine/drive_c/users/xuser/Documents/My Games/Fallout3"

    data class Result(
        val installedPath: String,
        val systemRegistry: File,
        val userRegistry: File,
        val dinput8Override: String,
        val falloutIni: File,
        val preferencesFile: File,
        val seededFalloutIni: Boolean,
        val seededPreferences: Boolean,
        val displayWidth: Int,
        val displayHeight: Int,
    )

    fun ensure(
        containerRoot: File,
        gameRoot: File,
        guestGameRoot: String,
        displayWidth: Int,
        displayHeight: Int,
    ): Result {
        require(containerRoot.isDirectory) { "The container root is unavailable" }
        require(gameRoot.isDirectory) { "Fallout 3's game directory is unavailable" }
        val prefix = File(containerRoot, ".wine")
        val systemRegistry = File(prefix, "system.reg")
        check(systemRegistry.isFile) { "The Wine system registry is unavailable" }
        val userRegistry = File(prefix, "user.reg")
        check(userRegistry.isFile) { "The Wine user registry is unavailable" }
        val settings = File(containerRoot, SETTINGS_PATH)
        check(settings.isDirectory || settings.mkdirs()) {
            "Could not create Fallout 3's settings directory"
        }

        val falloutIni = File(settings, "FALLOUT.INI")
        val seededFalloutIni = seedIfMissing(
            falloutIni,
            listOf(File(gameRoot, "Fallout.ini"), File(gameRoot, "Fallout_default.ini")),
            "[Controls]\r\nbUse Joystick=1\r\nbGamePadRumble=1\r\n" +
                "[Interface]\r\nbDisable360Controller=0\r\n",
        )
        val preferencesFile = File(settings, "FalloutPrefs.ini")
        val seededPreferences = seedIfMissing(
            preferencesFile,
            listOf(
                File(gameRoot, "Fallout3/FalloutPrefs.ini"),
                File(gameRoot, "FalloutPrefs.ini"),
            ),
            "[Display]\r\nbFull Screen=1\r\n" +
                "[Controls]\r\nbUse Joystick=1\r\nbGamePadRumble=1\r\n" +
                "[Interface]\r\nbDisable360Controller=0\r\n",
        )

        val installedPath = guestGameRoot.trimEnd('\\', '/') + "\\"
        WineRegistryEditor(systemRegistry).use { registry ->
            registry.setStringValue(INSTALLED_PATH_KEY, INSTALLED_PATH_VALUE, installedPath)
        }
        WineRegistryEditor(systemRegistry).use { registry ->
            check(registry.getStringValue(INSTALLED_PATH_KEY, INSTALLED_PATH_VALUE) == installedPath) {
                "Fallout 3's Installed Path registry value could not be verified"
            }
        }
        // The second-screen mod is an app-local dinput8 proxy. Wine prefers its
        // builtin dinput8 unless this executable-scoped policy is present. Do
        // this on every launch: a copied-container provisioning receipt is not
        // proof that the private prefix's user.reg contains the policy, and a
        // launcher/settings run may rewrite the registry after provisioning.
        WineRegistryEditor(userRegistry).use { registry ->
            Fallout3Compatibility.applyExecutableOverrides(registry)
        }
        val dinput8Override = WineRegistryEditor(userRegistry).use { registry ->
            registry.getStringValue(
                "Software\\Wine\\AppDefaults\\Fallout3.exe\\DllOverrides",
                "dinput8",
            )
        }
        check(dinput8Override.equals("native,builtin", ignoreCase = true)) {
            "Fallout 3's second-screen DLL override could not be verified"
        }
        check(Fallout3Compatibility.applyRuntimeSettings(prefix)) {
            "Fallout 3's controller and compatibility INIs could not be prepared"
        }
        val displayProfiles = listOf(
            preferencesFile,
            File(gameRoot, "FalloutPrefs.ini"),
            File(gameRoot, "Fallout3/FalloutPrefs.ini"),
        ).distinctBy(File::getAbsolutePath).filter(File::isFile)
        check(displayProfiles.isNotEmpty()) {
            "Fallout 3's display profile is unavailable"
        }
        check(displayProfiles.all {
            Fallout3Compatibility.applyDisplayResolution(it, displayWidth, displayHeight)
        }) {
            "Fallout 3's display resolution could not be synchronized"
        }
        verifyController(falloutIni)
        verifyController(preferencesFile)
        verifyResolution(preferencesFile, displayWidth, displayHeight)
        return Result(
            installedPath = installedPath,
            systemRegistry = systemRegistry,
            userRegistry = userRegistry,
            dinput8Override = dinput8Override,
            falloutIni = falloutIni,
            preferencesFile = preferencesFile,
            seededFalloutIni = seededFalloutIni,
            seededPreferences = seededPreferences,
            displayWidth = displayWidth,
            displayHeight = displayHeight,
        )
    }

    private fun seedIfMissing(target: File, sources: List<File>, fallback: String): Boolean {
        if (target.isFile) return false
        val source = sources.firstOrNull(File::isFile)
        val contents = source?.let(FileUtils::readString)?.takeIf { it.isNotBlank() } ?: fallback
        check(FileUtils.writeStringAtomic(target, contents)) {
            "Could not create ${target.name}"
        }
        return true
    }

    private fun verifyController(file: File) {
        val contents = FileUtils.readString(file) ?: error("Could not verify ${file.name}")
        check(Regex("(?im)^\\s*bUse Joystick\\s*=\\s*1\\s*$").containsMatchIn(contents)) {
            "${file.name} did not enable joystick input"
        }
        check(Regex("(?im)^\\s*bDisable360Controller\\s*=\\s*0\\s*$").containsMatchIn(contents)) {
            "${file.name} disabled the Xbox controller"
        }
    }

    private fun verifyResolution(file: File, width: Int, height: Int) {
        val contents = FileUtils.readString(file) ?: error("Could not verify ${file.name}")
        fun has(key: String, value: Int) =
            Regex("(?im)^\\s*${Regex.escape(key)}\\s*=\\s*$value\\s*$").containsMatchIn(contents)
        check(has("iSize W", width) && has("iSize H", height)) {
            "${file.name} does not match the container resolution ${width}x$height"
        }
    }
}
