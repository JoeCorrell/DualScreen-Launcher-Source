package com.dualscreen.launcher

import android.content.Context
import android.content.Intent
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.view.View
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
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

@Composable
private fun LauncherTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = androidx.compose.material3.darkColorScheme(
            primary = Color(0xFFF4AA43),
            onPrimary = Color(0xFF17120A),
            background = Color(0xFF0D0E0C),
            onBackground = Color(0xFFF1EEE6),
            surface = Color(0xFF171812),
            surfaceVariant = Color(0xFF22231B),
            onSurface = Color(0xFFF1EEE6),
            onSurfaceVariant = Color(0xFFA8A397),
            outline = Color(0xFF49483C),
            error = Color(0xFFFF8A80),
        ),
        content = content,
    )
}
