package com.dualscreen.launcher

import android.content.Context
import android.net.Uri
import com.wemu.engine.container.Container
import com.wemu.engine.container.ContainerManager
import com.wemu.engine.container.Shortcut
import com.wemu.engine.contents.AdrenotoolsManager
import com.wemu.engine.contents.ContentProfile
import com.wemu.engine.contents.ContentsManager
import com.wemu.engine.core.DefaultVersion
import com.wemu.engine.core.GraphicsDriverConfig
import com.wemu.engine.core.KeyValueSet
import com.wemu.engine.session.ContainerPerformanceOptions
import com.wemu.runtime.WemuRuntimeEngine
import com.wemu.runtime.install.RuntimeInstallListener
import com.wemu.runtime.install.RuntimeInstallStage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

internal typealias ProgressReporter = (InstallStage, Float, String) -> Unit

/** Owns immutable app-private presets, with Stardew isolated from every other game. */
class PresetContainerManager(private val context: Context) {
    private val preferences = context.getSharedPreferences("preset_container", Context.MODE_PRIVATE)

    suspend fun prepare(game: SupportedGame, reporter: ProgressReporter): Container {
        prepareRuntime(reporter)
        val specification = presetFor(game)
        val baseProfile = loadPreset(specification)
        val selectedDriver = selectedDriver(baseProfile, specification)
        val profile = profileWithDriver(baseProfile, selectedDriver)
        ComponentProvisioner(context).ensure(profile, reporter)

        val manager = withContext(Dispatchers.IO) { ContainerManager(context) }
        // A driver choice updates the tested container in place. It is not a
        // reason to discard and recreate a working Wine prefix.
        val fingerprint = sha256(baseProfile.toString())
        val storedId = preferences.getInt(containerIdKey(specification), -1)
        if (storedId > 0) {
            val existing = withContext(Dispatchers.IO) {
                manager.reload()
                manager.getContainerById(storedId)
            }
            if (existing != null && existing.configFile.isFile) {
                synchronizePresetConfiguration(existing, baseProfile, selectedDriver, specification)
                preferences.edit().putString(fingerprintKey(specification), fingerprint).apply()
                reporter(InstallStage.CONTAINER, 1f, "Using tested container ${existing.name}")
                return existing
            }
        }

        reporter(InstallStage.CONTAINER, 0.05f, "Creating tested ${specification.expectedName} container")
        val contents = withContext(Dispatchers.IO) {
            ContentsManager(context).apply { syncContents() }
        }
        val created = suspendCancellableCoroutine<Container?> { continuation ->
            manager.createContainerAsync(profileForCreation(profile), contents) { value ->
                if (continuation.isActive) continuation.resume(value)
            }
        } ?: error("The predefined container could not be created")

        preferences.edit()
            .putInt(containerIdKey(specification), created.id)
            .putString(fingerprintKey(specification), fingerprint)
            .apply()
        reporter(InstallStage.CONTAINER, 1f, "Container ${created.name} is ready")
        return created
    }

    /** Applies a validated global driver selection to the existing preset. */
    suspend fun applyGpuDriver(driverId: String): Boolean = withContext(Dispatchers.IO) {
        val selected = driverId.ifBlank { DEFAULT_CAPTURED_GPU_DRIVER }
        val storedId = preferences.getInt(containerIdKey(DEFAULT_PRESET), -1)
        if (storedId <= 0) return@withContext true
        val manager = ContainerManager(context).apply { reload() }
        val container = manager.getContainerById(storedId) ?: return@withContext true
        updateContainerDriver(container, selected)
    }

    private fun updateContainerDriver(container: Container, selected: String): Boolean {
        val config = GraphicsDriverConfig.parse(container.graphicsDriverConfig)
        config["version"] = selected
        container.graphicsDriver = Container.DEFAULT_GRAPHICS_DRIVER
        container.graphicsDriverConfig = GraphicsDriverConfig.toConfigString(config)
        container.rendererDriverId = selected
        val dxConfig = KeyValueSet(container.dxWrapperConfig)
        if (dxConfig.get("version").isEmpty()) {
            container.dxWrapperConfig = Container.DEFAULT_DXWRAPPERCONFIG
        }
        container.putExtra(ContainerPerformanceOptions.SECOND_SCREEN, "1")
        container.putExtra("wemuOwnsDesktop", "1")
        container.putExtra(ContainerPerformanceOptions.DIRECT_SCANOUT, "1")
        container.putExtra(ContainerPerformanceOptions.ADPF_HINTS, "1")
        return container.saveData()
    }

    /**
     * Invalidates copied materialization markers before a captured preset is
     * launched for the first time.
     *
     * The Games JSON contains both settings (which must be copied) and receipts
     * saying that WEMU already wrote those settings into a particular prefix.
     * The Companion owns a different prefix, so copying the receipts caused it
     * to skip the common tools, DXVK, native components, registry changes and
     * theme.  In particular, the final command named winhandler.exe while that
     * file did not exist.  Removing only the receipts makes the normal WEMU
     * bootstrap materialize the exact captured choices into this prefix.
     *
     * @return true when the next session must perform the one-time repair.
     */
    suspend fun prepareForLaunch(
        container: Container,
        game: SupportedGame,
        reporter: (String) -> Unit,
    ): Boolean =
        withContext(Dispatchers.IO) {
            val specification = presetFor(game)
            val expectedId = preferences.getInt(containerIdKey(specification), -1)
            check(expectedId == container.id) {
                "${game.title} is assigned to the wrong predefined container"
            }
            val baseProfile = loadPreset(specification)
            val selectedDriver = selectedDriver(baseProfile, specification)
            ensureSelectedDriverAvailable(selectedDriver)
            val configurationChanged = synchronizePresetConfiguration(
                container,
                baseProfile,
                selectedDriver,
                specification,
            )
            if (container.getExtra(KEY_MATERIALIZATION_REVISION) == MATERIALIZATION_REVISION &&
                !configurationChanged
            ) {
                return@withContext false
            }

            RUNTIME_RECEIPT_KEYS.forEach { key -> container.putExtra(key, null) }
            check(container.saveData()) { "Could not stage the copied container runtime" }
            reporter(
                if (configurationChanged) {
                    "Restored the current WEMU ${specification.expectedName} preset; " +
                        "rebuilding changed runtime files once"
                } else {
                    "The captured configuration is valid, but its private prefix receipts were " +
                        "not transferable; rebuilding the exact runtime files once"
                },
            )
            true
        }

    suspend fun markLaunchMaterialized(container: Container) = withContext(Dispatchers.IO) {
        container.putExtra(KEY_MATERIALIZATION_REVISION, MATERIALIZATION_REVISION)
        check(container.saveData()) { "Could not record the verified container runtime" }
    }

    suspend fun find(id: Int): Container? = withContext(Dispatchers.IO) {
        ContainerManager(context).getContainerById(id)
    }

    suspend fun gameDirectory(container: Container, game: SupportedGame): File =
        withContext(Dispatchers.IO) {
            File(container.rootDir, ".wine/drive_c/Games/${game.id}").apply {
                parentFile?.mkdirs()
            }
        }

    suspend fun createShortcut(
        container: Container,
        game: SupportedGame,
        executable: String,
    ): Shortcut = withContext(Dispatchers.IO) {
        val desktop = container.desktopDir
        check(desktop.isDirectory || desktop.mkdirs()) { "Could not create the launch directory" }
        val file = File(desktop, "${safeName(game.title)}.desktop")
        val guestPath = "C:\\Games\\${game.id}\\${executable.replace('/', '\\')}"
        file.writeText(
            buildString {
                appendLine("[Desktop Entry]")
                appendLine("Name=${game.title}")
                appendLine("Exec=$guestPath")
                appendLine("Type=Application")
                appendLine("StartupNotify=True")
            },
        )
        Shortcut(container, file).also { shortcut ->
            shortcut.putExtra("envVars", supportedGameLaunchEnvironment())
            if (game.launchArguments.isNotBlank()) {
                shortcut.putExtra("execArgs", game.launchArguments)
            }
            shortcut.saveData()
        }
    }

    suspend fun shortcut(containerId: Int, path: String): Shortcut? = withContext(Dispatchers.IO) {
        val manager = ContainerManager(context)
        val container = manager.getContainerById(containerId) ?: return@withContext null
        val file = File(path)
        if (!file.isFile || !file.canonicalPath.startsWith(container.rootDir.canonicalPath + File.separator)) {
            return@withContext null
        }
        Shortcut(container, file)
    }

    private suspend fun prepareRuntime(reporter: ProgressReporter) = withContext(Dispatchers.IO) {
        val installer = WemuRuntimeEngine.INSTANCE.installer
        if (!installer.isInstallNeeded(context)) {
            reporter(InstallStage.RUNTIME, 1f, "Windows runtime already verified")
            return@withContext
        }
        val ok = installer.install(
            context,
            object : RuntimeInstallListener {
                override fun onStage(stage: RuntimeInstallStage) {
                    reporter(InstallStage.RUNTIME, 0f, runtimeLabel(stage))
                }

                override fun onProgress(percent: Int) {
                    reporter(
                        InstallStage.RUNTIME,
                        percent.coerceIn(0, 100) / 100f,
                        "Preparing Windows runtime",
                    )
                }
            },
        )
        check(ok) { "The Windows runtime could not be installed" }
    }

    internal fun requiresDedicatedContainer(game: SupportedGame): Boolean =
        presetFor(game) != DEFAULT_PRESET

    private fun loadPreset(specification: PresetSpecification): JSONObject {
        val text = runCatching {
            context.assets.open(specification.asset).bufferedReader().use { it.readText() }
        }.getOrElse {
            error("The verified ${specification.expectedName} container preset is missing from this build")
        }
        return JSONObject(text).also { data ->
            data.remove("id")
            check(data.optString("name").equals(specification.expectedName, ignoreCase = true)) {
                "The embedded container is not the captured ${specification.expectedName} preset"
            }
        }
    }

    private fun selectedDriver(base: JSONObject, specification: PresetSpecification): String =
        if (specification.allowGpuOverride) {
            LauncherSettingsStore(context).settings.value.gpuDriverId
                .ifBlank { DEFAULT_CAPTURED_GPU_DRIVER }
        } else {
            base.getString("rendererDriverId")
        }

    private fun profileWithDriver(base: JSONObject, selected: String): JSONObject =
        JSONObject(base.toString()).also { profile ->
            val config = GraphicsDriverConfig.parse(
                profile.optString("graphicsDriverConfig", Container.DEFAULT_GRAPHICSDRIVERCONFIG),
            )
            config["version"] = selected
            profile.put("graphicsDriver", Container.DEFAULT_GRAPHICS_DRIVER)
            profile.put("graphicsDriverConfig", GraphicsDriverConfig.toConfigString(config))
            profile.put("rendererDriverId", selected)
        }

    /**
     * Re-applies every locked preset field while preserving the two settings
     * this launcher intentionally exposes: resolution/DPI and GPU driver.
     * Existing prefix receipts and game compatibility revisions survive the
     * merge; prepareForLaunch invalidates only receipts whose files must be
     * materialized again after an actual configuration change.
     */
    private fun synchronizePresetConfiguration(
        container: Container,
        base: JSONObject,
        selectedDriver: String,
        specification: PresetSpecification,
    ): Boolean {
        val desired = profileWithDriver(base, selectedDriver)
        desired.put("screenSize", container.screenSize)
        desired.put("dpi", container.dpi)
        val mergedExtras = container.extraDataCopy
        desired.optJSONObject("extraData")?.let { captured ->
            captured.keys().forEach { key -> mergedExtras.put(key, captured.get(key)) }
        }
        desired.put("extraData", mergedExtras)

        val before = container.toJson().toString()
        container.loadData(desired)
        val changed = before != container.toJson().toString()
        check(!changed || container.saveData()) {
            "Could not synchronize the predefined ${specification.expectedName} container"
        }
        return changed
    }

    private fun ensureSelectedDriverAvailable(driverId: String) {
        if (!requiresAdrenotoolsProvisioning(driverId)) return
        val manager = AdrenotoolsManager(context)
        val installed = driverId in manager.enumarateInstalledDrivers() &&
            manager.getLibraryName(driverId).isNotBlank()
        val available = installed || (
            manager.extractDriverFromResources(driverId) &&
                manager.getLibraryName(driverId).isNotBlank()
            )
        check(available) {
            "The selected GPU driver is unavailable: $driverId"
        }
    }

    /** A fresh prefix must never inherit receipts belonging to WEMU's prefix. */
    private fun profileForCreation(profile: JSONObject): JSONObject =
        JSONObject(profile.toString()).also { copy ->
            copy.optJSONObject("extraData")?.let { extras ->
                RUNTIME_RECEIPT_KEYS.forEach(extras::remove)
                extras.remove(KEY_MATERIALIZATION_REVISION)
            }
        }

    private fun runtimeLabel(stage: RuntimeInstallStage): String = when (stage) {
        RuntimeInstallStage.DOWNLOADING_SYSTEM_FILES -> "Downloading system files"
        RuntimeInstallStage.DOWNLOADING_WINE_FILES -> "Downloading Wine runtime"
        RuntimeInstallStage.DOWNLOADING_CONTAINER_FILES -> "Downloading container template"
        RuntimeInstallStage.SYSTEM_FILES -> "Installing system files"
        RuntimeInstallStage.WINE_FILES -> "Installing Wine runtime"
        RuntimeInstallStage.GRAPHICS_DRIVERS -> "Installing graphics runtime"
    }

    private fun safeName(value: String): String = value.replace(Regex("[^A-Za-z0-9._ -]"), "_")

    private fun sha256(value: String): String = MessageDigest.getInstance("SHA-256")
        .digest(value.toByteArray())
        .joinToString("") { "%02x".format(it) }

    companion object {
        private val DEFAULT_PRESET = PresetSpecification(
            key = "games",
            asset = "presets/games.json",
            expectedName = "games",
            allowGpuOverride = true,
        )
        private val STARDEW_PRESET = PresetSpecification(
            key = "stardew-proton9",
            asset = "presets/stardew-proton9.json",
            expectedName = "proton 9",
            allowGpuOverride = false,
        )
        private const val KEY_CONTAINER_ID = "containerId"
        private const val KEY_FINGERPRINT = "fingerprint"
        private const val KEY_MATERIALIZATION_REVISION = "companionPresetMaterialization"
        private const val MATERIALIZATION_REVISION = "3"

        private fun presetFor(game: SupportedGame): PresetSpecification =
            if (game.id == "stardew-valley") STARDEW_PRESET else DEFAULT_PRESET

        private fun containerIdKey(specification: PresetSpecification): String =
            if (specification == DEFAULT_PRESET) KEY_CONTAINER_ID
            else "$KEY_CONTAINER_ID.${specification.key}"

        private fun fingerprintKey(specification: PresetSpecification): String =
            if (specification == DEFAULT_PRESET) KEY_FINGERPRINT
            else "$KEY_FINGERPRINT.${specification.key}"

        /** Values produced by bootstrap, not user-selected configuration. */
        private val RUNTIME_RECEIPT_KEYS = setOf(
            "imgVersion",
            "graphicsDriver",
            "dxwrapper",
            "wincomponents",
            "desktopTheme",
            "guestHudStripped",
            "dxvkCacheRevision",
            "fexcoreVersion",
            "box64Version",
            "skyrimAudioProvisioning",
            "audioDriver",
            "dpi",
            "startupSelection",
        )
    }
}

private data class PresetSpecification(
    val key: String,
    val asset: String,
    val expectedName: String,
    val allowGpuOverride: Boolean,
)

/**
 * Supported games never need Wine's optional Mono or Gecko downloaders. Wine
 * otherwise opens those modal first-boot installers before the requested EXE,
 * which a game-only launcher deliberately hides and therefore cannot dismiss.
 */
internal fun supportedGameLaunchEnvironment(): String =
    "WINEDLLOVERRIDES=icu=n;mscoree=d;mshtml=d FAKE_EVDEV_LOG=1"

/** Installs only the exact component ids named by the captured preset. */
private class ComponentProvisioner(private val context: Context) {
    private val receipts = context.getSharedPreferences("captured_components", Context.MODE_PRIVATE)

    suspend fun ensure(profile: JSONObject, reporter: ProgressReporter) {
        val required = componentIds(profile)
        if (required.isEmpty()) {
            reporter(InstallStage.COMPONENTS, 1f, "Preset uses bundled components")
            return
        }

        val contents = withContext(Dispatchers.IO) {
            ContentsManager(context).apply { syncContents() }
        }
        val captured = loadCapturedComponents()
        val unavailable = required - captured.keys
        check(unavailable.isEmpty()) {
            "The exact captured component package is missing: ${unavailable.joinToString()}"
        }
        val missing = required.filter { id ->
            contents.getProfileByEntryName(id) == null ||
                receipts.getString(id, null) != captured[id]?.sha256
        }
        if (missing.isEmpty()) {
            verifyFixedRuntimeAssets(profile)
            reporter(InstallStage.COMPONENTS, 1f, "Exact captured compatibility components verified")
            return
        }

        missing.forEachIndexed { indexOf, id ->
            val component = checkNotNull(captured[id])
            reporter(
                InstallStage.COMPONENTS,
                indexOf.toFloat() / missing.size,
                "Installing captured $id",
            )
            val archive = withContext(Dispatchers.IO) {
                copyCaptured(component) { percent ->
                    reporter(
                        InstallStage.COMPONENTS,
                        (indexOf + percent / 100f) / missing.size,
                        "Installing captured $id",
                    )
                }
            }
            contents.getProfileByEntryName(id)?.let(contents::removeContent)
            install(contents, archive, id)
            receipts.edit().putString(id, component.sha256).apply()
        }
        verifyFixedRuntimeAssets(profile)
        reporter(InstallStage.COMPONENTS, 1f, "Exact captured compatibility components installed")
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

    private fun loadCapturedComponents(): Map<String, CapturedComponent> {
        val text = context.assets.open(COMPONENT_MANIFEST).bufferedReader().use { it.readText() }
        val array = JSONObject(text).getJSONArray("components")
        return buildMap {
            for (index in 0 until array.length()) {
                val item = array.getJSONObject(index)
                val component = CapturedComponent(
                    id = item.getString("id"),
                    source = item.getString("source"),
                    bytes = item.getLong("bytes"),
                    sha256 = item.getString("sha256").lowercase(),
                )
                check(put(component.id, component) == null) {
                    "Duplicate captured component: ${component.id}"
                }
            }
        }
    }

    private fun copyCaptured(component: CapturedComponent, progress: (Int) -> Unit): File {
        val target = File(context.cacheDir, "captured-${safeFile(component.id)}.tgz")
        val partial = File(target.parentFile, target.name + ".partial")
        partial.delete()
        var received = 0L
        context.assets.open("components/${component.source}").use { input ->
            FileOutputStream(partial).use { output ->
                val buffer = ByteArray(128 * 1024)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    output.write(buffer, 0, count)
                    received += count
                    val percent = (((received * 100L) + component.bytes - 1L) / component.bytes)
                        .toInt()
                        .coerceIn(1, 100)
                    progress(percent)
                }
                output.fd.sync()
            }
        }
        check(received == component.bytes) { "Captured component size mismatch: ${component.id}" }
        check(sha256(partial) == component.sha256) { "Captured component hash mismatch: ${component.id}" }
        target.delete()
        check(partial.renameTo(target)) { "Could not publish captured component: ${component.id}" }
        return target
    }

    private fun verifyFixedRuntimeAssets(profile: JSONObject) {
        val box64 = profile.optString("box64Version")
        check(box64.isNotBlank()) { "The captured preset has no Box64 version" }
        context.assets.open("box64/box64-$box64.tzst").close()

        val driver = profile.optString("rendererDriverId")
        check(driver.isNotBlank()) { "The captured preset has no renderer driver" }
        if (!requiresAdrenotoolsProvisioning(driver)) return
        val manager = AdrenotoolsManager(context)
        val alreadyInstalled = driver in manager.enumarateInstalledDrivers() &&
            manager.getLibraryName(driver).isNotBlank()
        // Extracting the bundled driver performs real archive and filesystem
        // work. Do it only as a repair when the installed driver is absent or
        // incomplete; the previous eager expression unpacked Turnip during
        // every otherwise-complete game setup.
        val available = alreadyInstalled || (
            manager.extractDriverFromResources(driver) &&
                manager.getLibraryName(driver).isNotBlank()
            )
        check(available) {
            "The exact captured graphics driver could not be provisioned: $driver"
        }
    }

    private suspend fun install(contents: ContentsManager, archive: File, expectedId: String) {
        try {
            val validated = suspendCancellableCoroutine<ContentProfile> { continuation ->
                contents.extraContentFile(
                    Uri.fromFile(archive),
                    object : ContentsManager.OnInstallFinishedCallback {
                        override fun onFailed(reason: ContentsManager.InstallFailedReason, e: Exception?) {
                            continuation.resumeWithException(
                                IllegalStateException("Component package rejected: $reason", e),
                            )
                        }

                        override fun onSucceed(profile: ContentProfile) {
                            val observed = ContentsManager.getEntryName(profile)
                            if (observed != expectedId) {
                                continuation.resumeWithException(
                                    IllegalStateException("Expected $expectedId but package contains $observed"),
                                )
                            } else {
                                continuation.resume(profile)
                            }
                        }
                    },
                )
            }
            suspendCancellableCoroutine<Unit> { continuation ->
                contents.finishInstallContent(
                    validated,
                    object : ContentsManager.OnInstallFinishedCallback {
                        override fun onFailed(reason: ContentsManager.InstallFailedReason, e: Exception?) {
                            continuation.resumeWithException(
                                IllegalStateException("Component install failed: $reason", e),
                            )
                        }

                        override fun onSucceed(profile: ContentProfile) = continuation.resume(Unit)
                    },
                )
            }
            contents.syncContents()
        } finally {
            archive.delete()
            ContentsManager.cleanTmpDir(context)
        }
    }

    private fun safeFile(value: String): String = value.replace(Regex("[^A-Za-z0-9._-]"), "_")

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered(128 * 1024).use { input ->
            val buffer = ByteArray(128 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private data class CapturedComponent(
        val id: String,
        val source: String,
        val bytes: Long,
        val sha256: String,
    )

    companion object {
        private const val COMPONENT_MANIFEST = "components/manifest.json"
    }
}

internal fun requiresAdrenotoolsProvisioning(driverId: String): Boolean =
    driverId.isNotBlank() && !driverId.equals(DefaultVersion.WRAPPER, ignoreCase = true)
