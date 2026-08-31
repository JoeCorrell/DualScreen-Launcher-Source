package com.dualscreen.launcher.session

import com.thor.core.common.coroutines.backgroundScope
import com.thor.core.common.log.ThorLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.atomic.AtomicBoolean

enum class NativePanelHaptic {
    Selection,
    Action,
    Unlock,
}

/**
 * Ordered input-only transport for the native Stardew panel.
 *
 * The launcher keeps its older native mods on their private 17788 channel;
 * Stardew's embedded runtime intentionally uses WEMU's standard 7788 channel.
 * Keeping this transport separate avoids changing any other supported game.
 */
object NativePanelInput {
    private const val TAG = "NativePanelInput"
    private const val PORT = 7788
    private const val RETRY_DELAY_MS = 100L

    private data class Message(val kind: String, val payload: String)

    private val scope = backgroundScope(TAG, Dispatchers.Default)
    private val queue = Channel<Message>(capacity = 512)
    private val queueWarningLogged = AtomicBoolean(false)
    private val mutableHaptics = MutableSharedFlow<NativePanelHaptic>(extraBufferCapacity = 16)
    val haptics: SharedFlow<NativePanelHaptic> = mutableHaptics.asSharedFlow()

    @Suppress("unused")
    private val pump = scope.launch(Dispatchers.IO) {
        DatagramSocket().use { socket ->
            val loopback = InetAddress.getByAddress(byteArrayOf(127, 0, 0, 1))
            coroutineScope {
                launch {
                    val buffer = ByteArray(64)
                    while (true) {
                        val failure = runCatching {
                            val packet = DatagramPacket(buffer, buffer.size)
                            socket.receive(packet)
                            if (!packet.address.isLoopbackAddress || packet.port != PORT) {
                                return@runCatching
                            }
                            val cue = when (
                                packet.data.decodeToString(
                                    startIndex = packet.offset,
                                    endIndex = packet.offset + packet.length,
                                ).trim()
                            ) {
                                "haptic selection" -> NativePanelHaptic.Selection
                                "haptic action" -> NativePanelHaptic.Action
                                "haptic unlock" -> NativePanelHaptic.Unlock
                                else -> null
                            }
                            cue?.let(mutableHaptics::tryEmit)
                        }.exceptionOrNull()
                        if (failure != null) delay(RETRY_DELAY_MS)
                    }
                }

                for (message in queue) {
                    runCatching {
                        val bytes = message.payload.toByteArray(Charsets.US_ASCII)
                        socket.send(DatagramPacket(bytes, bytes.size, loopback, PORT))
                    }.onFailure { failure ->
                        ThorLog.w(TAG, "Native panel ${message.kind} send failed", failure)
                    }
                }
            }
        }
    }

    fun gesture(kind: String, x: Float, y: Float, value: Float? = null): Boolean {
        if (kind !in gestures) return false
        val suffix = value?.takeIf(Float::isFinite)?.let { " $it" }.orEmpty()
        if (kind == "pinch" && suffix.isEmpty()) return false
        return enqueue(kind, "$kind ${x.coerceIn(0f, 1f)} ${y.coerceIn(0f, 1f)}$suffix")
    }

    fun page(next: Boolean): Boolean =
        enqueue("page", if (next) "page next" else "page previous")

    fun toolbar(next: Boolean): Boolean =
        enqueue("toolbar", if (next) "toolbar next" else "toolbar previous")

    fun navigation(action: String): Boolean {
        if (action !in navigation) return false
        return enqueue("navigation", "nav $action")
    }

    fun action(name: String): Boolean {
        if (name !in actions) return false
        return enqueue("action", name)
    }

    private fun enqueue(kind: String, payload: String): Boolean {
        val result = queue.trySend(Message(kind, payload))
        if (!result.isSuccess && queueWarningLogged.compareAndSet(false, true)) {
            ThorLog.w(TAG, "Native panel input queue rejected a $kind packet")
        }
        return result.isSuccess
    }

    private val gestures = setOf("tap", "hold", "down", "move", "up", "cancel", "pinch")
    private val navigation = setOf("left", "right", "up", "down", "activate")
    private val actions = setOf("sort", "use")
}
