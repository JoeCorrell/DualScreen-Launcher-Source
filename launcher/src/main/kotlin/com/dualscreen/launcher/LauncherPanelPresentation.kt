package com.dualscreen.launcher

import android.app.Presentation
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.Display
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView

/** Persistent lower-screen install console. Gameplay replaces it with the selected game's panel. */
class LauncherPanelPresentation(
    context: Context,
    display: Display,
) : Presentation(context, display) {
    private lateinit var summary: TextView
    private lateinit var badge: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var logView: TextView
    private lateinit var logScroll: ScrollView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window?.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        window?.decorView?.systemUiVisibility = FULLSCREEN_FLAGS
        setContentView(buildContent())
    }

    fun update(
        installed: Map<String, InstalledGame>,
        progress: Map<String, InstallProgress>,
        saveProgress: Map<String, SaveImportProgress>,
        launch: LaunchStatus?,
    ) {
        if (!::summary.isInitialized) return
        val state = latestInstallProgress(progress.values)
        val saveState = latestSaveImport(saveProgress.values)
        val game = state?.gameId?.let(GameCatalog::byId)
        val saveGame = saveState?.gameId?.let(GameCatalog::byId)
        val percent = installPercent(state?.fraction ?: 0f)
        val savePercent = installPercent(saveState?.fraction ?: 0f)
        val showSave = state?.running != true && saveState != null
        val showLaunch = state?.running != true && saveState == null && launch != null

        when {
            state?.running == true && game != null -> {
                summary.text = "${game.title} · ${state.stage.label} · $percent% overall"
                setBadge("WORKING", Color.rgb(244, 170, 67), Color.rgb(26, 20, 9))
                setProgress(state.fraction, Color.rgb(244, 170, 67))
            }
            showSave && saveState?.running == true && saveGame != null -> {
                summary.text = "${saveGame.title} · ${saveState.stage.label} · $savePercent% overall"
                setBadge("IMPORTING SAVE", Color.rgb(244, 170, 67), Color.rgb(26, 20, 9))
                setProgress(saveState.fraction, Color.rgb(244, 170, 67))
            }
            showSave && saveState?.error != null && saveGame != null -> {
                summary.text = "${saveGame.title} · save import stopped at $savePercent%"
                setBadge("SAVE ERROR", Color.rgb(151, 58, 47), Color.WHITE)
                setProgress(saveState.fraction, Color.rgb(214, 86, 68))
            }
            showSave && saveState?.stage == SaveImportStage.COMPLETE && saveGame != null -> {
                summary.text = "${saveGame.title} saves imported successfully"
                setBadge("SAVE COMPLETE", Color.rgb(65, 115, 74), Color.WHITE)
                setProgress(1f, Color.rgb(87, 157, 98))
            }
            showLaunch && launch?.failure != null -> {
                summary.text = "${launch.gameTitle} · launch failed · ${launch.stage}"
                setBadge("LAUNCH ERROR", Color.rgb(151, 58, 47), Color.WHITE)
                progressBar.visibility = View.INVISIBLE
            }
            showLaunch && launch?.running == true -> {
                summary.text = "${launch.gameTitle} · ${launch.stage.lowercase()}"
                setBadge("LAUNCHING", Color.rgb(244, 170, 67), Color.rgb(26, 20, 9))
                progressBar.visibility = View.INVISIBLE
            }
            showLaunch && launch != null -> {
                summary.text = "${launch.gameTitle} · ${launch.stage.lowercase()}"
                setBadge("SESSION", Color.rgb(65, 115, 74), Color.WHITE)
                progressBar.visibility = View.INVISIBLE
            }
            state?.error != null && game != null -> {
                summary.text = "${game.title} · stopped at $percent% overall"
                setBadge("ERROR", Color.rgb(151, 58, 47), Color.WHITE)
                setProgress(state.fraction, Color.rgb(214, 86, 68))
            }
            state?.stage == InstallStage.COMPLETE && game != null -> {
                summary.text = "${game.title} is ready to play"
                setBadge("COMPLETE", Color.rgb(65, 115, 74), Color.WHITE)
                setProgress(1f, Color.rgb(87, 157, 98))
            }
            else -> {
                summary.text = "Games preset ready · ${installed.size} of ${GameCatalog.games.size} installed"
                setBadge("READY", Color.rgb(65, 115, 74), Color.WHITE)
                progressBar.visibility = View.INVISIBLE
            }
        }

        val lines = when {
            showSave && saveState != null -> saveState.log
            showLaunch && launch != null -> launch.lines
            else -> state?.log?.takeIf { it.isNotEmpty() } ?: listOf(
            "[READY] Dualscreen Launcher started",
            "[OK] Exact WEMU Games preset is locked and available",
            "[OK] ${GameCatalog.games.size} verified dual-screen installers indexed",
            "[OK] Archive safety and payload hash checks enabled",
            "",
            "[WAIT] Choose ADD FILES on the Games screen to begin.",
            )
        }
        logView.text = buildString {
            if (showSave && saveState != null && saveGame != null) {
                appendLine("SESSION  ${saveGame.title.uppercase()}")
                appendLine("STAGE    ${saveState.stage.label.uppercase()}")
                appendLine("OVERALL  $savePercent%")
                appendLine("────────────────────────────────────────────────")
            } else if (showLaunch && launch != null) {
                appendLine("SESSION  ${launch.gameTitle.uppercase()}")
                appendLine("STAGE    ${launch.stage}")
                appendLine("LOG      ${launch.logFile}")
                appendLine("────────────────────────────────────────────────")
            } else if (game != null) {
                appendLine("SESSION  ${game.title.uppercase()}")
                appendLine("STAGE    ${state.stage.label.uppercase()}")
                appendLine("OVERALL  $percent%")
                appendLine("────────────────────────────────────────────────")
            }
            lines.forEach(::appendLine)
            // Some high-frequency byte updates intentionally do not create a
            // history entry. Always finish with the exact live overall value so
            // the newest console line and the progress bar cannot disagree.
            if (!showLaunch && !showSave) state?.let { current ->
                val liveLine = formatInstallLogLine(current.fraction, current.detail)
                if (lines.lastOrNull() != liveLine) appendLine(liveLine)
            }
            if (showSave) saveState?.error?.let {
                appendLine()
                appendLine("ERROR  $it")
            }
            if (!showLaunch && !showSave) state?.error?.let {
                appendLine()
                appendLine("ERROR  $it")
            }
        }.trimEnd()
        logScroll.post { logScroll.fullScroll(View.FOCUS_DOWN) }
    }

    private fun buildContent(): View {
        val root = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(34), dp(26), dp(34), dp(24))
            background = GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                intArrayOf(Color.rgb(27, 26, 18), Color.rgb(13, 13, 9)),
            )
        }

        val header = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val title = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            addView(label("BOTTOM SCREEN  ·  ACTIVITY", 12f, Color.rgb(154, 147, 130), Typeface.NORMAL))
            addView(label("Console", 29f, Color.WHITE, Typeface.BOLD))
        }
        header.addView(title, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        badge = label("READY", 12f, Color.WHITE, Typeface.BOLD).apply {
            gravity = Gravity.CENTER
            setPadding(dp(22), 0, dp(22), 0)
        }
        header.addView(badge, LinearLayout.LayoutParams(dp(148), dp(44)))
        root.addView(header)

        summary = label("Games preset ready", 14f, Color.rgb(181, 174, 158), Typeface.NORMAL)
        root.addView(summary, LinearLayout.LayoutParams(-1, dp(38)))
        progressBar = ProgressBar(context, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 1000
            visibility = View.INVISIBLE
            progressTintList = android.content.res.ColorStateList.valueOf(Color.rgb(244, 170, 67))
            progressBackgroundTintList = android.content.res.ColorStateList.valueOf(Color.rgb(56, 52, 37))
        }
        root.addView(progressBar, LinearLayout.LayoutParams(-1, dp(6)))

        val consoleFrame = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(18), dp(22), dp(18))
            background = rounded(Color.rgb(10, 10, 7), 14, Color.rgb(72, 67, 48))
        }
        consoleFrame.addView(label("LIVE ACTIVITY LOG", 11f, Color.rgb(244, 170, 67), Typeface.BOLD))
        logView = label("", 13f, Color.rgb(205, 199, 184), Typeface.NORMAL).apply {
            typeface = Typeface.MONOSPACE
            setTextIsSelectable(false)
            setLineSpacing(dp(3).toFloat(), 1f)
            setPadding(0, dp(14), 0, dp(8))
        }
        logScroll = ScrollView(context).apply {
            isVerticalScrollBarEnabled = true
            addView(logView, android.widget.FrameLayout.LayoutParams(-1, -2))
        }
        consoleFrame.addView(logScroll, LinearLayout.LayoutParams(-1, 0, 1f))
        root.addView(consoleFrame, LinearLayout.LayoutParams(-1, 0, 1f).apply {
            topMargin = dp(18)
            bottomMargin = 0
        })
        setBadge("READY", Color.rgb(65, 115, 74), Color.WHITE)
        return root
    }

    private fun setBadge(text: String, color: Int, textColor: Int) {
        badge.text = text
        badge.setTextColor(textColor)
        badge.background = rounded(color, 20)
    }

    private fun setProgress(fraction: Float, color: Int) {
        progressBar.visibility = View.VISIBLE
        progressBar.progress = (fraction * progressBar.max).toInt().coerceIn(0, progressBar.max)
        progressBar.progressTintList = android.content.res.ColorStateList.valueOf(color)
    }

    private fun label(text: String, size: Float, color: Int, style: Int) = TextView(context).apply {
        this.text = text
        textSize = size
        setTextColor(color)
        typeface = Typeface.create(Typeface.DEFAULT, style)
        gravity = Gravity.CENTER_VERTICAL
    }

    private fun rounded(color: Int, radius: Int, stroke: Int? = null) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radius).toFloat()
        stroke?.let { setStroke(dp(1), it) }
    }

    private fun dp(value: Int): Int = (value * context.resources.displayMetrics.density).toInt()

    private companion object {
        @Suppress("DEPRECATION")
        const val FULLSCREEN_FLAGS =
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
    }
}
