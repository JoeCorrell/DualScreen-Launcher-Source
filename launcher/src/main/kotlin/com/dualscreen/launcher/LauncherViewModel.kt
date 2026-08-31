package com.dualscreen.launcher

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class LauncherViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as DualScreenLauncherApplication
    val progress = app.installer.progress
    val saveProgress = app.saveImporter.progress
    val settings = app.settings.settings
    val launchStatus = app.launchStatus.status
    val gpuDrivers = app.gpuDrivers.drivers
    val gpuDriverImportState = app.gpuDrivers.importState
    internal val payloadState = app.payloads.state
    internal val updateState = app.updates.state
    private var synchronizedPayloadVersion: String? = null

    private val mutableGpuDriverMessage = MutableStateFlow<String?>(null)
    val gpuDriverMessage: StateFlow<String?> = mutableGpuDriverMessage.asStateFlow()

    private val mutableInstalled = MutableStateFlow(app.registry.all())
    val installed: StateFlow<Map<String, InstalledGame>> = mutableInstalled.asStateFlow()

    init {
        refreshUpdates()
        viewModelScope.launch { app.gpuDrivers.refresh() }
    }

    fun refreshUpdates() {
        viewModelScope.launch {
            app.payloads.ensureReady()
            synchronizeInstalledMods()
        }
        viewModelScope.launch { app.updates.check() }
    }

    private suspend fun synchronizeInstalledMods() {
        val ready = payloadState.value as? PayloadBootstrapState.Ready ?: return
        if (synchronizedPayloadVersion == ready.version) return
        app.registry.all().values.forEach { installed ->
            GameCatalog.byId(installed.gameId)?.let { game ->
                app.installer.ensureCurrentPayload(game)
            }
        }
        mutableInstalled.value = app.registry.all()
        synchronizedPayloadVersion = ready.version
    }

    fun install(game: SupportedGame, archive: Uri) {
        if (payloadState.value !is PayloadBootstrapState.Ready) return
        if (progress.value.values.any(InstallProgress::running) ||
            saveProgress.value.values.any(SaveImportProgress::running)
        ) return
        app.saveImporter.clear()
        viewModelScope.launch {
            app.installer.install(game, archive)
            mutableInstalled.value = app.registry.all()
        }
    }

    fun importSave(game: SupportedGame, installed: InstalledGame, archive: Uri) {
        if (progress.value.values.any(InstallProgress::running) ||
            saveProgress.value.values.any(SaveImportProgress::running)
        ) return
        viewModelScope.launch {
            app.saveImporter.import(game, installed, archive)
        }
    }

    fun clearSaveImport() = app.saveImporter.clear()

    fun setKeepScreenAwake(enabled: Boolean) = app.settings.setKeepScreenAwake(enabled)

    fun setResolution(resolution: GameResolution) = app.settings.setResolution(resolution)

    fun setGpuDriver(driverId: String) {
        viewModelScope.launch {
            mutableGpuDriverMessage.value = "Applying GPU driver..."
            val applied = runCatching {
                PresetContainerManager(app).applyGpuDriver(driverId)
            }.getOrDefault(false)
            if (applied) {
                app.settings.setGpuDriver(driverId)
                mutableGpuDriverMessage.value = "GPU driver will be used for the next game launch."
            } else {
                mutableGpuDriverMessage.value = "Could not apply that GPU driver."
            }
        }
    }

    fun importGpuDriver(uri: Uri) {
        viewModelScope.launch {
            val driverId = app.gpuDrivers.install(uri) ?: return@launch
            setGpuDriver(driverId)
        }
    }

    fun retryPayloadBootstrap() {
        viewModelScope.launch {
            app.payloads.ensureReady(force = true)
            synchronizedPayloadVersion = null
            synchronizeInstalledMods()
        }
    }
}
