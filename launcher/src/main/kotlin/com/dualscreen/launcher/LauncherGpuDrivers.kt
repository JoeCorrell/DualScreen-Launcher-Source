package com.dualscreen.launcher

import android.content.Context
import android.net.Uri
import com.thor.core.common.log.ThorLog
import com.wemu.engine.contents.AdrenotoolsManager
import com.wemu.engine.contents.BundledContents
import com.wemu.engine.core.DefaultVersion
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext

/** A Vulkan driver the Launcher can safely place in its tested container preset. */
data class LauncherGpuDriver(
    val id: String,
    val label: String,
    val source: Source,
) {
    enum class Source { SYSTEM, BUNDLED, UPLOADED }
}

/** User-visible state for the document-picker driver import. */
sealed interface GpuDriverImportState {
    data object Idle : GpuDriverImportState
    data object Importing : GpuDriverImportState
    data class Installed(val driver: LauncherGpuDriver) : GpuDriverImportState
    data class Failed(val message: String) : GpuDriverImportState
}

/**
 * The Launcher's small, safe view over the engine's AdrenoTools driver manager.
 *
 * Archive validation and extraction deliberately stay in [AdrenotoolsManager]:
 * it rejects path traversal, links, missing metadata/libraries, excessive entry
 * counts and packages that expand beyond its limit. The launcher never opens or
 * unpacks an uploaded archive itself.
 */
class LauncherGpuDrivers(
    context: Context,
    private val io: CoroutineDispatcher = Dispatchers.IO,
) {
    private val appContext = context.applicationContext
    private val manager = AdrenotoolsManager(appContext)

    private val _drivers = MutableStateFlow<List<LauncherGpuDriver>>(emptyList())
    val drivers: StateFlow<List<LauncherGpuDriver>> = _drivers.asStateFlow()

    private val _importState = MutableStateFlow<GpuDriverImportState>(GpuDriverImportState.Idle)
    val importState: StateFlow<GpuDriverImportState> = _importState.asStateFlow()

    suspend fun refresh() {
        _drivers.value = withContext(io) { readInstalled() }
    }

    /** Installs a selected AdrenoTools ZIP and returns its validated driver id. */
    suspend fun install(uri: Uri): String? {
        _importState.value = GpuDriverImportState.Importing
        val id = withContext(io) {
            runCatching { manager.installDriver(uri) }
                .onFailure { ThorLog.e(TAG, "Could not import GPU driver $uri", it) }
                .getOrDefault("")
        }
        refresh()

        val installed = _drivers.value.firstOrNull { it.id == id }
        return if (id.isNotBlank() && installed != null) {
            _importState.value = GpuDriverImportState.Installed(installed)
            id
        } else {
            _importState.value = GpuDriverImportState.Failed(
                "That file is not a valid AdrenoTools driver ZIP, or it is already installed.",
            )
            null
        }
    }

    fun clearImportMessage() {
        if (_importState.value !is GpuDriverImportState.Importing) {
            _importState.value = GpuDriverImportState.Idle
        }
    }

    private fun readInstalled(): List<LauncherGpuDriver> {
        val bundled = runCatching { BundledContents.drivers(appContext) }
            .onFailure { ThorLog.w(TAG, "Could not read bundled GPU drivers", it) }
            .getOrDefault(listOf(DefaultVersion.WRAPPER))
        val uploaded = runCatching { manager.enumarateInstalledDrivers().toList() }
            .onFailure { ThorLog.w(TAG, "Could not enumerate uploaded GPU drivers", it) }
            .getOrDefault(emptyList())

        return (bundled + uploaded)
            .distinct()
            .map { id ->
                val source = when {
                    id.equals(DefaultVersion.WRAPPER, ignoreCase = true) ->
                        LauncherGpuDriver.Source.SYSTEM
                    id in uploaded -> LauncherGpuDriver.Source.UPLOADED
                    else -> LauncherGpuDriver.Source.BUNDLED
                }
                LauncherGpuDriver(id = id, label = labelOf(id), source = source)
            }
    }

    private fun labelOf(id: String): String {
        if (id.equals(DefaultVersion.WRAPPER, ignoreCase = true)) return "System driver"
        val metadataName = manager.getDriverName(id)
        val version = manager.getDriverVersion(id)
        return when {
            metadataName.isNotBlank() && version.isNotBlank() &&
                !metadataName.contains(version, ignoreCase = true) -> "$metadataName · $version"
            metadataName.isNotBlank() -> metadataName
            id.startsWith("turnip", ignoreCase = true) ->
                "Turnip ${id.removePrefix("turnip")}".trim()
            else -> id
        }
    }

    private companion object {
        const val TAG = "LauncherGpuDrivers"
    }
}
