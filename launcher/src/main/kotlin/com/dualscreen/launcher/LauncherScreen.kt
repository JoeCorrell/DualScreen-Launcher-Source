package com.dualscreen.launcher

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.lifecycle.compose.collectAsStateWithLifecycle

private sealed interface LauncherPage {
    data object Games : LauncherPage
    data class GameDetails(val gameId: String) : LauncherPage
    data object Guide : LauncherPage
    data object Settings : LauncherPage
}

internal enum class LauncherNavItem { GAMES, GUIDE, SETTINGS }

@Composable
internal fun LauncherScreen(
    model: LauncherViewModel,
    onPlay: (SupportedGame, InstalledGame) -> Unit,
    onConfigure: (SupportedGame, InstalledGame) -> Unit,
    onOpenUpdate: (String) -> Unit,
) {
    val installed by model.installed.collectAsStateWithLifecycle()
    val progress by model.progress.collectAsStateWithLifecycle()
    val saveProgress by model.saveProgress.collectAsStateWithLifecycle()
    val settings by model.settings.collectAsStateWithLifecycle()
    val gpuDrivers by model.gpuDrivers.collectAsStateWithLifecycle()
    val gpuDriverImportState by model.gpuDriverImportState.collectAsStateWithLifecycle()
    val gpuDriverMessage by model.gpuDriverMessage.collectAsStateWithLifecycle()
    val payloadState by model.payloadState.collectAsStateWithLifecycle()
    val updateState by model.updateState.collectAsStateWithLifecycle()
    var pendingGame by remember { mutableStateOf<SupportedGame?>(null) }
    var pendingSaveGame by remember { mutableStateOf<Pair<SupportedGame, InstalledGame>?>(null) }
    var page by remember { mutableStateOf<LauncherPage>(LauncherPage.Games) }

    val gameArchivePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val game = pendingGame
        pendingGame = null
        if (game != null && uri != null) model.install(game, uri)
    }
    val saveArchivePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val target = pendingSaveGame
        pendingSaveGame = null
        if (target != null && uri != null) model.importSave(target.first, target.second, uri)
    }
    val gpuDriverPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) model.importGpuDriver(uri)
    }

    fun chooseGameArchive(game: SupportedGame) {
        pendingGame = game
        gameArchivePicker.launch(arrayOf("application/zip", "application/x-zip-compressed"))
    }

    fun chooseSaveArchive(game: SupportedGame, ready: InstalledGame) {
        pendingSaveGame = game to ready
        saveArchivePicker.launch(arrayOf("application/zip", "application/x-zip-compressed"))
    }

    val selectedNavigation = when (page) {
        LauncherPage.Games,
        is LauncherPage.GameDetails,
        -> LauncherNavItem.GAMES
        LauncherPage.Guide -> LauncherNavItem.GUIDE
        LauncherPage.Settings -> LauncherNavItem.SETTINGS
    }

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background,
        contentColor = MaterialTheme.colorScheme.onBackground,
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.verticalGradient(
                        listOf(Color(0xFF171811), Color(0xFF0C0D0B)),
                    ),
                ),
        ) {
            if (payloadState !is PayloadBootstrapState.Ready) {
                PayloadBootstrapPage(
                    state = payloadState,
                    onRetry = model::retryPayloadBootstrap,
                )
                return@Column
            }
            LauncherHeader(
                selected = selectedNavigation,
                onGames = { page = LauncherPage.Games },
                onGuide = { page = LauncherPage.Guide },
                onSettings = { page = LauncherPage.Settings },
            )
            LauncherUpdateBanner(updateState, onOpenUpdate)
            AnimatedContent(
                targetState = page,
                modifier = Modifier.weight(1f),
                transitionSpec = {
                    (slideInHorizontally { it / 8 } + fadeIn()) togetherWith
                        (slideOutHorizontally { -it / 8 } + fadeOut()) using
                        SizeTransform(clip = false)
                },
                label = "launcher-page",
            ) { destination ->
                when (destination) {
                    LauncherPage.Games -> GamesPage(
                        installed = installed,
                        progress = progress,
                        saveProgress = saveProgress,
                        onSelectGame = { page = LauncherPage.GameDetails(it.id) },
                    )

                    is LauncherPage.GameDetails -> {
                        val game = GameCatalog.byId(destination.gameId)
                        if (game == null) {
                            page = LauncherPage.Games
                        } else {
                            GameDetailsPage(
                                game = game,
                                installed = installed[game.id],
                                progress = progress[game.id],
                                saveProgress = saveProgress[game.id],
                                onBack = { page = LauncherPage.Games },
                                onInstall = { chooseGameArchive(game) },
                                onImportSave = {
                                    installed[game.id]?.let { chooseSaveArchive(game, it) }
                                },
                                onPlay = { installed[game.id]?.let { onPlay(game, it) } },
                                onConfigure = {
                                    installed[game.id]?.let { onConfigure(game, it) }
                                },
                            )
                        }
                    }

                    LauncherPage.Guide -> GuidePage(
                        onBrowseGames = { page = LauncherPage.Games },
                    )

                    LauncherPage.Settings -> SettingsPage(
                        settings = settings,
                        installed = installed,
                        gpuDrivers = gpuDrivers,
                        gpuDriverImportState = gpuDriverImportState,
                        gpuDriverMessage = gpuDriverMessage,
                        onKeepAwake = model::setKeepScreenAwake,
                        onResolution = model::setResolution,
                        onGpuDriver = model::setGpuDriver,
                        onImportGpuDriver = {
                            gpuDriverPicker.launch(
                                arrayOf("application/zip", "application/x-zip-compressed"),
                            )
                        },
                    )
                }
            }
        }
    }
}
