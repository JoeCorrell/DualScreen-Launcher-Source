package com.dualscreen.launcher.access

import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Key
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dualscreen.launcher.LauncherAccent
import com.dualscreen.launcher.LauncherBackground
import com.dualscreen.launcher.LauncherDanger
import com.dualscreen.launcher.LauncherOutline
import com.dualscreen.launcher.LauncherPanel
import com.dualscreen.launcher.LauncherPanelRaised
import com.dualscreen.launcher.LauncherTextMuted
import com.dualscreen.launcher.PrimaryAction
import kotlinx.coroutines.launch

@Composable
fun AccessGate(
    access: AccessManager,
    content: @Composable () -> Unit,
) {
    val state by access.state.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    var initialVerificationStarted by remember { mutableStateOf(false) }

    LaunchedEffect(access) {
        if (!initialVerificationStarted) {
            initialVerificationStarted = true
            access.verifyNow()
        }
    }

    if (state is AccessState.Granted) {
        content()
        return
    }

    AccessLockedScreen(
        state = state,
        onPatreon = {
            scope.launch {
                access.beginPatreonLogin().onSuccess { uri ->
                    runCatching {
                        context.startActivity(
                            Intent(Intent.ACTION_VIEW, uri)
                                .addCategory(Intent.CATEGORY_BROWSABLE),
                        )
                    }.onFailure {
                        Toast.makeText(context, "No web browser is available for Patreon sign-in.", Toast.LENGTH_LONG).show()
                    }
                }
            }
        },
        onRedeem = { code -> scope.launch { access.redeemCode(code) } },
        onRetry = { scope.launch { access.verifyNow() } },
    )
}

@Composable
private fun AccessLockedScreen(
    state: AccessState,
    onPatreon: () -> Unit,
    onRedeem: (String) -> Unit,
    onRetry: () -> Unit,
) {
    var code by rememberSaveable { mutableStateOf("") }
    val waiting = state is AccessState.Checking
    val configured = state !is AccessState.ConfigurationError
    val statusTint = when (state) {
        is AccessState.Denied,
        is AccessState.ConfigurationError,
        -> LauncherDanger
        is AccessState.Unavailable -> LauncherAccent
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.linearGradient(
                    listOf(Color(0xFF181A12), LauncherBackground, Color(0xFF10120E)),
                ),
            ),
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(3.dp)
                .background(LauncherAccent),
        )
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(top = 3.dp),
        ) {
            AccessHeader(waiting = waiting, statusTint = statusTint)
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 32.dp, vertical = 22.dp),
                contentAlignment = Alignment.Center,
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .widthIn(max = 920.dp)
                        .fillMaxHeight()
                        .border(1.dp, LauncherOutline, RectangleShape)
                        .background(LauncherPanel),
                ) {
                    AccessOverview(
                        state = state,
                        statusTint = statusTint,
                        waiting = waiting,
                        modifier = Modifier
                            .weight(0.92f)
                            .fillMaxHeight()
                            .padding(28.dp),
                    )
                    Box(
                        Modifier
                            .fillMaxHeight()
                            .width(1.dp)
                            .background(LauncherOutline),
                    )
                    AccessActions(
                        code = code,
                        onCodeChange = { code = it.uppercase().take(MAX_CODE_INPUT) },
                        waiting = waiting,
                        configured = configured,
                        unavailable = state is AccessState.Unavailable,
                        onPatreon = onPatreon,
                        onRedeem = {
                            onRedeem(code)
                            code = ""
                        },
                        onRetry = onRetry,
                        modifier = Modifier
                            .weight(1.08f)
                            .fillMaxHeight()
                            .padding(28.dp),
                    )
                }
            }
        }
    }
}

@Composable
private fun AccessHeader(waiting: Boolean, statusTint: Color) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(58.dp)
            .background(Color(0xE614160F))
            .padding(horizontal = 24.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(32.dp)
                .background(LauncherAccent)
                .border(1.dp, Color(0xFFFFD58D), RectangleShape),
            contentAlignment = Alignment.Center,
        ) {
            androidx.compose.material3.Icon(
                imageVector = Icons.Default.Lock,
                contentDescription = null,
                tint = Color(0xFF19130A),
                modifier = Modifier.size(18.dp),
            )
        }
        Spacer(Modifier.width(11.dp))
        Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
            Text(
                text = "DUALSCREEN",
                color = LauncherAccent,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 9.sp,
                letterSpacing = 1.7.sp,
            )
            Text(
                text = "Secure Access",
                color = MaterialTheme.colorScheme.onBackground,
                fontWeight = FontWeight.SemiBold,
                fontSize = 16.sp,
            )
        }
        Spacer(Modifier.weight(1f))
        Row(
            modifier = Modifier
                .background(statusTint.copy(alpha = 0.11f))
                .border(1.dp, statusTint.copy(alpha = 0.45f), RectangleShape)
                .padding(horizontal = 11.dp, vertical = 7.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            if (waiting) {
                CircularProgressIndicator(
                    modifier = Modifier.size(11.dp),
                    color = LauncherAccent,
                    strokeWidth = 2.dp,
                )
            } else {
                Box(Modifier.size(7.dp).background(statusTint))
            }
            Text(
                text = if (waiting) "VERIFYING ACCESS" else "ACCESS LOCKED",
                color = if (waiting) LauncherAccent else statusTint,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 9.sp,
                letterSpacing = 1.1.sp,
            )
        }
    }
}

@Composable
private fun AccessOverview(
    state: AccessState,
    statusTint: Color,
    waiting: Boolean,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            text = "MEMBER ACCESS",
            color = LauncherAccent,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 10.sp,
            letterSpacing = 1.8.sp,
        )
        Spacer(Modifier.height(6.dp))
        Text(
            text = "Unlock your\nDualScreen library",
            color = MaterialTheme.colorScheme.onBackground,
            fontWeight = FontWeight.Bold,
            fontSize = 30.sp,
            lineHeight = 33.sp,
        )
        Spacer(Modifier.height(10.dp))
        Text(
            text = "Continue with an active paid Patreon membership or a one-time code issued by the developer.",
            color = LauncherTextMuted,
            fontSize = 12.sp,
            lineHeight = 17.sp,
        )
        Spacer(Modifier.height(20.dp))
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(statusTint.copy(alpha = 0.08f))
                .border(1.dp, statusTint.copy(alpha = 0.38f), RectangleShape)
                .padding(horizontal = 13.dp, vertical = 12.dp),
            verticalAlignment = Alignment.Top,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (waiting) {
                CircularProgressIndicator(
                    modifier = Modifier.size(15.dp),
                    color = LauncherAccent,
                    strokeWidth = 2.dp,
                )
            } else {
                Box(
                    Modifier
                        .padding(top = 4.dp)
                        .size(8.dp)
                        .background(statusTint),
                )
            }
            Text(
                text = state.message,
                color = if (state is AccessState.Denied || state is AccessState.ConfigurationError) {
                    LauncherDanger
                } else {
                    MaterialTheme.colorScheme.onSurface
                },
                fontSize = 12.sp,
                lineHeight = 17.sp,
                fontWeight = FontWeight.Medium,
            )
        }
        Spacer(Modifier.height(16.dp))
        Text(
            text = "ONLINE VERIFICATION  •  SERVER-SIGNED ACCESS",
            color = LauncherTextMuted,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Medium,
            fontSize = 8.sp,
            letterSpacing = 0.8.sp,
        )
    }
}

@Composable
private fun AccessActions(
    code: String,
    onCodeChange: (String) -> Unit,
    waiting: Boolean,
    configured: Boolean,
    unavailable: Boolean,
    onPatreon: () -> Unit,
    onRedeem: () -> Unit,
    onRetry: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.Center,
    ) {
        if (!configured) {
            Text(
                text = "CONFIGURATION REQUIRED",
                color = LauncherDanger,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp,
                letterSpacing = 1.3.sp,
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = "This build cannot contact the secure access service. Install a correctly configured release.",
                color = MaterialTheme.colorScheme.onSurface,
                fontSize = 13.sp,
                lineHeight = 18.sp,
            )
            return
        }

        Text(
            text = "PATREON MEMBERS",
            color = MaterialTheme.colorScheme.onBackground,
            fontWeight = FontWeight.SemiBold,
            fontSize = 15.sp,
        )
        Text(
            text = "Sign in to verify a currently active paid membership.",
            color = LauncherTextMuted,
            fontSize = 11.sp,
        )
        Spacer(Modifier.height(12.dp))
        PrimaryAction(
            label = "SIGN IN WITH PATREON",
            icon = Icons.Default.Lock,
            onClick = onPatreon,
            enabled = !waiting,
            primary = true,
            modifier = Modifier.fillMaxWidth(),
        )
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 13.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Box(Modifier.weight(1f).height(1.dp).background(LauncherOutline))
            Text(
                text = "OR USE AN OWNER CODE",
                color = LauncherTextMuted,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 8.sp,
                letterSpacing = 0.8.sp,
            )
            Box(Modifier.weight(1f).height(1.dp).background(LauncherOutline))
        }
        OutlinedTextField(
            value = code,
            onValueChange = onCodeChange,
            enabled = !waiting,
            label = { Text("One-time access code") },
            placeholder = { Text("XXXX-XXXX-XXXX-XXXX") },
            singleLine = true,
            shape = RectangleShape,
            textStyle = MaterialTheme.typography.bodyMedium.copy(
                color = MaterialTheme.colorScheme.onSurface,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Medium,
                letterSpacing = 0.8.sp,
            ),
            colors = OutlinedTextFieldDefaults.colors(
                focusedTextColor = MaterialTheme.colorScheme.onSurface,
                unfocusedTextColor = MaterialTheme.colorScheme.onSurface,
                focusedBorderColor = LauncherAccent,
                unfocusedBorderColor = LauncherOutline,
                focusedLabelColor = LauncherAccent,
                unfocusedLabelColor = LauncherTextMuted,
                focusedPlaceholderColor = LauncherTextMuted.copy(alpha = 0.65f),
                unfocusedPlaceholderColor = LauncherTextMuted.copy(alpha = 0.65f),
                cursorColor = LauncherAccent,
                disabledBorderColor = LauncherOutline.copy(alpha = 0.55f),
                disabledTextColor = LauncherTextMuted,
                disabledLabelColor = LauncherTextMuted.copy(alpha = 0.6f),
            ),
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(10.dp))
        PrimaryAction(
            label = "REDEEM CODE",
            icon = Icons.Default.Key,
            onClick = onRedeem,
            enabled = !waiting && code.isNotBlank(),
            modifier = Modifier.fillMaxWidth(),
        )
        if (unavailable) {
            Spacer(Modifier.height(8.dp))
            PrimaryAction(
                label = "RETRY VERIFICATION",
                icon = Icons.Default.Refresh,
                onClick = onRetry,
                enabled = !waiting,
                modifier = Modifier.fillMaxWidth(),
            )
        } else {
            Spacer(Modifier.height(12.dp))
            Text(
                text = "Membership is rechecked securely. Each owner code can be redeemed once.",
                color = LauncherTextMuted,
                fontSize = 9.sp,
                lineHeight = 13.sp,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

private const val MAX_CODE_INPUT = 128
