package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.os.Process
import android.view.MotionEvent
import android.view.View
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log

internal class ArkhamCompanionView(context: Context) : View(context) {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val SNAPSHOT_SIZE = 480
        private const val SNAPSHOT_MAGIC = 0x4D4B5241
        private const val POLL_INTERVAL_MS = 250L
        private const val ACTIVE_STATUS = 5
        private const val MAP_AVAILABLE = 1 shl 4
        private val TABS = arrayOf("OVERVIEW", "GADGETS", "MAP", "SKILLS", "CASE FILE")
        private val GADGETS = arrayOf(
            "Batarang",
            "Batclaw",
            "Explosive Gel",
            "Cryptographic Sequencer",
            "Line Launcher",
            "Remote Control Batarang",
            "Sonic Batarang",
            "Triple Batarang"
        )
        private val SKILLS = arrayOf(
            "Health Upgrade I",
            "Health Upgrade II",
            "Health Upgrade III",
            "Health Upgrade IV",
            "Special Combo Throw",
            "Remote Control Batarang",
            "Batarang Power",
            "Twin Batarang",
            "Triple Batarang",
            "Combo Batarang",
            "Critical Combo Strikes",
            "Special Combo Takedown",
            "Inverted Takedown",
            "Auto Proximity Detonation",
            "Proximity Mine",
            "Cryptographic Power Amplifier",
            "Cryptographic Range Amplifier",
            "Sonic Batarang",
            "Sonic Shock Batarang",
            "FreeFlow Focus"
        )
    }

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val path = Path()
    private val regular = Typeface.create("sans-serif-condensed", Typeface.NORMAL)
    private val medium = Typeface.create("sans-serif-condensed", Typeface.BOLD)
    private val frameLock = Any()
    private val snapshot = ByteBuffer.allocateDirect(SNAPSHOT_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    private val tabBounds = Array(TABS.size) { RectF() }
    private var poller: ScheduledExecutorService? = null
    private var backgroundShader: LinearGradient? = null
    private var selectedTab = 0
    private var pressedTab = -1
    private var status = 0
    private var availability = 0
    private var stage = 0
    private var health = 0
    private var maxHealth = 0
    private var xp = 0f
    private var xpPrevious = 0f
    private var xpNext = 0f
    private var xpLevel = 0f
    private var playerLevel = 0
    private var storyPercentage = 0
    private var unlockables = 0
    private var currentGadget = -1
    private var gadgetMask = 0
    private var upgradesBought = 0
    private var totalTime = 0f
    private var playerX = 0f
    private var playerY = 0f
    private var playerZ = 0f
    private var playerYaw = 0f
    private var locationId = 0
    private var saveChapter = 0
    private var gnamesCount = 0
    private var gobjectsCount = 0
    private val gadgetLevels = IntArray(16)
    private val skillStates = IntArray(20)
    private var objective = ""
    private var description = ""
    private var location = ""
    private var lastContentHash = 0
    private var mapZoom = 1f
    private var mapPanX = 0f
    private var mapPanY = 0f
    private var touchX = 0f
    private var touchY = 0f
    private var lastPinchDistance = 0f
    private var mapDragging = false

    init {
        isClickable = true
        isFocusable = true
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (poller != null) return
        poller = Executors.newSingleThreadScheduledExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                },
                "Arkham-Companion-Telemetry"
            ).apply {
                isDaemon = true
                priority = Thread.NORM_PRIORITY - 1
            }
        }.also { executor ->
            executor.scheduleWithFixedDelay(
                {
                    try {
                        pollSnapshot()
                    } catch (error: Throwable) {
                        Log.error("[Arkham Companion] Snapshot poll failed: ${error.message}")
                    }
                },
                0,
                POLL_INTERVAL_MS,
                TimeUnit.MILLISECONDS
            )
        }
    }

    override fun onDetachedFromWindow() {
        poller?.shutdownNow()
        poller = null
        super.onDetachedFromWindow()
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        super.onSizeChanged(width, height, oldWidth, oldHeight)
        backgroundShader = LinearGradient(
            0f,
            0f,
            width.toFloat(),
            height.toFloat(),
            intArrayOf(Color.rgb(5, 10, 14), Color.rgb(13, 24, 28), Color.rgb(3, 7, 10)),
            floatArrayOf(0f, .52f, 1f),
            Shader.TileMode.CLAMP
        )
    }

    private fun pollSnapshot() {
        snapshot.clear()
        if (NativeLibrary.fillArkhamCompanionSnapshot(snapshot) != SNAPSHOT_SIZE) return
        if (snapshot.getInt(0) != SNAPSHOT_MAGIC || snapshot.getShort(4).toInt() != 1) return
        var hash = 17
        for (index in 12 until SNAPSHOT_SIZE) hash = hash * 31 + snapshot.get(index)
        if (hash == lastContentHash) return
        synchronized(frameLock) {
            status = snapshot.getInt(12)
            availability = snapshot.getInt(16)
            stage = snapshot.getInt(20)
            health = snapshot.getInt(24)
            maxHealth = snapshot.getInt(28)
            xp = snapshot.getFloat(36)
            xpPrevious = snapshot.getFloat(40)
            xpNext = snapshot.getFloat(44)
            xpLevel = snapshot.getFloat(52)
            playerLevel = snapshot.getInt(56)
            storyPercentage = snapshot.getInt(60)
            unlockables = snapshot.getInt(64)
            currentGadget = snapshot.getInt(68)
            gadgetMask = snapshot.getInt(72)
            upgradesBought = snapshot.getInt(76)
            totalTime = snapshot.getFloat(80)
            playerX = snapshot.getFloat(84)
            playerY = snapshot.getFloat(88)
            playerZ = snapshot.getFloat(92)
            playerYaw = snapshot.getFloat(96)
            locationId = snapshot.getInt(100)
            saveChapter = snapshot.getInt(104)
            gnamesCount = snapshot.getInt(108)
            gobjectsCount = snapshot.getInt(112)
            for (index in gadgetLevels.indices) {
                gadgetLevels[index] = snapshot.get(120 + index).toInt() and 0xFF
            }
            for (index in skillStates.indices) {
                skillStates[index] = snapshot.get(136 + index).toInt() and 0xFF
            }
            objective = readCString(160, 96)
            description = readCString(256, 160)
            location = readCString(416, 64)
            lastContentHash = hash
        }
        postInvalidate()
    }

    private fun readCString(offset: Int, length: Int): String {
        val bytes = ByteArray(length)
        for (index in bytes.indices) bytes[index] = snapshot.get(offset + index)
        val end = bytes.indexOf(0).let { if (it < 0) bytes.size else it }
        return bytes.copyOf(end).toString(Charsets.UTF_8).trim()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        paint.shader = backgroundShader
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        paint.shader = null
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val offsetX = (width - DESIGN_WIDTH * scale) * .5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * .5f
        canvas.save()
        canvas.translate(offsetX, offsetY)
        canvas.scale(scale, scale)
        drawTexture(canvas)
        synchronized(frameLock) {
            drawHeader(canvas)
            if (status == ACTIVE_STATUS) {
                drawNavigation(canvas)
                when (selectedTab) {
                    0 -> drawOverview(canvas)
                    1 -> drawGadgets(canvas)
                    2 -> drawMap(canvas)
                    3 -> drawSkills(canvas)
                    else -> drawCaseFile(canvas)
                }
            } else {
                drawWaiting(canvas)
            }
        }
        canvas.restore()
    }

    private fun drawTexture(canvas: Canvas) {
        stroke.strokeWidth = 1f
        stroke.color = Color.argb(20, 184, 218, 216)
        for (x in 0..1240 step 40) canvas.drawLine(x.toFloat(), 0f, x.toFloat(), 1080f, stroke)
        for (y in 0..1080 step 40) canvas.drawLine(0f, y.toFloat(), 1240f, y.toFloat(), stroke)
        paint.color = Color.argb(20, 235, 175, 68)
        for (index in 0..22) {
            val x = ((index * 173) % 1210).toFloat()
            val y = ((index * 97) % 1050).toFloat()
            canvas.drawCircle(x, y, 1.5f, paint)
        }
    }

    private fun drawHeader(canvas: Canvas) {
        paint.color = Color.rgb(5, 9, 12)
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, 118f, paint)
        paint.color = Color.rgb(216, 166, 68)
        canvas.drawRect(0f, 114f, DESIGN_WIDTH, 118f, paint)
        drawBatMark(canvas, 68f, 57f, 48f, Color.rgb(220, 173, 76))
        text(canvas, "WAYNETECH", 132f, 50f, 22f, Color.rgb(150, 163, 164), false)
        text(canvas, "ARKHAM FIELD TERMINAL", 132f, 87f, 36f, Color.WHITE, true)
        val chapter = if (saveChapter > 0) "CHAPTER ${saveChapter.toString().padStart(2, '0')}" else "LIVE FIELD DATA"
        text(canvas, chapter, 1176f, 48f, 19f, Color.rgb(163, 174, 173), false, Paint.Align.RIGHT)
        val pulse = if (status == ACTIVE_STATUS) Color.rgb(103, 214, 159) else Color.rgb(216, 166, 68)
        paint.color = pulse
        canvas.drawCircle(1163f, 77f, 6f, paint)
        text(canvas, if (status == ACTIVE_STATUS) "LINK ACTIVE" else "LINKING", 1148f, 84f, 18f, pulse, true, Paint.Align.RIGHT)
    }

    private fun drawNavigation(canvas: Canvas) {
        val left = 24f
        val width = (DESIGN_WIDTH - 48f) / TABS.size
        for (index in TABS.indices) {
            val bounds = tabBounds[index]
            bounds.set(left + index * width, 135f, left + (index + 1) * width - 8f, 220f)
            val selected = index == selectedTab
            paint.color = when {
                selected -> Color.rgb(211, 163, 67)
                pressedTab == index -> Color.rgb(40, 53, 55)
                else -> Color.rgb(16, 27, 31)
            }
            canvas.drawRect(bounds, paint)
            stroke.color = if (selected) Color.rgb(246, 202, 101) else Color.rgb(55, 72, 75)
            stroke.strokeWidth = if (selected) 3f else 2f
            canvas.drawRect(bounds, stroke)
            drawTabIcon(canvas, index, bounds.left + 32f, bounds.centerY(), selected)
            text(
                canvas,
                TABS[index],
                bounds.left + 62f,
                bounds.centerY() + 8f,
                22f,
                if (selected) Color.rgb(10, 15, 17) else Color.rgb(206, 215, 214),
                true
            )
        }
    }

    private fun drawWaiting(canvas: Canvas) {
        val title = when (status) {
            2 -> "UNSUPPORTED ARKHAM BUILD"
            3 -> "CONNECTING TO WAYNETECH"
            4 -> "WAITING FOR SAVE DATA"
            else -> "WAITING FOR BATMAN: ARKHAM ASYLUM"
        }
        val detail = when (status) {
            2 -> "Install the supported 1.0.1 update before starting the game."
            3 -> "Resolving game systems securely · stage $stage of 6"
            4 -> "Load or begin a save. The field terminal activates after Batman enters the game."
            else -> "Start Arkham Asylum on the main display."
        }
        drawBatMark(canvas, DESIGN_WIDTH * .5f, 430f, 118f, Color.rgb(203, 155, 61))
        text(canvas, title, DESIGN_WIDTH * .5f, 590f, 42f, Color.WHITE, true, Paint.Align.CENTER)
        text(canvas, detail, DESIGN_WIDTH * .5f, 645f, 24f, Color.rgb(164, 178, 178), false, Paint.Align.CENTER)
        val progress = (stage.coerceIn(0, 6) / 6f)
        drawBar(canvas, RectF(360f, 692f, 880f, 708f), progress, Color.rgb(210, 161, 65))
        if (stage > 0) {
            text(
                canvas,
                "$gnamesCount names · $gobjectsCount live objects",
                DESIGN_WIDTH * .5f,
                752f,
                19f,
                Color.rgb(105, 124, 126),
                false,
                Paint.Align.CENTER
            )
        }
    }

    private fun drawOverview(canvas: Canvas) {
        sectionTitle(canvas, "TACTICAL OVERVIEW", "LIVE STATUS AND CASE PROGRESS")
        drawHealthCard(canvas, RectF(24f, 294f, 594f, 486f))
        drawXpCard(canvas, RectF(24f, 505f, 594f, 696f))
        drawProgressCard(canvas, RectF(24f, 715f, 594f, 1038f))
        drawObjectiveCard(canvas, RectF(615f, 294f, 1216f, 650f))
        drawEquipmentCard(canvas, RectF(615f, 669f, 1216f, 1038f))
    }

    private fun drawHealthCard(canvas: Canvas, bounds: RectF) {
        card(canvas, bounds, "VITAL SIGNS", "BIOMETRIC FEED")
        drawHeart(canvas, bounds.left + 72f, bounds.top + 108f, 34f, Color.rgb(194, 51, 55))
        val value = if (maxHealth > 0) health.toFloat() / maxHealth else health.coerceIn(0, 100) / 100f
        drawBar(canvas, RectF(bounds.left + 130f, bounds.top + 86f, bounds.right - 35f, bounds.top + 119f), value, Color.rgb(190, 52, 55))
        val label = if (maxHealth > 0) "$health / $maxHealth" else health.toString()
        text(canvas, label, bounds.right - 35f, bounds.top + 160f, 32f, Color.WHITE, true, Paint.Align.RIGHT)
        text(canvas, "CURRENT HEALTH", bounds.left + 130f, bounds.top + 158f, 18f, Color.rgb(133, 151, 151), false)
    }

    private fun drawXpCard(canvas: Canvas, bounds: RectF) {
        card(canvas, bounds, "EXPERIENCE", "COMBAT ANALYSIS")
        val range = max(1f, xpNext - xpPrevious)
        val progress = ((xp - xpPrevious) / range).coerceIn(0f, 1f)
        drawBar(canvas, RectF(bounds.left + 34f, bounds.top + 92f, bounds.right - 34f, bounds.top + 121f), progress, Color.rgb(211, 162, 65))
        text(canvas, "LEVEL ${if (playerLevel > 0) playerLevel else xpLevel.toInt()}", bounds.left + 34f, bounds.top + 164f, 29f, Color.WHITE, true)
        text(canvas, "${xp.toInt()} XP", bounds.right - 34f, bounds.top + 164f, 27f, Color.rgb(225, 181, 89), true, Paint.Align.RIGHT)
    }

    private fun drawProgressCard(canvas: Canvas, bounds: RectF) {
        card(canvas, bounds, "PROFILE", "ARKHAM INCIDENT")
        statRow(canvas, bounds, 104f, "STORY COMPLETE", "${storyPercentage.coerceIn(0, 100)}%")
        statRow(canvas, bounds, 164f, "UPGRADES ACQUIRED", upgradesBought.toString())
        statRow(canvas, bounds, 224f, "UPGRADES AVAILABLE", unlockables.coerceAtLeast(0).toString())
        statRow(canvas, bounds, 284f, "FIELD TIME", formatDuration(totalTime))
    }

    private fun drawObjectiveCard(canvas: Canvas, bounds: RectF) {
        card(canvas, bounds, "CURRENT OBJECTIVE", "ORACLE CASE FEED")
        text(canvas, objective.ifBlank { "Objective data is synchronizing" }, bounds.left + 32f, bounds.top + 108f, 31f, Color.rgb(231, 187, 92), true)
        drawWrappedText(
            canvas,
            description.ifBlank { "The current case description will appear when the save publishes its active objective." },
            bounds.left + 32f,
            bounds.top + 158f,
            bounds.width() - 64f,
            25f,
            35f,
            Color.rgb(205, 215, 213),
            5
        )
    }

    private fun drawEquipmentCard(canvas: Canvas, bounds: RectF) {
        card(canvas, bounds, "EQUIPMENT STATUS", "SELECTED GADGET")
        val index = currentGadget.takeIf { it in GADGETS.indices } ?: 0
        drawGadgetIcon(canvas, index, bounds.left + 125f, bounds.centerY() + 10f, 76f, Color.rgb(221, 176, 81))
        text(canvas, if (currentGadget in GADGETS.indices) GADGETS[currentGadget] else "No gadget selected", bounds.left + 235f, bounds.top + 145f, 34f, Color.WHITE, true)
        text(canvas, "READY FOR DEPLOYMENT", bounds.left + 235f, bounds.top + 187f, 20f, Color.rgb(105, 208, 157), true)
        val owned = Integer.bitCount(gadgetMask)
        text(canvas, "$owned SYSTEMS ONLINE", bounds.left + 235f, bounds.top + 242f, 21f, Color.rgb(151, 167, 166), false)
        text(canvas, location.ifBlank { "Location ID $locationId" }, bounds.left + 235f, bounds.top + 287f, 20f, Color.rgb(151, 167, 166), false)
    }

    private fun drawGadgets(canvas: Canvas) {
        sectionTitle(canvas, "GADGET ARRAY", "LIVE INVENTORY AND UPGRADE STATUS")
        for (index in GADGETS.indices) {
            val column = index % 4
            val row = index / 4
            val bounds = RectF(24f + column * 303f, 302f + row * 318f, 307f + column * 303f, 596f + row * 318f)
            val selected = currentGadget == index
            val owned = gadgetMask and (1 shl index) != 0 || selected
            paint.color = when {
                selected -> Color.rgb(49, 46, 31)
                owned -> Color.rgb(16, 29, 33)
                else -> Color.rgb(10, 17, 20)
            }
            canvas.drawRect(bounds, paint)
            stroke.color = when {
                selected -> Color.rgb(229, 182, 84)
                owned -> Color.rgb(73, 98, 99)
                else -> Color.rgb(35, 46, 48)
            }
            stroke.strokeWidth = if (selected) 4f else 2f
            canvas.drawRect(bounds, stroke)
            drawGadgetIcon(canvas, index, bounds.centerX(), bounds.top + 112f, 64f, if (owned) Color.rgb(218, 174, 80) else Color.rgb(74, 83, 83))
            fittedText(canvas, GADGETS[index], bounds.centerX(), bounds.top + 215f, 25f, bounds.width() - 32f, if (owned) Color.WHITE else Color.rgb(98, 108, 108), true)
            val state = when {
                selected -> "SELECTED"
                owned -> "AVAILABLE"
                else -> "LOCKED"
            }
            text(canvas, state, bounds.centerX(), bounds.top + 260f, 18f, if (selected) Color.rgb(229, 182, 84) else Color.rgb(125, 144, 143), true, Paint.Align.CENTER)
        }
        text(canvas, "Inventory updates automatically as Batman acquires or changes equipment.", 24f, 1006f, 20f, Color.rgb(120, 138, 138), false)
    }

    private fun drawMap(canvas: Canvas) {
        sectionTitle(canvas, "ARKHAM POSITIONING", "LIVE COORDINATE AND ORIENTATION FEED")
        val bounds = RectF(24f, 294f, 1216f, 1038f)
        paint.color = Color.rgb(7, 16, 19)
        canvas.drawRect(bounds, paint)
        stroke.color = Color.rgb(47, 67, 68)
        stroke.strokeWidth = 2f
        canvas.drawRect(bounds, stroke)
        canvas.save()
        canvas.clipRect(bounds)
        canvas.translate(mapPanX, mapPanY)
        val grid = 80f * mapZoom
        val centerX = bounds.centerX()
        val centerY = bounds.centerY()
        stroke.strokeWidth = 1f
        stroke.color = Color.rgb(29, 49, 51)
        var x = centerX % grid - grid
        while (x < bounds.right + grid) {
            canvas.drawLine(x, bounds.top, x, bounds.bottom, stroke)
            x += grid
        }
        var y = centerY % grid - grid
        while (y < bounds.bottom + grid) {
            canvas.drawLine(bounds.left, y, bounds.right, y, stroke)
            y += grid
        }
        stroke.color = Color.rgb(64, 91, 92)
        stroke.strokeWidth = 3f
        canvas.drawLine(bounds.left, centerY, bounds.right, centerY, stroke)
        canvas.drawLine(centerX, bounds.top, centerX, bounds.bottom, stroke)
        drawMapCompass(canvas, bounds.right - 90f, bounds.top + 95f)
        drawPlayerMarker(canvas, centerX, centerY)
        canvas.restore()
        paint.color = Color.argb(218, 5, 11, 14)
        canvas.drawRect(bounds.left + 20f, bounds.top + 20f, bounds.left + 390f, bounds.top + 160f, paint)
        text(canvas, location.ifBlank { "ARKHAM SECTOR $locationId" }, bounds.left + 42f, bounds.top + 61f, 24f, Color.rgb(224, 180, 88), true)
        text(canvas, "X  ${formatCoordinate(playerX)}", bounds.left + 42f, bounds.top + 98f, 19f, Color.rgb(184, 199, 197), false)
        text(canvas, "Y  ${formatCoordinate(playerY)}", bounds.left + 156f, bounds.top + 98f, 19f, Color.rgb(184, 199, 197), false)
        text(canvas, "Z  ${formatCoordinate(playerZ)}", bounds.left + 270f, bounds.top + 98f, 19f, Color.rgb(184, 199, 197), false)
        text(canvas, "HEADING ${playerYaw.toInt()}°", bounds.left + 42f, bounds.top + 135f, 18f, Color.rgb(124, 145, 145), true)
        if (availability and MAP_AVAILABLE == 0) {
            text(canvas, "POSITION FEED WAITING FOR SAVE", bounds.centerX(), bounds.centerY() + 120f, 22f, Color.rgb(165, 135, 75), true, Paint.Align.CENTER)
        }
        text(canvas, "DRAG TO PAN · PINCH TO ZOOM", bounds.right - 24f, bounds.bottom - 22f, 17f, Color.rgb(103, 123, 123), true, Paint.Align.RIGHT)
    }

    private fun drawSkills(canvas: Canvas) {
        sectionTitle(canvas, "WAYNETECH UPGRADES", "$upgradesBought OF ${SKILLS.size} ACQUIRED · $unlockables AVAILABLE")
        for (index in SKILLS.indices) {
            val column = index % 4
            val row = index / 4
            val bounds = RectF(24f + column * 303f, 292f + row * 143f, 307f + column * 303f, 421f + row * 143f)
            val acquired = skillStates[index] > 0
            paint.color = if (acquired) Color.rgb(29, 34, 28) else Color.rgb(11, 20, 23)
            canvas.drawRect(bounds, paint)
            stroke.color = if (acquired) Color.rgb(209, 162, 68) else Color.rgb(43, 59, 61)
            stroke.strokeWidth = if (acquired) 3f else 2f
            canvas.drawRect(bounds, stroke)
            paint.color = if (acquired) Color.rgb(211, 164, 69) else Color.rgb(44, 59, 61)
            canvas.drawCircle(bounds.left + 35f, bounds.centerY(), 18f, paint)
            if (acquired) drawCheck(canvas, bounds.left + 35f, bounds.centerY(), Color.rgb(8, 15, 16))
            fittedText(canvas, SKILLS[index], bounds.left + 66f, bounds.centerY() + 8f, 22f, bounds.width() - 82f, if (acquired) Color.WHITE else Color.rgb(120, 135, 134), true, Paint.Align.LEFT)
        }
    }

    private fun drawCaseFile(canvas: Canvas) {
        sectionTitle(canvas, "CASE FILE", "CURRENT MISSION AND INCIDENT TELEMETRY")
        val primary = RectF(24f, 294f, 820f, 1038f)
        val sidebar = RectF(842f, 294f, 1216f, 1038f)
        card(canvas, primary, "ACTIVE INVESTIGATION", "ORACLE SECURE CHANNEL")
        text(canvas, objective.ifBlank { "Current objective pending" }, primary.left + 38f, primary.top + 118f, 34f, Color.rgb(229, 184, 90), true)
        drawWrappedText(
            canvas,
            description.ifBlank { "Load a save and enter gameplay to synchronize the active case description." },
            primary.left + 38f,
            primary.top + 180f,
            primary.width() - 76f,
            27f,
            40f,
            Color.rgb(211, 220, 218),
            12
        )
        paint.color = Color.rgb(12, 22, 25)
        canvas.drawRect(sidebar, paint)
        stroke.color = Color.rgb(54, 72, 74)
        stroke.strokeWidth = 2f
        canvas.drawRect(sidebar, stroke)
        text(canvas, "INCIDENT DATA", sidebar.left + 28f, sidebar.top + 54f, 24f, Color.WHITE, true)
        statRow(canvas, sidebar, 126f, "STORY", "${storyPercentage.coerceIn(0, 100)}%")
        statRow(canvas, sidebar, 194f, "SAVE CHAPTER", saveChapter.toString())
        statRow(canvas, sidebar, 262f, "LOCATION ID", locationId.toString())
        statRow(canvas, sidebar, 330f, "PLAYER LEVEL", playerLevel.toString())
        statRow(canvas, sidebar, 398f, "EXPERIENCE", xp.toInt().toString())
        statRow(canvas, sidebar, 466f, "HEALTH", health.toString())
        statRow(canvas, sidebar, 534f, "FIELD TIME", formatDuration(totalTime))
        drawBatMark(canvas, sidebar.centerX(), sidebar.bottom - 100f, 64f, Color.rgb(55, 67, 67))
    }

    private fun card(canvas: Canvas, bounds: RectF, title: String, subtitle: String) {
        paint.color = Color.rgb(12, 22, 25)
        canvas.drawRect(bounds, paint)
        stroke.color = Color.rgb(52, 71, 72)
        stroke.strokeWidth = 2f
        canvas.drawRect(bounds, stroke)
        paint.color = Color.rgb(207, 159, 65)
        canvas.drawRect(bounds.left, bounds.top, bounds.left + 6f, bounds.bottom, paint)
        text(canvas, title, bounds.left + 28f, bounds.top + 42f, 23f, Color.WHITE, true)
        text(canvas, subtitle, bounds.right - 22f, bounds.top + 40f, 15f, Color.rgb(111, 132, 132), true, Paint.Align.RIGHT)
        stroke.color = Color.rgb(43, 59, 61)
        stroke.strokeWidth = 1f
        canvas.drawLine(bounds.left + 24f, bounds.top + 59f, bounds.right - 24f, bounds.top + 59f, stroke)
    }

    private fun sectionTitle(canvas: Canvas, title: String, subtitle: String) {
        text(canvas, title, 24f, 267f, 29f, Color.WHITE, true)
        text(canvas, subtitle, 1216f, 265f, 17f, Color.rgb(118, 139, 139), true, Paint.Align.RIGHT)
    }

    private fun statRow(canvas: Canvas, bounds: RectF, yOffset: Float, label: String, value: String) {
        val y = bounds.top + yOffset
        text(canvas, label, bounds.left + 30f, y, 19f, Color.rgb(137, 155, 154), true)
        text(canvas, value, bounds.right - 30f, y, 25f, Color.WHITE, true, Paint.Align.RIGHT)
        stroke.color = Color.rgb(35, 49, 51)
        stroke.strokeWidth = 1f
        canvas.drawLine(bounds.left + 30f, y + 18f, bounds.right - 30f, y + 18f, stroke)
    }

    private fun drawBar(canvas: Canvas, bounds: RectF, progress: Float, color: Int) {
        paint.color = Color.rgb(28, 39, 41)
        canvas.drawRect(bounds, paint)
        val fill = RectF(bounds.left, bounds.top, bounds.left + bounds.width() * progress.coerceIn(0f, 1f), bounds.bottom)
        paint.color = color
        canvas.drawRect(fill, paint)
        stroke.color = Color.rgb(82, 100, 100)
        stroke.strokeWidth = 2f
        canvas.drawRect(bounds, stroke)
    }

    private fun drawBatMark(canvas: Canvas, centerX: Float, centerY: Float, size: Float, color: Int) {
        path.reset()
        path.moveTo(centerX, centerY + size * .40f)
        path.lineTo(centerX - size * .18f, centerY + size * .14f)
        path.lineTo(centerX - size * .48f, centerY + size * .27f)
        path.lineTo(centerX - size * .39f, centerY)
        path.lineTo(centerX - size, centerY - size * .29f)
        path.lineTo(centerX - size * .58f, centerY - size * .42f)
        path.lineTo(centerX - size * .28f, centerY - size * .22f)
        path.lineTo(centerX - size * .12f, centerY - size * .52f)
        path.lineTo(centerX, centerY - size * .25f)
        path.lineTo(centerX + size * .12f, centerY - size * .52f)
        path.lineTo(centerX + size * .28f, centerY - size * .22f)
        path.lineTo(centerX + size * .58f, centerY - size * .42f)
        path.lineTo(centerX + size, centerY - size * .29f)
        path.lineTo(centerX + size * .39f, centerY)
        path.lineTo(centerX + size * .48f, centerY + size * .27f)
        path.lineTo(centerX + size * .18f, centerY + size * .14f)
        path.close()
        paint.color = color
        canvas.drawPath(path, paint)
    }

    private fun drawHeart(canvas: Canvas, centerX: Float, centerY: Float, size: Float, color: Int) {
        path.reset()
        path.moveTo(centerX, centerY + size)
        path.cubicTo(centerX - size * 1.45f, centerY + size * .05f, centerX - size * .9f, centerY - size, centerX, centerY - size * .38f)
        path.cubicTo(centerX + size * .9f, centerY - size, centerX + size * 1.45f, centerY + size * .05f, centerX, centerY + size)
        paint.color = color
        canvas.drawPath(path, paint)
    }

    private fun drawGadgetIcon(canvas: Canvas, index: Int, centerX: Float, centerY: Float, size: Float, color: Int) {
        stroke.color = color
        stroke.strokeWidth = max(4f, size * .08f)
        stroke.strokeCap = Paint.Cap.ROUND
        stroke.strokeJoin = Paint.Join.ROUND
        path.reset()
        when (index) {
            0, 5, 6, 7 -> {
                path.moveTo(centerX - size, centerY)
                path.quadTo(centerX - size * .35f, centerY - size * .55f, centerX, centerY - size * .12f)
                path.quadTo(centerX + size * .35f, centerY - size * .55f, centerX + size, centerY)
                path.quadTo(centerX + size * .38f, centerY + size * .14f, centerX, centerY + size * .44f)
                path.quadTo(centerX - size * .38f, centerY + size * .14f, centerX - size, centerY)
                canvas.drawPath(path, stroke)
                if (index == 6) {
                    canvas.drawCircle(centerX, centerY, size * .62f, stroke)
                    canvas.drawCircle(centerX, centerY, size * .86f, stroke)
                }
            }
            1 -> {
                canvas.drawCircle(centerX, centerY - size * .35f, size * .35f, stroke)
                canvas.drawLine(centerX, centerY, centerX, centerY + size, stroke)
                canvas.drawLine(centerX, centerY + size * .35f, centerX - size * .45f, centerY + size * .68f, stroke)
                canvas.drawLine(centerX, centerY + size * .35f, centerX + size * .45f, centerY + size * .68f, stroke)
            }
            2 -> {
                canvas.drawRoundRect(centerX - size * .65f, centerY - size * .78f, centerX + size * .65f, centerY + size * .78f, size * .14f, size * .14f, stroke)
                canvas.drawLine(centerX - size * .38f, centerY - size * .3f, centerX + size * .38f, centerY + size * .3f, stroke)
                canvas.drawLine(centerX + size * .38f, centerY - size * .3f, centerX - size * .38f, centerY + size * .3f, stroke)
            }
            3 -> {
                canvas.drawCircle(centerX, centerY, size * .78f, stroke)
                canvas.drawCircle(centerX, centerY, size * .24f, stroke)
                for (angle in 0 until 8) {
                    val a = Math.toRadians((angle * 45).toDouble())
                    canvas.drawLine(centerX, centerY, centerX + kotlin.math.cos(a).toFloat() * size * .78f, centerY + kotlin.math.sin(a).toFloat() * size * .78f, stroke)
                }
            }
            else -> {
                canvas.drawLine(centerX - size, centerY + size * .5f, centerX + size, centerY - size * .5f, stroke)
                canvas.drawCircle(centerX - size * .75f, centerY + size * .38f, size * .24f, stroke)
                canvas.drawCircle(centerX + size * .75f, centerY - size * .38f, size * .24f, stroke)
            }
        }
        stroke.strokeCap = Paint.Cap.BUTT
    }

    private fun drawTabIcon(canvas: Canvas, index: Int, centerX: Float, centerY: Float, selected: Boolean) {
        val color = if (selected) Color.rgb(15, 21, 22) else Color.rgb(149, 165, 164)
        when (index) {
            0 -> drawHeart(canvas, centerX, centerY, 12f, color)
            1 -> drawGadgetIcon(canvas, 0, centerX, centerY, 16f, color)
            2 -> {
                stroke.color = color
                stroke.strokeWidth = 3f
                canvas.drawCircle(centerX, centerY, 15f, stroke)
                canvas.drawLine(centerX, centerY - 20f, centerX, centerY + 20f, stroke)
                canvas.drawLine(centerX - 20f, centerY, centerX + 20f, centerY, stroke)
            }
            3 -> {
                paint.color = color
                canvas.drawRect(centerX - 16f, centerY - 16f, centerX - 3f, centerY - 3f, paint)
                canvas.drawRect(centerX + 3f, centerY - 16f, centerX + 16f, centerY - 3f, paint)
                canvas.drawRect(centerX - 16f, centerY + 3f, centerX - 3f, centerY + 16f, paint)
                canvas.drawRect(centerX + 3f, centerY + 3f, centerX + 16f, centerY + 16f, paint)
            }
            else -> {
                stroke.color = color
                stroke.strokeWidth = 3f
                canvas.drawRect(centerX - 14f, centerY - 18f, centerX + 14f, centerY + 18f, stroke)
                canvas.drawLine(centerX - 8f, centerY - 7f, centerX + 8f, centerY - 7f, stroke)
                canvas.drawLine(centerX - 8f, centerY + 2f, centerX + 8f, centerY + 2f, stroke)
                canvas.drawLine(centerX - 8f, centerY + 11f, centerX + 4f, centerY + 11f, stroke)
            }
        }
    }

    private fun drawCheck(canvas: Canvas, centerX: Float, centerY: Float, color: Int) {
        stroke.color = color
        stroke.strokeWidth = 4f
        path.reset()
        path.moveTo(centerX - 9f, centerY)
        path.lineTo(centerX - 2f, centerY + 7f)
        path.lineTo(centerX + 11f, centerY - 8f)
        canvas.drawPath(path, stroke)
    }

    private fun drawMapCompass(canvas: Canvas, centerX: Float, centerY: Float) {
        stroke.color = Color.rgb(170, 135, 66)
        stroke.strokeWidth = 3f
        canvas.drawCircle(centerX, centerY, 42f, stroke)
        canvas.drawLine(centerX, centerY - 55f, centerX, centerY + 55f, stroke)
        canvas.drawLine(centerX - 55f, centerY, centerX + 55f, centerY, stroke)
        text(canvas, "N", centerX, centerY - 62f, 19f, Color.rgb(222, 177, 83), true, Paint.Align.CENTER)
    }

    private fun drawPlayerMarker(canvas: Canvas, centerX: Float, centerY: Float) {
        canvas.save()
        canvas.rotate(playerYaw, centerX, centerY)
        path.reset()
        path.moveTo(centerX, centerY - 34f)
        path.lineTo(centerX - 22f, centerY + 25f)
        path.lineTo(centerX, centerY + 13f)
        path.lineTo(centerX + 22f, centerY + 25f)
        path.close()
        paint.color = Color.rgb(228, 181, 84)
        canvas.drawPath(path, paint)
        canvas.restore()
        stroke.color = Color.argb(100, 228, 181, 84)
        stroke.strokeWidth = 3f
        canvas.drawCircle(centerX, centerY, 50f, stroke)
        canvas.drawCircle(centerX, centerY, 70f, stroke)
    }

    private fun text(
        canvas: Canvas,
        value: String,
        x: Float,
        y: Float,
        size: Float,
        color: Int,
        bold: Boolean,
        align: Paint.Align = Paint.Align.LEFT
    ) {
        paint.shader = null
        paint.style = Paint.Style.FILL
        paint.color = color
        paint.textSize = size
        paint.typeface = if (bold) medium else regular
        paint.textAlign = align
        canvas.drawText(value, x, y, paint)
    }

    private fun fittedText(
        canvas: Canvas,
        value: String,
        x: Float,
        y: Float,
        size: Float,
        maxWidth: Float,
        color: Int,
        bold: Boolean,
        align: Paint.Align = Paint.Align.CENTER
    ) {
        paint.textSize = size
        paint.typeface = if (bold) medium else regular
        val nextSize = if (paint.measureText(value) > maxWidth) size * maxWidth / paint.measureText(value) else size
        text(canvas, value, x, y, max(15f, nextSize), color, bold, align)
    }

    private fun drawWrappedText(
        canvas: Canvas,
        value: String,
        x: Float,
        y: Float,
        maxWidth: Float,
        size: Float,
        lineHeight: Float,
        color: Int,
        maxLines: Int
    ) {
        paint.textSize = size
        paint.typeface = regular
        val words = value.replace('\n', ' ').split(Regex("\\s+")).filter { it.isNotBlank() }
        var line = ""
        var lineIndex = 0
        for (word in words) {
            val candidate = if (line.isEmpty()) word else "$line $word"
            if (paint.measureText(candidate) > maxWidth && line.isNotEmpty()) {
                text(canvas, line, x, y + lineIndex * lineHeight, size, color, false)
                line = word
                lineIndex++
                if (lineIndex >= maxLines) return
            } else {
                line = candidate
            }
        }
        if (line.isNotEmpty() && lineIndex < maxLines) text(canvas, line, x, y + lineIndex * lineHeight, size, color, false)
    }

    private fun formatDuration(seconds: Float): String {
        if (!seconds.isFinite() || seconds < 0f) return "--:--"
        val totalMinutes = (seconds / 60f).toInt()
        return "${totalMinutes / 60}:${(totalMinutes % 60).toString().padStart(2, '0')}"
    }

    private fun formatCoordinate(value: Float): String = if (value.isFinite()) String.format("%.0f", value) else "--"

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val offsetX = (width - DESIGN_WIDTH * scale) * .5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * .5f
        val x = (event.x - offsetX) / scale
        val y = (event.y - offsetY) / scale
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                touchX = x
                touchY = y
                pressedTab = tabBounds.indexOfFirst { it.contains(x, y) }
                mapDragging = selectedTab == 2 && y >= 294f
                lastPinchDistance = 0f
                invalidate()
                return true
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                if (selectedTab == 2 && event.pointerCount >= 2) lastPinchDistance = pinchDistance(event)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (selectedTab == 2 && event.pointerCount >= 2) {
                    val distance = pinchDistance(event)
                    if (lastPinchDistance > 0f) mapZoom = (mapZoom * distance / lastPinchDistance).coerceIn(.55f, 4f)
                    lastPinchDistance = distance
                    invalidate()
                } else if (mapDragging) {
                    mapPanX += x - touchX
                    mapPanY += y - touchY
                    touchX = x
                    touchY = y
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP -> {
                val tab = tabBounds.indexOfFirst { it.contains(x, y) }
                if (pressedTab >= 0 && tab == pressedTab) {
                    selectedTab = tab
                    performClick()
                }
                pressedTab = -1
                mapDragging = false
                lastPinchDistance = 0f
                invalidate()
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                pressedTab = -1
                mapDragging = false
                lastPinchDistance = 0f
                invalidate()
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun pinchDistance(event: MotionEvent): Float {
        if (event.pointerCount < 2) return 0f
        val dx = event.getX(0) - event.getX(1)
        val dy = event.getY(0) - event.getY(1)
        return kotlin.math.sqrt(dx * dx + dy * dy)
    }
}
