package com.dualscreen.launcher

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Gamepad
import androidx.compose.material.icons.filled.MenuBook
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Smartphone
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

internal val LauncherAccent = Color(0xFFF4AA43)
internal val LauncherSuccess = Color(0xFF83C99A)
internal val LauncherDanger = Color(0xFFE97A69)
internal val LauncherPanel = Color(0xFF171812)
internal val LauncherPanelRaised = Color(0xFF202119)

@Composable
internal fun LauncherHeader(
    selected: LauncherNavItem,
    onGames: () -> Unit,
    onGuide: () -> Unit,
    onSettings: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(56.dp)
            .background(Color(0xEA11120F))
            .border(0.5.dp, Color(0xFF32332A))
            .padding(horizontal = 18.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(30.dp)
                .clip(RoundedCornerShape(9.dp))
                .background(Brush.linearGradient(listOf(Color(0xFFFFC15C), Color(0xFFD88428)))),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Default.Gamepad,
                contentDescription = null,
                tint = Color(0xFF19130A),
                modifier = Modifier.size(19.dp),
            )
        }
        Spacer(Modifier.width(10.dp))
        Column {
            Text(
                "DUALSCREEN",
                color = LauncherAccent,
                fontSize = 9.sp,
                lineHeight = 10.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.7.sp,
            )
            Text("Launcher", fontSize = 17.sp, lineHeight = 18.sp, fontWeight = FontWeight.SemiBold)
        }
        Spacer(Modifier.weight(1f))
        HeaderDestination(
            label = "Games",
            icon = Icons.Default.SportsEsports,
            selected = selected == LauncherNavItem.GAMES,
            onClick = onGames,
        )
        Spacer(Modifier.width(6.dp))
        HeaderDestination(
            label = "Guide",
            icon = Icons.Default.MenuBook,
            selected = selected == LauncherNavItem.GUIDE,
            onClick = onGuide,
        )
        Spacer(Modifier.width(6.dp))
        HeaderDestination(
            label = "Settings",
            icon = Icons.Default.Settings,
            selected = selected == LauncherNavItem.SETTINGS,
            onClick = onSettings,
        )
    }
}

@Composable
private fun HeaderDestination(
    label: String,
    icon: ImageVector,
    selected: Boolean,
    onClick: () -> Unit,
) {
    val background by animateColorAsState(
        if (selected) Color(0xFF393225) else Color.Transparent,
        label = "navigation-background",
    )
    val foreground by animateColorAsState(
        if (selected) LauncherAccent else MaterialTheme.colorScheme.onSurfaceVariant,
        label = "navigation-foreground",
    )
    Row(
        modifier = Modifier
            .height(36.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(background)
            .clickable(onClick = onClick)
            .padding(horizontal = 11.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        Icon(icon, contentDescription = null, tint = foreground, modifier = Modifier.size(16.dp))
        Text(label, color = foreground, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
    }
}

@Composable
internal fun PageHeading(
    eyebrow: String,
    title: String,
    subtitle: String,
    modifier: Modifier = Modifier,
) {
    Column(modifier) {
        Text(
            eyebrow.uppercase(),
            color = LauncherAccent,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 9.sp,
            letterSpacing = 1.8.sp,
        )
        Text(title, fontSize = 27.sp, lineHeight = 31.sp, fontWeight = FontWeight.Bold)
        Text(
            subtitle,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontSize = 12.sp,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
internal fun LauncherCard(
    modifier: Modifier = Modifier,
    compact: Boolean = false,
    content: @Composable ColumnScope.() -> Unit,
) {
    Card(
        modifier = modifier.border(1.dp, Color(0xFF35362D), RoundedCornerShape(13.dp)),
        shape = RoundedCornerShape(13.dp),
        colors = CardDefaults.cardColors(containerColor = LauncherPanel),
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(if (compact) 11.dp else 15.dp),
            verticalArrangement = Arrangement.spacedBy(if (compact) 7.dp else 9.dp),
            content = content,
        )
    }
}

@Composable
internal fun SectionTitle(title: String, detail: String? = null) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(title, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        if (detail != null) {
            Text(
                detail,
                fontSize = 11.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
internal fun StatusPill(state: GameInstallState) {
    val (label, tint) = when (state) {
        GameInstallState.NOT_INSTALLED -> "NOT INSTALLED" to Color(0xFF969389)
        GameInstallState.INSTALLING -> "INSTALLING" to LauncherAccent
        GameInstallState.IMPORTING_SAVE -> "IMPORTING SAVE" to LauncherAccent
        GameInstallState.READY -> "READY" to LauncherSuccess
        GameInstallState.ERROR -> "ACTION NEEDED" to LauncherDanger
    }
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .background(tint.copy(alpha = 0.13f))
            .border(1.dp, tint.copy(alpha = 0.48f), RoundedCornerShape(50))
            .padding(horizontal = 9.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(Modifier.size(6.dp).clip(RoundedCornerShape(50)).background(tint))
        Text(
            label,
            color = tint,
            fontSize = 8.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
internal fun PrimaryAction(
    label: String,
    icon: ImageVector,
    onClick: () -> Unit,
    enabled: Boolean,
    primary: Boolean = false,
    modifier: Modifier = Modifier,
) {
    if (primary) {
        Button(
            onClick = onClick,
            enabled = enabled,
            modifier = modifier.height(44.dp),
            shape = RoundedCornerShape(10.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = LauncherAccent,
                contentColor = Color(0xFF17120A),
                disabledContainerColor = Color(0xFF2C2C25),
                disabledContentColor = Color(0xFF69675E),
            ),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 11.dp),
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(6.dp))
            Text(
                label,
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    } else {
        OutlinedButton(
            onClick = onClick,
            enabled = enabled,
            modifier = modifier.height(44.dp),
            shape = RoundedCornerShape(10.dp),
            colors = ButtonDefaults.outlinedButtonColors(
                containerColor = Color(0xFF1C1D17),
                contentColor = MaterialTheme.colorScheme.onSurface,
                disabledContainerColor = Color(0xFF171812),
                disabledContentColor = Color(0xFF5F5F57),
            ),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 11.dp),
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(6.dp))
            Text(
                label,
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
internal fun InformationRow(
    label: String,
    value: String,
    emphasized: Boolean = false,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.Top,
    ) {
        Text(
            label,
            modifier = Modifier.weight(0.42f).padding(end = 8.dp),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontSize = 11.sp,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            value,
            modifier = Modifier.weight(0.58f),
            color = if (emphasized) LauncherSuccess else MaterialTheme.colorScheme.onSurface,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold,
            textAlign = TextAlign.End,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/**
 * Correctly proportioned, replaceable screenshot slots for each supported game.
 * A game-specific drawable can be wired here later without changing details layout.
 */
@Composable
internal fun DualScreenPreview(game: SupportedGame) {
    LauncherCard {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SectionTitle(
                "Dual-screen preview",
                "Scaled to the physical 1920×1080 and 1240×1080 displays",
            )
            Text(
                "LIVE MOD LAYOUT",
                color = LauncherAccent,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 8.sp,
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            ScreenPreview(
                modifier = Modifier.weight(1.55f),
                game = game,
                label = "TOP SCREEN",
                resolution = "1920 × 1080",
                aspectRatio = 16f / 9f,
                top = true,
            )
            ScreenPreview(
                modifier = Modifier.weight(0.95f),
                game = game,
                label = "BOTTOM SCREEN",
                resolution = "1240 × 1080",
                aspectRatio = 1240f / 1080f,
                top = false,
            )
        }
    }
}

@Composable
private fun ScreenPreview(
    modifier: Modifier,
    game: SupportedGame,
    label: String,
    resolution: String,
    aspectRatio: Float,
    top: Boolean,
) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(5.dp)) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(aspectRatio)
                .clip(RoundedCornerShape(9.dp))
                .background(Color.Black)
                .border(2.dp, Color(0xFF46473D), RoundedCornerShape(9.dp)),
            contentAlignment = Alignment.Center,
        ) {
            if (top) {
                Image(
                    painter = painterResource(gameArtwork(game.id)),
                    contentDescription = "$label preview",
                    contentScale = ContentScale.Crop,
                    modifier = Modifier.fillMaxSize(),
                )
                Box(
                    Modifier
                        .fillMaxSize()
                        .background(Brush.verticalGradient(listOf(Color.Transparent, Color(0xA6000000)))),
                )
                Text(
                    "GAMEPLAY",
                    modifier = Modifier.align(Alignment.BottomStart).padding(10.dp),
                    color = Color.White,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 9.sp,
                )
            } else {
                Box(
                    Modifier
                        .fillMaxSize()
                        .background(
                            Brush.linearGradient(
                                listOf(Color(0xFF2C2E24), Color(0xFF11130F)),
                            ),
                        ),
                )
                Column(
                    modifier = Modifier.align(Alignment.Center).padding(12.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Icon(
                        Icons.Default.Smartphone,
                        contentDescription = null,
                        tint = LauncherAccent,
                        modifier = Modifier.size(26.dp),
                    )
                    Text(
                        gameShortTitle(game.id),
                        color = Color.White,
                        fontWeight = FontWeight.Bold,
                        fontSize = 10.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        game.summary,
                        color = Color(0xFFB9B5A8),
                        fontSize = 8.sp,
                        lineHeight = 10.sp,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 8.sp, fontWeight = FontWeight.Bold)
            Text(resolution, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
        }
    }
}

@Composable
internal fun PressScaleContainer(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val scale by animateFloatAsState(if (pressed) 0.975f else 1f, label = "pressed-scale")
    Box(
        modifier = modifier
            .graphicsLayer { scaleX = scale; scaleY = scale }
            .clip(RoundedCornerShape(13.dp))
            .clickable(interactionSource = source, indication = null, onClick = onClick)
            .focusable(),
    ) {
        content()
    }
}
