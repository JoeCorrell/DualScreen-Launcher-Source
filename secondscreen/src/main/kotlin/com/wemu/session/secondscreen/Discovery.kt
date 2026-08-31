package com.wemu.session.secondscreen

import android.os.SystemClock
import com.thor.core.common.coroutines.backgroundScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.job
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import java.net.SocketTimeoutException

data class FoundGame(
    val host: String,
    val game: String,
    val save: String,
)

object Discovery {

    private const val PORT = 7787
    private const val BUFFER = 2048
    private const val FORGET_AFTER = 12_000L

    private val scope = backgroundScope("Discovery")
    private var listener: Job? = null
    private var listenerSocket: DatagramSocket? = null
    private var generation = 0

    private val seen = mutableMapOf<String, Pair<FoundGame, Long>>()

    private val _found = MutableStateFlow<List<FoundGame>>(emptyList())
    val found: StateFlow<List<FoundGame>> = _found.asStateFlow()

    private val json = Json { ignoreUnknownKeys = true; isLenient = true }

    @Synchronized
    fun start() {
        if (listener != null) return
        val token = ++generation
        val job = scope.launch(start = CoroutineStart.LAZY) {
            var socket: DatagramSocket? = null
            try {
                socket = DatagramSocket(null).apply {
                    reuseAddress = true
                    broadcast = true
                    soTimeout = 2000
                    bind(InetSocketAddress(PORT))
                }
                synchronized(this@Discovery) {
                    if (listener !== coroutineContext.job || generation != token) {
                        socket.close()
                        return@launch
                    }
                    listenerSocket = socket
                }

                val buffer = ByteArray(BUFFER)
                while (isActive) {
                    val packet = DatagramPacket(buffer, buffer.size)
                    val receiveFailure = runCatching { socket.receive(packet) }.onSuccess {
                        accept(
                            token,
                            packet.address?.hostAddress.orEmpty(),
                            String(packet.data, 0, packet.length),
                        )
                    }.exceptionOrNull()
                    if (receiveFailure != null &&
                        receiveFailure !is SocketTimeoutException &&
                        isActive
                    ) {
                        delay(SOCKET_RETRY_MS)
                    }
                    expire(token)
                }
            } finally {
                runCatching { socket?.close() }
                synchronized(this@Discovery) {
                    if (listenerSocket === socket) listenerSocket = null
                    if (listener === coroutineContext.job) listener = null
                }
            }
        }
        listener = job
        job.start()
    }

    @Synchronized
    fun stop() {
        generation++
        listener?.cancel()
        listener = null
        runCatching { listenerSocket?.close() }
        listenerSocket = null
        seen.clear()
        _found.value = emptyList()
    }

    private fun accept(token: Int, host: String, payload: String) {
        if (host.isEmpty()) return
        val parsed = runCatching {
            val obj = json.parseToJsonElement(payload).jsonObject
            if (obj["type"]?.jsonPrimitive?.contentOrNull != "wemu_beacon") return
            FoundGame(
                host = host,
                game = obj["game"]?.jsonPrimitive?.contentOrNull.orEmpty(),
                save = obj["save"]?.jsonPrimitive?.contentOrNull.orEmpty(),
            )
        }.getOrNull() ?: return

        synchronized(this) {
            if (generation != token) return
            seen[host] = parsed to SystemClock.elapsedRealtime()
            publish()
        }
    }

    private fun expire(token: Int) {
        synchronized(this) {
            if (generation != token) return
            val now = SystemClock.elapsedRealtime()
            val gone = seen.filterValues { now - it.second > FORGET_AFTER }.keys
            if (gone.isEmpty()) return
            gone.forEach(seen::remove)
            publish()
        }
    }

    private fun publish() {
        _found.value = seen.values.map { it.first }.sortedBy { it.host }
    }

    private const val SOCKET_RETRY_MS = 100L
}
