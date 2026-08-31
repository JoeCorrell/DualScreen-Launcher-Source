package com.wemu.session.secondscreen

import androidx.compose.runtime.Immutable

@Immutable
data class ValheimItem(
    val slot: Int,
    val name: String = "",
    val stack: Int = 0,
    val equipped: Boolean = false,
    val iconKey: String = "",
    val quality: Int = 0,
    val durability: Float = -1f,
    val maxDurability: Float = -1f,
) {
    val empty: Boolean get() = name.isEmpty()

    val wear: Float?
        get() = if (maxDurability > 0f && durability >= 0f) {
            (durability / maxDurability).coerceIn(0f, 1f)
        } else {
            null
        }
}

@Immutable
data class ValheimEffect(
    val name: String,
    val displayName: String = "",
    val iconKey: String = "",
    val remaining: Float = 0f,
    val ttl: Float = 0f,
) {
    val left: Float get() = if (ttl > 0f) (remaining / ttl).coerceIn(0f, 1f) else 0f

    val clock: String
        get() {
            val seconds = remaining.toInt().coerceAtLeast(0)
            val minutes = seconds / 60
            return if (minutes >= 1) "${minutes}m" else "${seconds}s"
        }
}

@Immutable
data class ValheimState(
    val worldName: String = "",
    val playerName: String = "",
    val health: Float = 0f,
    val maxHealth: Float = 0f,
    val stamina: Float = 0f,
    val maxStamina: Float = 0f,
    val hotbar: List<ValheimItem> = emptyList(),
    val inventory: List<ValheimItem> = emptyList(),
    val effects: List<ValheimEffect> = emptyList(),
    val sprites: Map<String, UiSprite> = emptyMap(),
    val mirrors: Map<String, androidx.compose.ui.graphics.ImageBitmap> = emptyMap(),
    val terrain: androidx.compose.ui.graphics.ImageBitmap? = null,
    val fog: androidx.compose.ui.graphics.ImageBitmap? = null,
    val mapSize: Int = 0,
    val x: Float = 0f,
    val z: Float = 0f,
    val angle: Float = 0f,
    val pixelSize: Float = 0f,
) {
    fun piece(key: String): UiSprite? = sprites[key]

    val hasMap: Boolean get() = terrain != null
    val hasWorld: Boolean get() = maxHealth > 0f

    val food: List<ValheimEffect>
        get() = effects.filter { it.ttl > 0f }.sortedBy { it.remaining }

    val expiring: ValheimEffect? get() = food.firstOrNull { it.left < 0.25f }
}
