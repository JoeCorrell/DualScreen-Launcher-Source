package com.wemu.session.secondscreen

import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import com.thor.core.common.coroutines.backgroundScope
import com.thor.core.common.log.ThorLog
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

enum class GuestHapticCue {
    Selection,
    Action,
    Unlock,
}

/** Controller actions forwarded from the focused upper-screen session window. */
enum class StardewPanelControl {
    PREVIOUS_PAGE,
    NEXT_PAGE,
    PREVIOUS_SLOT,
    NEXT_SLOT,
    PREVIOUS_TOOLBAR,
    NEXT_TOOLBAR,
    SORT,
    USE,
}

object SecondScreen {

    private const val TAG = "SecondScreenInput"

    const val PORT = 7786
    // Launcher owns a distinct loopback port from full WEMU (7788). Android
    // applications share the device network namespace, so using WEMU's port
    // made the second process fail WSA bind with 10048 and silently disabled
    // every lower-panel touch/keyboard packet.
    private const val GUEST_INPUT_PORT = 17788

    private val scope = backgroundScope("SecondScreen", Dispatchers.Default)

    /**
     * Ordered local input for native guest mods.
     *
     * A new coroutine and UDP socket per move can reorder down/move/up and is
     * needlessly expensive during a drag. One actor owns one loopback socket,
     * preserving gesture order without ever blocking Compose's pointer loop.
     */
    private data class GuestInputMessage(
        val kind: String,
        val payload: String,
        val queuedAtNanos: Long,
    )

    private val guestInput = Channel<GuestInputMessage>(Channel.UNLIMITED)
    private val guestInputQueueWarningLogged = AtomicBoolean(false)
    private val queuedPinches = AtomicLong(0)
    private val transmittedPinches = AtomicLong(0)
    private val _guestHaptics = MutableSharedFlow<GuestHapticCue>(
        extraBufferCapacity = 16,
    )
    val guestHaptics: SharedFlow<GuestHapticCue> = _guestHaptics.asSharedFlow()

    private val _stardewPanelControls = MutableSharedFlow<StardewPanelControl>(
        extraBufferCapacity = 32,
    )
    val stardewPanelControls: SharedFlow<StardewPanelControl> =
        _stardewPanelControls.asSharedFlow()

    fun controlStardewPanel(control: StardewPanelControl): Boolean =
        _stardewPanelControls.tryEmit(control)

    @Suppress("unused")
    private val guestInputPump = scope.launch(Dispatchers.IO) {
        DatagramSocket().use { socket ->
            // Android deliberately returns the IPv6 ::1 address from
            // getLoopbackAddress(). Native Windows guest mods bind an AF_INET
            // listener, so pin this transport to IPv4 or every gesture is
            // silently sent to the wrong socket family.
            val address = InetAddress.getByAddress(byteArrayOf(127, 0, 0, 1))
            coroutineScope {
                launch {
                    val feedbackBytes = ByteArray(64)
                    while (true) {
                        val failure = runCatching {
                            val packet = DatagramPacket(feedbackBytes, feedbackBytes.size)
                            socket.receive(packet)
                            if (!packet.address.isLoopbackAddress || packet.port != GUEST_INPUT_PORT) {
                                return@runCatching
                            }
                            val message = packet.data.decodeToString(
                                startIndex = packet.offset,
                                endIndex = packet.offset + packet.length,
                            ).trim()
                            val cue = when (message) {
                                "haptic selection" -> GuestHapticCue.Selection
                                "haptic action" -> GuestHapticCue.Action
                                "haptic unlock" -> GuestHapticCue.Unlock
                                else -> null
                            }
                            cue?.let(_guestHaptics::tryEmit)
                        }.exceptionOrNull()
                        // A closed or temporarily failed socket must not become an
                        // unbounded retry loop on Dispatchers.IO.
                        if (failure != null) delay(GUEST_SOCKET_RETRY_MS)
                    }
                }
                for (message in guestInput) {
                    val failure = runCatching {
                        val bytes = message.payload.toByteArray(Charsets.US_ASCII)
                        socket.send(
                            DatagramPacket(bytes, bytes.size, address, GUEST_INPUT_PORT),
                        )
                    }.exceptionOrNull()
                    if (failure != null) {
                        ThorLog.w(
                            TAG,
                            "Native guest UDP send failed for ${message.kind}",
                            failure,
                        )
                    } else if (message.kind == "pinch") {
                        val count = transmittedPinches.incrementAndGet()
                        val latencyMicros =
                            (System.nanoTime() - message.queuedAtNanos).coerceAtLeast(0L) / 1_000L
                        if (count == 1L) {
                            ThorLog.i(
                                TAG,
                                "First native guest pinch transmitted to 127.0.0.1:" +
                                    "$GUEST_INPUT_PORT queueLatencyUs=$latencyMicros " +
                                    "payload=${message.payload}",
                            )
                        } else {
                            ThorLog.d(TAG) {
                                "Native guest pinch transmitted count=$count " +
                                    "queueLatencyUs=$latencyMicros payload=${message.payload}"
                            }
                        }
                    }
                }
            }
        }
    }

    @Volatile
    private var connection: GameConnection? = null

    private var pump: Job? = null

    private val _state = MutableStateFlow(StardewState())
    val stardew: StateFlow<StardewState> = _state.asStateFlow()

    private val _valheim = MutableStateFlow(ValheimState())
    val valheim: StateFlow<ValheimState> = _valheim.asStateFlow()

    private val _gameId = MutableStateFlow("")

    val gameId: StateFlow<String> = _gameId.asStateFlow()

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    @Volatile
    var host: String = ""

    fun connectTo(where: String) {
        if (where == host && connection != null) return
        host = where
        shutdown()
        start()
    }

    fun start() {
        if (connection != null) return
        val live = GameConnection(host.ifEmpty { LOOPBACK }, PORT)
        connection = live
        // Start undispatched so the message collector exists before a local guest
        // can complete its WebSocket handshake and send the initial snapshot.
        pump = scope.launch(start = CoroutineStart.UNDISPATCHED) {
            launch {
                live.connectionState.collect { state ->
                    if (connection !== live) return@collect
                    _connectionState.value = state

                    if (state !is ConnectionState.Connected) forget()
                }
            }
            live.messages.collect { message ->
                if (connection === live) apply(message)
            }
        }
        live.connect()
    }

    private const val LOOPBACK = "127.0.0.1"
    private const val GUEST_SOCKET_RETRY_MS = 100L

    private fun forget() {
        _valheim.value = ValheimState()
        _state.value = StardewState()
        _gameId.value = ""
    }

    fun shutdown() {
        val oldConnection = connection
        connection = null
        pump?.cancel()
        pump = null
        oldConnection?.close()
        forget()
        _connectionState.value = ConnectionState.Disconnected
    }

    private fun decodeSprite(base64: String): ImageBitmap? = runCatching {
        val bytes = Base64.decode(base64, Base64.DEFAULT)
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
    }.getOrNull()

    fun send(command: String) {
        connection?.send(command)
    }

    /**
     * Delivers a normalized tap to a native guest-window mod.
     *
     * The Thor's guest panel is a zero-copy SurfaceControl, so Android owns its
     * touch sheet and no Windows mouse message exists. Managed mods already use
     * the WebSocket above; native proxies such as Deus Ex use this tiny local
     * datagram instead. Sending when no native listener exists is harmless.
     */
    fun sendGuestTap(x: Float, y: Float) {
        sendGuestGesture("tap", x, y)
    }

    fun sendGuestGesture(kind: String, x: Float, y: Float, value: Float? = null): Boolean {
        val nx = x.coerceIn(0f, 1f)
        val ny = y.coerceIn(0f, 1f)
        val suffix = value?.takeIf { it.isFinite() }?.let { " $it" }.orEmpty()
        if (kind == "pinch" && suffix.isEmpty()) {
            ThorLog.w(TAG, "Rejected native guest pinch with a missing/non-finite scale")
            return false
        }
        val payload = "$kind $nx $ny$suffix"
        val result = guestInput.trySend(
            GuestInputMessage(
                kind = kind,
                payload = payload,
                queuedAtNanos = System.nanoTime(),
            ),
        )
        if (!result.isSuccess && guestInputQueueWarningLogged.compareAndSet(false, true)) {
            ThorLog.w(TAG, "Native guest input queue rejected a $kind packet")
        } else if (result.isSuccess && kind == "pinch") {
            val count = queuedPinches.incrementAndGet()
            if (count == 1L) {
                ThorLog.i(TAG, "First native guest pinch enqueued payload=$payload")
            }
        }
        return result.isSuccess
    }

    /** Sends printable Unicode through the native guest mod's real Win32 input path. */
    fun sendGuestText(character: Char): Boolean =
        queueGuestInput("text", "text ${character.code}")

    /** Sends a non-printable keyboard action understood by native guest mods. */
    fun sendGuestKey(name: String): Boolean {
        if (name != "backspace" && name != "enter") return false
        return queueGuestInput("key", "key $name")
    }

    private fun queueGuestInput(kind: String, payload: String): Boolean {
        val result = guestInput.trySend(
            GuestInputMessage(
                kind = kind,
                payload = payload,
                queuedAtNanos = System.nanoTime(),
            ),
        )
        if (!result.isSuccess && guestInputQueueWarningLogged.compareAndSet(false, true)) {
            ThorLog.w(TAG, "Native guest input queue rejected a $kind packet")
        }
        return result.isSuccess
    }

    private fun apply(message: GameMessage) {
        when (message) {
            is GameInfoMessage -> {
                _gameId.value = message.gameId
                _state.value = _state.value.copy(saveName = message.saveName)
            }

            is StardewDayMessage -> _state.value = _state.value.copy(
                season = message.season,
                day = message.day,
                year = message.year,
                weekday = message.weekday,
                timeOfDay = message.timeOfDay,
                weatherToday = message.weatherToday,
                weatherTomorrow = message.weatherTomorrow,
                gold = message.gold,
                energy = message.energy,
                maxEnergy = message.maxEnergy,
                health = message.health,
                maxHealth = message.maxHealth,
                playerName = message.playerName,
                farmName = message.farmName,
                playerLevel = message.playerLevel,
                totalEarnings = message.totalEarnings,
                location = message.location,
                luck = message.luck,
                mapX = message.mapX,
                mapY = message.mapY,
            )

            is StardewInventoryMessage -> _state.value = _state.value.copy(
                items = message.items,
                selectedSlot = message.selected,
                capacity = message.capacity,
            )

            is StardewSpriteMessage -> decodeSprite(message.png)?.let { bitmap ->
                val sprite = UiSprite(
                    image = bitmap,
                    left = message.insetX,
                    top = message.insetY,
                    right = message.insetX,
                    bottom = message.insetY,
                )
                _state.value = _state.value.copy(
                    sprites = _state.value.sprites + (message.id to sprite),
                )
            }

            is StardewQuestsMessage ->
                _state.value = _state.value.copy(quests = message.quests)

            is StardewVillagersMessage ->
                _state.value = _state.value.copy(villagers = message.villagers)

            is StardewCropsMessage ->
                _state.value = _state.value.copy(crops = message.crops)

            is StardewMachinesMessage ->
                _state.value = _state.value.copy(machines = message.machines)

            is StardewBundlesMessage ->
                _state.value = _state.value.copy(bundles = message.bundles)

            is StardewAnimalsMessage ->
                _state.value = _state.value.copy(animals = message.animals)

            is StardewGroundsMessage ->
                _state.value = _state.value.copy(grounds = message.grounds)

            is StardewConfigMessage ->
                _state.value = _state.value.copy(config = message)

            is StardewSkillsMessage -> _state.value = _state.value.copy(
                farming = message.farming,
                mining = message.mining,
                foraging = message.foraging,
                fishing = message.fishing,
                combat = message.combat,
                farmingNext = message.farmingNext,
                miningNext = message.miningNext,
                foragingNext = message.foragingNext,
                fishingNext = message.fishingNext,
                combatNext = message.combatNext,
            )

            is StardewGiftsMessage ->
                _state.value = _state.value.copy(gifts = message.villagers)

            is StardewCraftingMessage ->
                _state.value = _state.value.copy(recipes = message.recipes)

            is StardewCalendarMessage ->
                _state.value = _state.value.copy(calendar = message)

            is StardewCartMessage ->
                _state.value = _state.value.copy(cartToday = message.open)

            is StardewOrdersMessage ->
                _state.value = _state.value.copy(orders = message.orders)

            is StardewCookingMessage ->
                _state.value = _state.value.copy(cooking = message.recipes)

            is StardewShippingMessage ->
                _state.value = _state.value.copy(shipping = message)

            is StardewTreesMessage ->
                _state.value = _state.value.copy(trees = message.trees)

            is StardewPetMessage ->
                _state.value = _state.value.copy(pet = message)

            is StardewMinesMessage ->
                _state.value = _state.value.copy(mines = message)

            is StardewCollectionsMessage ->
                _state.value = _state.value.copy(collections = message)

            is ValheimPlayerMessage -> _valheim.value = _valheim.value.copy(
                playerName = message.playerName,
                x = message.x,
                z = message.z,
                angle = message.angle,
                health = message.health,
                maxHealth = message.maxHealth,
                stamina = message.stamina,
                maxStamina = message.maxStamina,
            )

            is ValheimHotbarMessage ->
                _valheim.value = _valheim.value.copy(hotbar = message.slots)

            is ValheimInventoryMessage ->
                _valheim.value = _valheim.value.copy(inventory = message.items)

            is ValheimEffectsMessage ->
                _valheim.value = _valheim.value.copy(effects = message.effects)

            is ValheimSpriteMessage -> decodeSprite(message.png)?.let { bitmap ->
                _valheim.value = _valheim.value.copy(
                    sprites = _valheim.value.sprites + (
                        message.key to UiSprite(
                            image = bitmap,
                            left = message.left,
                            top = message.top,
                            right = message.right,
                            bottom = message.bottom,
                        )
                        ),
                )
            }

            is ValheimMirrorMessage -> decodeSprite(message.png)?.let { bitmap ->
                _valheim.value = _valheim.value.copy(
                    mirrors = _valheim.value.mirrors + (message.key to bitmap),
                )
            }

            is ValheimMapConfigMessage -> _valheim.value = _valheim.value.copy(
                mapSize = message.textureSize,
                pixelSize = message.pixelSize,
            )

            is ValheimMapMessage -> decodeSprite(message.png)?.let { bitmap ->
                _valheim.value = when (message.kind) {
                    "fog" -> _valheim.value.copy(fog = bitmap)
                    else -> _valheim.value.copy(terrain = bitmap)
                }
            }

            is ValheimWorldMessage ->
                _valheim.value = _valheim.value.copy(worldName = message.worldName)

            is NoticeMessage -> Unit
        }
    }
}
