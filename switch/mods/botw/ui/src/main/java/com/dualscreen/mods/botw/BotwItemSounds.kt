package com.dualscreen.mods.botw

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool
import android.os.Handler
import android.os.Looper
import com.dualscreen.companion.api.CompanionHostV2
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.Executors

/** Original pause-player SLink cues, advanced by the portrait's animation clock. */
internal class BotwItemSounds(context: Context, private val host: CompanionHostV2) {
    private data class Cue(val files: List<String>, val at: Long, val end: Long,
                           val volume: Float, val pitch: Float)
    private val handler = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    private val pool = SoundPool.Builder().setMaxStreams(8).setAudioAttributes(
        AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build()
    private val samples = mutableMapOf<String, Int>()
    private val ready = mutableSetOf<Int>()
    private val active = mutableMapOf<Int, Long>()
    private var clips = emptyMap<String, List<Cue>>()
    private var playing = emptyList<Cue>()
    private var nextCue = 0
    private var closed = false

    init {
        pool.setOnLoadCompleteListener { _, id, status ->
            handler.post { if (!closed && status == 0) ready.add(id) }
        }
        val directory = File(context.cacheDir, "botw-item-sounds")
        worker.execute {
            runCatching {
                val root = host.openAsset("botw/player/sounds/cues.json").bufferedReader().use {
                    JSONObject(it.readText()).getJSONObject("clips")
                }
                val parsed = buildMap {
                    root.keys().forEach { source ->
                        val entries = root.getJSONArray(source)
                        put(source, List(entries.length()) { index ->
                            val item = entries.getJSONObject(index)
                            val options = item.optJSONArray("alternatives")
                            val names = options?.let { a -> List(a.length()) { a.getString(it) } }
                                ?: listOf(item.getString("file"))
                            require(names.all { it.matches(Regex("[A-Za-z0-9_]+\\.wav")) })
                            Cue(names, item.getLong("timeMs"), item.getLong("endMs"),
                                item.getDouble("volume").toFloat().coerceIn(0f, 1f),
                                item.getDouble("pitch").toFloat().coerceIn(.5f, 2f))
                        }.sortedBy { it.at })
                    }
                }
                check(directory.isDirectory || directory.mkdirs())
                val files = parsed.values.flatten().flatMap { it.files }.distinct().associateWith { name ->
                    val bytes = host.openAsset("botw/player/sounds/$name").use { it.readBytes() }
                    val hash = MessageDigest.getInstance("SHA-256").digest(bytes)
                        .joinToString("") { "%02x".format(it) }
                    File(directory, "$hash.wav").also { file ->
                        // These are disposable private sound-cache files, never RomFS/save files.
                        file.writeBytes(bytes)
                    }
                }
                handler.post {
                    if (!closed) {
                        clips = parsed
                        files.forEach { (name, file) -> samples[name] = pool.load(file.path, 1) }
                    }
                }
            }.onFailure { ModuleLog(host).warning("[BOTW Audio] Cue preload failed: ${it.message}") }
        }
    }

    fun begin(source: String) {
        stop()
        playing = clips[source].orEmpty()
        ModuleLog(host).info("[BOTW Audio] begin source=$source cues=${playing.size} ready=${ready.size}/${samples.size}")
    }

    fun advance(elapsed: Long) {
        active.filterValues { it <= elapsed }.keys.toList().forEach { pool.stop(it); active.remove(it) }
        while (nextCue < playing.size && playing[nextCue].at <= elapsed) {
            val cue = playing[nextCue++] // Exactly once, even if rendering retries a frame.
            if (elapsed >= cue.end || elapsed - cue.at > 100) continue
            val name = cue.files.random()
            val sample = samples[name] ?: continue
            if (sample !in ready) continue // Never play a late async load out of sync.
            val stream = pool.play(sample, cue.volume, cue.volume, 1, 0, cue.pitch)
            if (stream != 0) active[stream] = cue.end
            ModuleLog(host).info("[BOTW Audio] cue=$name at=${cue.at} elapsed=$elapsed stream=$stream")
        }
    }

    fun stop() {
        active.keys.forEach(pool::stop)
        active.clear()
        playing = emptyList()
        nextCue = 0
    }

    fun release() {
        if (closed) return
        closed = true
        stop()
        worker.shutdownNow()
        pool.release()
    }
}
