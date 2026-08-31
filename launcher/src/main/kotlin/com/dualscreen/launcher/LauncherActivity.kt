package com.dualscreen.launcher

import android.content.Context
import android.content.Intent
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.view.View
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import com.dualscreen.launcher.session.GameSessionActivity
import com.thor.core.display.DisplayTopology
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

class LauncherActivity : ComponentActivity() {
    private val model: LauncherViewModel by viewModels()
    private var lowerPanel: LauncherPanelPresentation? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        window.decorView.systemUiVisibility = FULLSCREEN_FLAGS
        setContent {
            LauncherTheme {
                LauncherScreen(
                    model = model,
                    onPlay = { game, installed -> launch(game, installed, configuration = false) },
                    onConfigure = { game, installed -> launch(game, installed, configuration = true) },
                )
            }
        }
        lifecycleScope.launch {
            model.installed.collectLatest { ready ->
                lowerPanel?.update(ready, model.progress.value, model.saveProgress.value, model.launchStatus.value)
            }
        }
        lifecycleScope.launch {
            model.progress.collectLatest { progress ->
                lowerPanel?.update(model.installed.value, progress, model.saveProgress.value, model.launchStatus.value)
            }
        }
        lifecycleScope.launch {
            model.saveProgress.collectLatest { saves ->
                lowerPanel?.update(model.installed.value, model.progress.value, saves, model.launchStatus.value)
            }
        }
        lifecycleScope.launch {
            model.launchStatus.collectLatest { launch ->
                lowerPanel?.update(model.installed.value, model.progress.value, model.saveProgress.value, launch)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        window.decorView.systemUiVisibility = FULLSCREEN_FLAGS
        showLowerPanel()
    }

    override fun onPause() {
        lowerPanel?.dismiss()
        lowerPanel = null
        super.onPause()
    }

    override fun onDestroy() {
        lowerPanel?.dismiss()
        lowerPanel = null
        super.onDestroy()
    }

    private fun showLowerPanel() {
        if (lowerPanel?.isShowing == true) return
        val manager = getSystemService(Context.DISPLAY_SERVICE) as? DisplayManager ?: return
        @Suppress("DEPRECATION")
        val mainDisplayId = windowManager.defaultDisplay.displayId
        val lowerDisplay = manager.displays.firstOrNull { candidate ->
            candidate.displayId != mainDisplayId && DisplayTopology.isUsableSecondary(candidate)
        } ?: return
        lowerPanel = LauncherPanelPresentation(
            context = this,
            display = lowerDisplay,
        ).also {
            it.show()
            it.update(model.installed.value, model.progress.value, model.saveProgress.value, model.launchStatus.value)
        }
    }

    private fun launch(
        game: SupportedGame,
        installed: InstalledGame,
        configuration: Boolean,
    ) {
        model.clearSaveImport()
        startActivity(
            Intent(this, GameSessionActivity::class.java)
                .putExtra(GameSessionActivity.EXTRA_GAME_ID, game.id)
                .putExtra(GameSessionActivity.EXTRA_CONTAINER_ID, installed.containerId)
                .putExtra(GameSessionActivity.EXTRA_SHORTCUT_PATH, installed.shortcutPath)
                .putExtra(GameSessionActivity.EXTRA_CONFIGURATION_LAUNCHER, configuration),
        )
    }

    companion object {
        @Suppress("DEPRECATION")
        private const val FULLSCREEN_FLAGS =
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
    }
}

private sealed interface LauncherPage {
    data object Library : LauncherPage
    data object Settings : LauncherPage
}

@Composable
private fun LauncherTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = androidx.compose.material3.darkColorScheme(
            primary = Color(0xFFF4AA43),
            onPrimary = Color(0xFF17120A),
            background = Color(0xFF11100B),
            onBackground = Color(0xFFF1EEE6),
            surface = Color(0xFF191811),
            surfaceVariant = Color(0xFF242218),
            onSurface = Color(0xFFF1EEE6),
            onSurfaceVariant = Color(0xFFA39D8F),
            error = Color(0xFFFF8A80),
        ),
        content = content,
    )
}

@Composable
private fun LauncherScreen(
    model: LauncherViewModel,
    onPlay: (SupportedGame, InstalledGame) -> Unit,
    onConfigure: (SupportedGame, InstalledGame) -> Unit,
) {
    val installed by model.installed.collectAsStateWithLifecycle()
    val progress by model.progress.collectAsStateWithLifecycle()
    val saveProgress by model.saveProgress.collectAsStateWithLifecycle()
    val settings by model.settings.collectAsStateWithLifecycle()
    val gpuDrivers by model.gpuDrivers.collectAsStateWithLifecycle()
    val gpuDriverImportState by model.gpuDriverImportState.collectAsStateWithLifecycle()
    val gpuDriverMessage by model.gpuDriverMessage.collectAsStateWithLifecycle()
    var pendingGame by remember { mutableStateOf<SupportedGame?>(null) }
    var pendingSaveGame by remember { mutableStateOf<Pair<SupportedGame, InstalledGame>?>(null) }
    var page by remember { mutableStateOf<LauncherPage>(LauncherPage.Library) }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val game = pendingGame
        pendingGame = null
        if (game != null && uri != null) {
            model.install(game, uri)
        }
    }
    val gpuDriverPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) model.importGpuDriver(uri)
    }
    val savePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val target = pendingSaveGame
        pendingSaveGame = null
        if (target != null && uri != null) model.importSave(target.first, target.second, uri)
    }

    fun chooseArchive(game: SupportedGame) {
        pendingGame = game
        picker.launch(arrayOf("application/zip", "application/x-zip-compressed"))
    }

    fun chooseSaveArchive(game: SupportedGame, installedGame: InstalledGame) {
        pendingSaveGame = game to installedGame
        savePicker.launch(arrayOf("application/zip", "application/x-zip-compressed"))
    }

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background,
        contentColor = MaterialTheme.colorScheme.onBackground,
    ) {
        AnimatedContent(targetState = page, label = "launcher-page") { destination ->
            when (destination) {
                LauncherPage.Library -> LibraryPage(
                    installed = installed,
                    progress = progress,
                    saveProgress = saveProgress,
                    onInstall = ::chooseArchive,
                    onImportSave = ::chooseSaveArchive,
                    onPlay = onPlay,
                    onConfigure = onConfigure,
                    onSettings = { page = LauncherPage.Settings },
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
                    onBack = { page = LauncherPage.Library },
                )
            }
        }
    }
}

@Composable
private fun LibraryPage(
    installed: Map<String, InstalledGame>,
    progress: Map<String, InstallProgress>,
    saveProgress: Map<String, SaveImportProgress>,
    onInstall: (SupportedGame) -> Unit,
    onImportSave: (SupportedGame, InstalledGame) -> Unit,
    onPlay: (SupportedGame, InstalledGame) -> Unit,
    onConfigure: (SupportedGame, InstalledGame) -> Unit,
    onSettings: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(Color(0xFF1A1911), Color(0xFF0E0D09))))
            .padding(horizontal = 24.dp, vertical = 14.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text("DUALSCREEN LAUNCHER", fontSize = 10.sp, letterSpacing = 2.5.sp, color = Color(0xFFF4AA43))
                Text("Games", fontSize = 28.sp, fontWeight = FontWeight.Bold)
            }
            OutlinedButton(
                onClick = onSettings,
                modifier = Modifier.height(38.dp),
            ) {
                Icon(Icons.Default.Settings, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(6.dp))
                Text("Settings", fontSize = 12.sp)
            }
        }
        Spacer(Modifier.height(10.dp))
        LazyRow(
            modifier = Modifier.fillMaxWidth().weight(1f),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.Top,
        ) {
            items(GameCatalog.games, key = { it.id }) { game ->
                GameCard(
                    game = game,
                    installed = installed[game.id],
                    progress = progress[game.id],
                    saveProgress = saveProgress[game.id],
                    onInstall = { onInstall(game) },
                    onImportSave = { installed[game.id]?.let { onImportSave(game, it) } },
                    onPlay = { installed[game.id]?.let { onPlay(game, it) } },
                    onConfigure = { installed[game.id]?.let { onConfigure(game, it) } },
                )
            }
        }
    }
}

@Composable
private fun GameCard(
    game: SupportedGame,
    installed: InstalledGame?,
    progress: InstallProgress?,
    saveProgress: SaveImportProgress?,
    onInstall: () -> Unit,
    onImportSave: () -> Unit,
    onPlay: () -> Unit,
    onConfigure: () -> Unit,
) {
    val installing = progress?.running == true
    val importingSave = saveProgress?.running == true
    val running = installing || importingSave
    val activeFraction = if (importingSave) saveProgress?.fraction ?: 0f else progress?.fraction ?: 0f
    Card(
        colors = CardDefaults.cardColors(containerColor = Color(0xFF17160F)),
        shape = RoundedCornerShape(14.dp),
        modifier = Modifier
            .width(144.dp)
            .height(332.dp)
            .border(1.dp, Color(0xFF484330), RoundedCornerShape(14.dp)),
    ) {
        Column(Modifier.fillMaxSize()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    // Leave a fixed action bay for three vertically stacked
                    // controls on every card. Artwork remains fully visible via
                    // ContentScale.Fit rather than being cropped to make room.
                    .height(166.dp)
                    .background(Color.Black),
            ) {
                Image(
                    painter = painterResource(gameArtwork(game.id)),
                    contentDescription = null,
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxSize(),
                )
                if (game.isBeta) {
                    Text(
                        text = "BETA",
                        modifier = Modifier
                            .align(Alignment.TopEnd)
                            .padding(7.dp)
                            .background(Color(0xFFE8A33E), RoundedCornerShape(6.dp))
                            .padding(horizontal = 7.dp, vertical = 3.dp),
                        color = Color(0xFF17120A),
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 8.sp,
                    )
                }
            }
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(Color(0xFF17160F))
                    .padding(horizontal = 8.dp, vertical = 7.dp),
                verticalArrangement = Arrangement.spacedBy(3.dp),
            ) {
                if (running) {
                    LinearProgressIndicator(
                        progress = { activeFraction },
                        modifier = Modifier.fillMaxWidth().height(3.dp),
                        color = Color(0xFFF4AA43),
                        trackColor = Color(0xFF3A3525),
                    )
                } else {
                    // Keep all cards identical when the progress indicator appears.
                    Spacer(Modifier.fillMaxWidth().height(3.dp))
                }
                Text(
                    gameCardTitle(game.id),
                    modifier = Modifier.fillMaxWidth().height(32.dp),
                    fontSize = 13.sp,
                    lineHeight = 15.sp,
                    fontWeight = FontWeight.Bold,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    when {
                        importingSave -> "IMPORTING SAVE  ·  ${installPercent(activeFraction)}%"
                        installing -> "${progress?.stage?.name}  ·  ${installPercent(activeFraction)}%"
                        saveProgress?.error != null -> "SAVE IMPORT FAILED"
                        progress?.error != null -> "VERIFY FAILED"
                        installed != null -> "READY  ·  MOD ACTIVE"
                        else -> "NEEDS ZIP"
                    },
                    color = when {
                        saveProgress?.error != null || progress?.error != null -> Color(0xFFE46C56)
                        running -> Color(0xFFF4AA43)
                        else -> Color(0xFF9A9382)
                    },
                    fontFamily = FontFamily.Monospace,
                    fontSize = 9.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(3.dp),
                ) {
                    GameCardAction(
                        label = when {
                            importingSave -> "IMPORTING SAVE"
                            installing -> "INSTALLING"
                            installed != null -> "PLAY"
                            progress?.error != null -> "RETRY ZIP"
                            else -> "ADD FILES"
                        },
                        onClick = if (installed != null) onPlay else onInstall,
                        enabled = !running,
                        primary = installed != null && !running,
                    )
                    GameCardAction(
                        label = "OPEN LAUNCHER",
                        onClick = onConfigure,
                        enabled = !running && installed != null &&
                            game.configurationLauncherCandidates.isNotEmpty(),
                    )
                    GameCardAction(
                        label = "IMPORT SAVE ZIP",
                        onClick = onImportSave,
                        enabled = !running && installed != null,
                    )
                }
            }
        }
    }
}

/** A compact fixed-height card action with no Material minimum-size inflation. */
@Composable
private fun GameCardAction(
    label: String,
    onClick: () -> Unit,
    enabled: Boolean,
    primary: Boolean = false,
) {
    val shape = RoundedCornerShape(7.dp)
    val fill = when {
        !enabled -> Color(0xFF1C1B16)
        primary -> Color(0xFFF4AA43)
        else -> Color.Transparent
    }
    val outline = when {
        !enabled -> Color(0xFF302E26)
        primary -> Color.Transparent
        else -> Color(0xFF615B49)
    }
    val content = when {
        !enabled -> Color(0xFF625E54)
        primary -> Color(0xFF17120A)
        else -> Color(0xFFE7E1D3)
    }
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(30.dp)
            .clip(shape)
            .background(fill)
            .border(1.dp, outline, shape)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = content,
            fontSize = 7.5.sp,
            lineHeight = 9.sp,
            maxLines = 1,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
        )
    }
}

@Composable
private fun SettingsPage(
    settings: LauncherSettings,
    installed: Map<String, InstalledGame>,
    gpuDrivers: List<LauncherGpuDriver>,
    gpuDriverImportState: GpuDriverImportState,
    gpuDriverMessage: String?,
    onKeepAwake: (Boolean) -> Unit,
    onResolution: (GameResolution) -> Unit,
    onGpuDriver: (String) -> Unit,
    onImportGpuDriver: () -> Unit,
    onBack: () -> Unit,
) {
    Column(
        Modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(Color(0xFF1A1911), Color(0xFF0E0D09))))
            .padding(horizontal = 42.dp, vertical = 30.dp),
    ) {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text("Dualscreen Launcher", fontSize = 13.sp, letterSpacing = 2.sp, color = Color(0xFFF4AA43))
                Text("Settings", fontSize = 34.sp, fontWeight = FontWeight.SemiBold)
                Text(
                    "Launcher behavior and verified installation status",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            OutlinedButton(onClick = onBack) { Text("Games") }
        }
        Spacer(Modifier.height(24.dp))
        LazyColumn(
            modifier = Modifier.fillMaxWidth().weight(1f).widthIn(max = 1050.dp).align(Alignment.CenterHorizontally),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            item {
                SettingsCard("Launcher behavior") {
                    SettingsSwitch(
                        title = "Keep display awake while playing",
                        detail = "Prevents the screens from sleeping during a game session.",
                        checked = settings.keepScreenAwake,
                        onCheckedChange = onKeepAwake,
                    )
                    Spacer(Modifier.height(14.dp))
                    Text(
                        "Game resolution",
                        fontSize = 16.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        "Applied to both the Windows container and the game before launch.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 12.sp,
                    )
                    Spacer(Modifier.height(10.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        GameResolution.entries.forEach { resolution ->
                            if (settings.resolution == resolution) {
                                Button(
                                    onClick = { onResolution(resolution) },
                                    contentPadding = PaddingValues(horizontal = 18.dp, vertical = 0.dp),
                                    modifier = Modifier.height(42.dp),
                                ) {
                                    Text(resolution.label, fontSize = 12.sp)
                                }
                            } else {
                                OutlinedButton(
                                    onClick = { onResolution(resolution) },
                                    contentPadding = PaddingValues(horizontal = 18.dp, vertical = 0.dp),
                                    modifier = Modifier.height(42.dp),
                                ) {
                                    Text(resolution.label, fontSize = 12.sp)
                                }
                            }
                        }
                    }
                }
            }
            item {
                SettingsCard("GPU driver") {
                    Text(
                        "Choose a bundled or imported AdrenoTools driver. The selection is applied to the tested container without rebuilding its Wine prefix.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 12.sp,
                    )
                    if (gpuDrivers.isEmpty()) {
                        Text("Reading installed drivers...", color = MaterialTheme.colorScheme.onSurfaceVariant)
                    } else {
                        LazyRow(
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            contentPadding = PaddingValues(vertical = 2.dp),
                        ) {
                            items(gpuDrivers, key = { it.id }) { driver ->
                                val label = when (driver.source) {
                                    LauncherGpuDriver.Source.SYSTEM -> driver.label
                                    LauncherGpuDriver.Source.BUNDLED -> "${driver.label} · bundled"
                                    LauncherGpuDriver.Source.UPLOADED -> "${driver.label} · uploaded"
                                }
                                if (settings.gpuDriverId == driver.id) {
                                    Button(
                                        onClick = { onGpuDriver(driver.id) },
                                        modifier = Modifier.height(42.dp),
                                        contentPadding = PaddingValues(horizontal = 16.dp),
                                    ) { Text(label, fontSize = 12.sp) }
                                } else {
                                    OutlinedButton(
                                        onClick = { onGpuDriver(driver.id) },
                                        modifier = Modifier.height(42.dp),
                                        contentPadding = PaddingValues(horizontal = 16.dp),
                                    ) { Text(label, fontSize = 12.sp) }
                                }
                            }
                        }
                    }
                    OutlinedButton(
                        onClick = onImportGpuDriver,
                        enabled = gpuDriverImportState !is GpuDriverImportState.Importing,
                        modifier = Modifier.height(42.dp),
                    ) {
                        Text(
                            if (gpuDriverImportState is GpuDriverImportState.Importing) {
                                "Validating driver..."
                            } else {
                                "Upload GPU driver ZIP"
                            },
                        )
                    }
                    when (gpuDriverImportState) {
                        is GpuDriverImportState.Failed -> Text(
                            gpuDriverImportState.message,
                            color = MaterialTheme.colorScheme.error,
                            fontSize = 12.sp,
                        )
                        is GpuDriverImportState.Installed -> Text(
                            "Imported ${gpuDriverImportState.driver.label}.",
                            color = MaterialTheme.colorScheme.primary,
                            fontSize = 12.sp,
                        )
                        GpuDriverImportState.Idle,
                        GpuDriverImportState.Importing,
                        -> Unit
                    }
                    gpuDriverMessage?.let { message ->
                        Text(message, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp)
                    }
                }
            }
            item {
                SettingsCard("Installed games") {
                    SettingValue("Ready to play", "${installed.size} of ${GameCatalog.games.size}")
                    SettingValue("Game data in managed storage", formatBytes(installed.values.sumOf { it.installedBytes }))
                    SettingValue("Location", "Private launcher storage · not exposed to other apps")
                }
            }
            item {
                SettingsCard("Locked tested environment") {
                    Text(
                        "Every game uses the captured WEMU Games preset. Only the display resolution and GPU driver can be changed.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    SettingValue("Proton", "Proton 11.0-5 ARM64EC 6")
                    SettingValue("FEX", "2608 stable 0")
                    SettingValue("Graphics", "DXVK 3.0-1 GPLAsync · VKD3D 2.9 · Turnip 26.2.0")
                }
            }
        }
    }
}

@Composable
private fun SettingsCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface,
            contentColor = MaterialTheme.colorScheme.onSurface,
        ),
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(22.dp),
            verticalArrangement = Arrangement.spacedBy(13.dp),
            content = content,
        )
    }
}

@Composable
private fun SettingsSwitch(
    title: String,
    detail: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f).padding(end = 20.dp)) {
            Text(title, fontWeight = FontWeight.Medium)
            Text(detail, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 13.sp)
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

@Composable
private fun SettingValue(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, fontWeight = FontWeight.Medium)
    }
}

internal fun gameArtwork(gameId: String): Int = when (gameId) {
    "skyrim-se" -> R.drawable.art_skyrim
    "fallout3" -> R.drawable.art_fallout3
    "fallout4" -> R.drawable.art_fallout4
    "grim-dawn" -> R.drawable.art_grim_dawn
    "deus-ex-hrdc" -> R.drawable.art_deus_ex
    "dead-island-de" -> R.drawable.art_dead_island
    "valheim" -> R.drawable.art_valheim
    "stardew-valley" -> R.drawable.art_stardew
    else -> R.drawable.ic_launcher
}

private fun gameCardTitle(gameId: String): String = when (gameId) {
    "skyrim-se" -> "Skyrim Special Edition"
    "fallout3" -> "Fallout 3"
    "fallout4" -> "Fallout 4"
    "grim-dawn" -> "Grim Dawn"
    "deus-ex-hrdc" -> "Deus Ex: Human Revolution"
    "dead-island-de" -> "Dead Island Definitive"
    "valheim" -> "Valheim"
    "stardew-valley" -> "Stardew Valley"
    else -> gameId
}

private fun formatBytes(bytes: Long): String {
    if (bytes <= 0) return "0 B"
    val units = listOf("B", "KB", "MB", "GB", "TB")
    var value = bytes.toDouble()
    var index = 0
    while (value >= 1024 && index < units.lastIndex) {
        value /= 1024
        index++
    }
    return if (index == 0) "${value.toLong()} ${units[index]}"
    else "%.1f %s".format(value, units[index])
}
