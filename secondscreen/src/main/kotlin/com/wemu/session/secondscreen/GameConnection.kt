package com.wemu.session.secondscreen

import android.util.Log
import com.thor.core.common.coroutines.backgroundScope
import com.thor.core.common.log.ThorLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

sealed class ConnectionState {
    data object Disconnected : ConnectionState()
    data object Connecting   : ConnectionState()
    data object Connected    : ConnectionState()
    data class  Error(val message: String) : ConnectionState()
}

class GameConnection(
    val host: String,
    val port: Int,
    private val autoReconnect: Boolean = true
) {
    private val scope = backgroundScope("GameConnection", Dispatchers.IO)

    private val _messages = MutableSharedFlow<GameMessage>(
        extraBufferCapacity = 256,
    )

    val messages: SharedFlow<GameMessage> = _messages.asSharedFlow()

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)

    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.MILLISECONDS)
        .writeTimeout(5, TimeUnit.SECONDS)

        .build()

    @Volatile private var webSocket: WebSocket? = null
    private var reconnectJob: Job? = null
    @Volatile private var manualDisconnect = false
    @Volatile private var closed = false
    private val warnedAboutDroppedMessage = AtomicBoolean(false)
    private val loggedFirstMessage = AtomicBoolean(false)
    private val warnedAboutInvalidMessage = AtomicBoolean(false)
    private val socketLock = Any()

    fun connect() {
        if (closed) return
        manualDisconnect = false
        reconnectJob?.cancel()
        Log.i(TAG, "Connecting to ws://$host:$port")
        doConnect()
    }

    fun disconnect() {
        manualDisconnect = true
        reconnectJob?.cancel()
        reconnectJob = null
        val active = synchronized(socketLock) {
            webSocket.also { webSocket = null }
        }
        active?.close(1000, "User disconnected")
        _connectionState.value = ConnectionState.Disconnected
    }

    /** Permanently releases this one-shot connection and all of OkHttp's threads. */
    fun close() {
        if (closed) return
        closed = true
        disconnect()
        httpClient.dispatcher.cancelAll()
        httpClient.connectionPool.evictAll()
        httpClient.cache?.close()
        httpClient.dispatcher.executorService.shutdown()
        scope.cancel()
    }

    fun send(text: String) {
        synchronized(socketLock) { webSocket }?.send(text)
    }

    private fun doConnect() {
        if (_connectionState.value is ConnectionState.Connected ||
            _connectionState.value is ConnectionState.Connecting) return

        _connectionState.value = ConnectionState.Connecting

        val request = Request.Builder()
            .url("ws://$host:$port")
            .build()

        val listener = object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) {
                val accepted = synchronized(socketLock) {
                    val current = webSocket
                    if (!closed && !manualDisconnect && (current == null || current === ws)) {
                        webSocket = ws
                        true
                    } else {
                        false
                    }
                }
                if (!accepted) {
                    ws.close(1000, "Connection superseded")
                    return
                }
                reconnectJob?.cancel()
                reconnectJob = null
                _connectionState.value = ConnectionState.Connected
                Log.i(TAG, "Connected to ws://$host:$port")
            }

            override fun onMessage(ws: WebSocket, text: String) {
                val current = synchronized(socketLock) { webSocket === ws }
                if (closed || manualDisconnect || !current) return
                val msg = parseGameMessage(text)
                if (msg == null) {
                    if (warnedAboutInvalidMessage.compareAndSet(false, true)) {
                        Log.w(TAG, "Ignored an invalid second-screen message (${text.length} chars)")
                    }
                    return
                }
                if (loggedFirstMessage.compareAndSet(false, true)) {
                    Log.i(TAG, "Received first second-screen message: ${msg.javaClass.simpleName}")
                }
                if (!_messages.tryEmit(msg) && warnedAboutDroppedMessage.compareAndSet(false, true)) {
                    ThorLog.w(TAG, "Second-screen messages exceeded the bounded input buffer")
                }
            }

            override fun onClosing(ws: WebSocket, code: Int, reason: String) {
                ws.close(1000, null)
            }

            override fun onClosed(ws: WebSocket, code: Int, reason: String) {
                handleDisconnect(ws, null)
            }

            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) {
                handleDisconnect(ws, t.message ?: "Connection failed")
            }
        }
        // Hold the identity lock across construction and assignment. OkHttp can
        // fail on another executor immediately; that callback must not observe
        // a null socket and leave this connection stuck in Connecting forever.
        synchronized(socketLock) {
            if (closed || manualDisconnect) return
            webSocket = httpClient.newWebSocket(request, listener)
        }
    }

    private fun handleDisconnect(socket: WebSocket, errorMsg: String?) {
        // An old socket can report failure after its replacement has connected.
        // Such a callback must not tear down or relabel the current connection.
        val current = synchronized(socketLock) {
            if (webSocket !== socket) false else {
                webSocket = null
                true
            }
        }
        if (!current) return
        _connectionState.value = if (errorMsg != null)
            ConnectionState.Error(errorMsg)
        else
            ConnectionState.Disconnected
        if (errorMsg != null) {
            Log.w(TAG, "Disconnected from ws://$host:$port: $errorMsg")
        } else {
            Log.i(TAG, "Disconnected from ws://$host:$port")
        }

        reconnectJob?.cancel()
        reconnectJob = null
        if (!closed && !manualDisconnect && autoReconnect) {
            Log.i(TAG, "Retrying second-screen connection in 3000ms")
            reconnectJob = scope.launch {
                delay(3_000)
                reconnectJob = null
                if (!closed && !manualDisconnect) doConnect()
            }
        }
    }

    private companion object {
        const val TAG = "GameConnection"
    }
}
