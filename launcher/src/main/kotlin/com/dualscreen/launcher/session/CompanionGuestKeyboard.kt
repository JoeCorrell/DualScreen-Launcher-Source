package com.dualscreen.launcher.session

import android.view.KeyCharacterMap
import android.view.KeyEvent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Backspace
import androidx.compose.material.icons.automirrored.rounded.KeyboardReturn
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ContentPaste
import androidx.compose.material.icons.rounded.KeyboardCapslock
import androidx.compose.material.icons.rounded.SpaceBar
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.thor.core.model.KeyboardCursor
import com.thor.core.model.KeyboardKey
import com.thor.core.model.KeyboardLayer
import com.thor.core.model.NavDirection
import com.thor.core.model.ThorKeyboardLayout
import com.dualscreen.launcher.CompanionTheme

/**
 * WEMU's guest keyboard state, kept independent from Android's IME.
 *
 * Each key is sent immediately to the active X/Win32 input path. [text] is only
 * an echo of what was sent; it is never a buffer that has to be committed, so a
 * game can react to every key exactly as it would to a physical keyboard.
 */
@Stable
class CompanionGuestKeyboard(
    private val injectKey: (KeyEvent) -> Unit,
    private val injectCharacter: (Char) -> Boolean,
    private val readClipboard: () -> String?,
) {
    var visible by mutableStateOf(false)
        private set
    var text by mutableStateOf("")
        private set
    var layer by mutableStateOf(KeyboardLayer.LETTERS)
        private set
    var shifted by mutableStateOf(false)
        private set
    var cursor by mutableStateOf(KeyboardCursor(0, 0))
        private set

    private val keyMap = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD)

    fun show() {
        text = ""
        layer = KeyboardLayer.LETTERS
        shifted = false
        cursor = KeyboardCursor(0, 0)
        visible = true
    }

    fun hide() {
        visible = false
    }

    fun toggle() = if (visible) hide() else show()

    fun move(direction: NavDirection) {
        cursor = ThorKeyboardLayout.move(layer, cursor.row, cursor.column, direction)
    }

    fun pressFocused() {
        ThorKeyboardLayout.keyAt(layer, cursor.row, cursor.column)?.let(::press)
    }

    fun press(key: KeyboardKey) {
        when (key) {
            is KeyboardKey.Character -> {
                val character = key.resolve(shifted)
                if (type(character)) text += character
                shifted = false
            }

            KeyboardKey.Space -> if (type(' ')) text += ' '
            KeyboardKey.Backspace -> {
                send(KeyEvent.KEYCODE_DEL)
                text = text.dropLast(1)
            }

            KeyboardKey.Shift -> shifted = !shifted
            KeyboardKey.Layer -> {
                layer = if (layer == KeyboardLayer.LETTERS) {
                    KeyboardLayer.SYMBOLS
                } else {
                    KeyboardLayer.LETTERS
                }
                cursor = KeyboardCursor(
                    cursor.row.coerceAtMost(ThorKeyboardLayout.rows(layer).lastIndex),
                    0,
                )
            }

            KeyboardKey.Enter -> {
                send(KeyEvent.KEYCODE_ENTER)
                hide()
            }

            KeyboardKey.Cancel -> hide()
            KeyboardKey.Clipboard -> paste(readClipboard().orEmpty())
        }
    }

    private fun paste(value: String) {
        // A bounded paste protects the UI thread from an accidentally enormous
        // clipboard while still covering names, commands, paths and chat.
        value.take(MAX_PASTE_CHARACTERS).forEach { character ->
            when (character) {
                '\r' -> Unit
                '\n' -> send(KeyEvent.KEYCODE_ENTER)
                else -> if (type(character)) text += character
            }
        }
    }

    private fun type(character: Char): Boolean {
        if (injectCharacter(character)) return true
        val events = keyMap.getEvents(charArrayOf(character)) ?: return false
        events.forEach(injectKey)
        return true
    }

    private fun send(keyCode: Int) {
        injectKey(KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
        injectKey(KeyEvent(KeyEvent.ACTION_UP, keyCode))
    }

    private companion object {
        const val MAX_PASTE_CHARACTERS = 1_024
    }
}

/** The complete two-layer WEMU keyboard, drawn entirely on the lower panel. */
@Composable
fun CompanionGuestKeyboardView(
    keyboard: CompanionGuestKeyboard,
    modifier: Modifier = Modifier,
) {
    val colors = CompanionTheme.colors
    val shapes = CompanionTheme.shapes
    val rows = ThorKeyboardLayout.rows(keyboard.layer)

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.82f))
            .clickable(onClick = keyboard::hide),
        contentAlignment = Alignment.BottomCenter,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(colors.surface)
                .clickable(enabled = false) {}
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(KEY_GAP.dp),
        ) {
            Text(
                text = "TYPING INTO THE GAME",
                style = CompanionTheme.type.monoSmall,
                color = colors.accentText,
                fontWeight = FontWeight.Bold,
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(shapes.control)
                    .background(colors.card)
                    .border(1.dp, colors.outline, shapes.control)
                    .padding(horizontal = 12.dp, vertical = 9.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = keyboard.text.ifEmpty { "Input is sent immediately" },
                    color = if (keyboard.text.isEmpty()) colors.textMuted else colors.text,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f),
                )
                Box(
                    modifier = Modifier
                        .size(width = 2.dp, height = 21.dp)
                        .background(colors.accent),
                )
            }

            rows.forEachIndexed { rowIndex, keys ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(KEY_GAP.dp),
                ) {
                    keys.forEachIndexed { columnIndex, key ->
                        KeyboardKeyCap(
                            key = key,
                            layer = keyboard.layer,
                            shifted = keyboard.shifted,
                            focused = rowIndex == keyboard.cursor.row &&
                                columnIndex == keyboard.cursor.column,
                            onClick = { keyboard.press(key) },
                            modifier = Modifier.weight(key.keyWeight()),
                        )
                    }
                }
            }

            Text(
                text = "A SELECT  ·  B DELETE  ·  X SPACE  ·  Y SHIFT  ·  L2/R2 SYMBOLS  ·  START CLOSE",
                style = CompanionTheme.type.monoSmall,
                color = colors.textMuted,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun KeyboardKeyCap(
    key: KeyboardKey,
    layer: KeyboardLayer,
    shifted: Boolean,
    focused: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = CompanionTheme.colors
    val shapes = CompanionTheme.shapes
    val active = key == KeyboardKey.Shift && shifted
    val border = if (focused) colors.accent else colors.outline
    val fill = if (active) colors.accent.copy(alpha = 0.18f) else colors.card
    val tint = if (focused || active) colors.accentText else colors.text

    Box(
        modifier = modifier
            .height(KEY_HEIGHT.dp)
            .clip(shapes.control)
            .background(fill)
            .border(1.dp, border, shapes.control)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        val icon = key.keyIcon()
        if (icon != null) {
            Icon(
                imageVector = icon,
                contentDescription = key.keyDescription(),
                tint = tint,
                modifier = Modifier.size(20.dp),
            )
        } else {
            Text(
                text = key.keyLabel(shifted, layer),
                color = tint,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
                maxLines = 1,
            )
        }
    }
}

private fun KeyboardKey.keyIcon(): ImageVector? = when (this) {
    is KeyboardKey.Character -> null
    KeyboardKey.Space -> Icons.Rounded.SpaceBar
    KeyboardKey.Backspace -> Icons.AutoMirrored.Rounded.Backspace
    KeyboardKey.Shift -> Icons.Rounded.KeyboardCapslock
    KeyboardKey.Layer -> null
    KeyboardKey.Enter -> Icons.AutoMirrored.Rounded.KeyboardReturn
    KeyboardKey.Cancel -> Icons.Rounded.Close
    KeyboardKey.Clipboard -> Icons.Rounded.ContentPaste
}

private fun KeyboardKey.keyLabel(shifted: Boolean, layer: KeyboardLayer): String = when (this) {
    is KeyboardKey.Character -> resolve(shifted).toString()
    KeyboardKey.Layer -> if (layer == KeyboardLayer.LETTERS) "?123" else "ABC"
    else -> ""
}

private fun KeyboardKey.keyDescription(): String = when (this) {
    is KeyboardKey.Character -> resolve(false).toString()
    KeyboardKey.Space -> "Space"
    KeyboardKey.Backspace -> "Delete"
    KeyboardKey.Shift -> "Shift"
    KeyboardKey.Layer -> "Symbols"
    KeyboardKey.Enter -> "Enter"
    KeyboardKey.Cancel -> "Close"
    KeyboardKey.Clipboard -> "Paste"
}

private fun KeyboardKey.keyWeight(): Float = when (this) {
    KeyboardKey.Space -> 4f
    KeyboardKey.Enter, KeyboardKey.Backspace, KeyboardKey.Shift, KeyboardKey.Layer -> 1.5f
    KeyboardKey.Cancel -> 1.2f
    else -> 1f
}

private const val KEY_HEIGHT = 42
private const val KEY_GAP = 5
