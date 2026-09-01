package com.dualscreen.launcher

import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.ColorMatrix
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

private val LibraryCardOutline = Color(0xFF5A482A)
private val LibraryCardFooter = Color(0xFF141611)

@Composable
internal fun GamesPage(
    installed: Map<String, InstalledGame>,
    progress: Map<String, InstallProgress>,
    saveProgress: Map<String, SaveImportProgress>,
    onSelectGame: (SupportedGame) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(start = 20.dp, end = 20.dp, top = 13.dp, bottom = 10.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.Bottom,
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(3.dp),
            ) {
                Text(
                    "YOUR LIBRARY",
                    color = LauncherAccent,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 8.sp,
                    letterSpacing = 1.5.sp,
                )
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        "Games",
                        fontSize = 25.sp,
                        lineHeight = 27.sp,
                        fontWeight = FontWeight.Bold,
                    )
                    Spacer(Modifier.width(15.dp))
                    Text(
                        "Select a title to install, configure, or play.",
                        modifier = Modifier.padding(bottom = 3.dp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 10.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            LibrarySummary(
                ready = installed.size,
                supported = GameCatalog.games.size,
            )
        }
        Spacer(Modifier.height(14.dp))
        LazyRow(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.Top,
        ) {
            items(GameCatalog.games, key = { it.id }) { game ->
                BrowseGameCard(
                    game = game,
                    installed = installed[game.id],
                    progress = progress[game.id],
                    saveProgress = saveProgress[game.id],
                    modifier = Modifier
                        .width(164.dp)
                        .fillParentMaxHeight(),
                    onClick = { onSelectGame(game) },
                )
            }
        }
    }
}

@Composable
private fun LibrarySummary(ready: Int, supported: Int) {
    Row(
        modifier = Modifier
            .background(Color(0xB512140F))
            .border(1.dp, LibraryCardOutline, RectangleShape)
            .padding(horizontal = 13.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(9.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(6.dp).background(if (ready == 0) LauncherTextMuted else LauncherSuccess))
        Text(
            "$ready READY",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 8.sp,
            letterSpacing = 0.5.sp,
        )
        Text("•", color = LauncherAccent, fontSize = 8.sp)
        Text(
            "$supported SUPPORTED",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 8.sp,
            letterSpacing = 0.5.sp,
        )
    }
}

@Composable
private fun BrowseGameCard(
    game: SupportedGame,
    installed: InstalledGame?,
    progress: InstallProgress?,
    saveProgress: SaveImportProgress?,
    modifier: Modifier,
    onClick: () -> Unit,
) {
    val state = resolveGameInstallState(installed, progress, saveProgress)
    val inactive = state == GameInstallState.NOT_INSTALLED
    val desaturated = ColorMatrix().apply {
        setToSaturation(if (inactive) 0.06f else 0.9f)
    }
    val borderColor by animateColorAsState(
        targetValue = when (state) {
            GameInstallState.UPDATING_MOD,
            GameInstallState.INSTALLING,
            GameInstallState.IMPORTING_SAVE,
            -> LauncherAccent.copy(alpha = 0.82f)
            GameInstallState.ERROR -> LauncherDanger.copy(alpha = 0.82f)
            GameInstallState.READY -> LibraryCardOutline
            GameInstallState.NOT_INSTALLED -> LauncherOutline
        },
        label = "game-card-border",
    )

    BoxWithConstraints(modifier = modifier) {
        val coverHeight = browseCoverHeight(maxHeight)
        PressScaleContainer(
            onClick = onClick,
            modifier = Modifier.fillMaxSize(),
        ) {
            Card(
                colors = CardDefaults.cardColors(containerColor = LauncherPanel),
                shape = RectangleShape,
                modifier = Modifier
                    .fillMaxSize()
                    .border(1.dp, borderColor, RectangleShape),
            ) {
                Column(Modifier.fillMaxSize()) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(coverHeight)
                            .background(Color.Black),
                    ) {
                        Image(
                            painter = painterResource(gameArtwork(game.id)),
                            contentDescription = "${game.title} cover",
                            contentScale = ContentScale.Fit,
                            colorFilter = if (inactive) ColorFilter.colorMatrix(desaturated) else null,
                            modifier = Modifier.fillMaxSize(),
                        )
                        if (inactive) {
                            Box(
                                Modifier
                                    .fillMaxSize()
                                    .background(Color(0x6B4B4D49)),
                            )
                        }
                        Box(
                            Modifier
                                .fillMaxWidth()
                                .height(45.dp)
                                .align(Alignment.BottomCenter)
                                .background(
                                    Brush.verticalGradient(
                                        listOf(Color.Transparent, Color(0xE611120F)),
                                    ),
                                ),
                        )
                        if (game.isBeta) {
                            BetaBadge(
                                modifier = Modifier
                                    .align(Alignment.TopEnd)
                                    .padding(7.dp),
                            )
                        }
                    }
                    GameCardFooter(
                        title = gameShortTitle(game.id),
                        state = state,
                        progress = progress,
                        saveProgress = saveProgress,
                        modifier = Modifier
                            .fillMaxWidth()
                            .weight(1f),
                    )
                }
            }
        }
    }
}

@Composable
private fun GameCardFooter(
    title: String,
    state: GameInstallState,
    progress: InstallProgress?,
    saveProgress: SaveImportProgress?,
    modifier: Modifier = Modifier,
) {
    val (status, statusColor) = when (state) {
        GameInstallState.NOT_INSTALLED -> "NOT INSTALLED" to Color(0xFF85867F)
        GameInstallState.READY -> "INSTALLED" to LauncherAccent.copy(alpha = 0.78f)
        GameInstallState.INSTALLING -> "INSTALLING" to LauncherAccent
        GameInstallState.UPDATING_MOD -> "MOD UPDATE" to LauncherAccent
        GameInstallState.IMPORTING_SAVE -> "IMPORTING SAVE" to LauncherAccent
        GameInstallState.ERROR -> "NEEDS ATTENTION" to LauncherDanger
    }
    val activeFraction = when (state) {
        GameInstallState.UPDATING_MOD -> modUpdateFraction(progress)
        GameInstallState.INSTALLING -> progress?.fraction ?: 0f
        GameInstallState.IMPORTING_SAVE -> saveProgress?.fraction ?: 0f
        else -> null
    }

    Box(
        modifier = modifier
            .background(
                Brush.verticalGradient(
                    listOf(Color(0xFF181A14), LibraryCardFooter),
                ),
            )
            .border(width = 1.dp, color = LibraryCardOutline.copy(alpha = 0.72f)),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(start = 11.dp, end = 10.dp, top = 9.dp, bottom = 8.dp),
        ) {
            Text(
                title,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(29.dp)
                    .padding(end = 17.dp),
                fontWeight = FontWeight.Bold,
                fontSize = 12.sp,
                lineHeight = 14.sp,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            Spacer(Modifier.height(5.dp))
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Box(Modifier.size(5.dp).background(statusColor))
                Text(
                    status,
                    color = statusColor,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 7.sp,
                    letterSpacing = 0.4.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            if (activeFraction != null) {
                Spacer(Modifier.weight(1f))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        if (state == GameInstallState.UPDATING_MOD) "UPDATING MOD" else "IN PROGRESS",
                        color = LauncherAccent,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 7.sp,
                        maxLines = 1,
                    )
                    Text(
                        "${installPercent(activeFraction)}%",
                        color = LauncherAccent,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 7.sp,
                    )
                }
                Spacer(Modifier.height(4.dp))
                LauncherProgress(
                    fraction = activeFraction,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(4.dp),
                )
            }
        }
        Icon(
            Icons.Default.MoreVert,
            contentDescription = "Open $title",
            tint = LauncherAccent,
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = 8.dp, end = 4.dp)
                .size(17.dp),
        )
    }
}

/** Keep the reference's tall artwork while reserving a stable metadata footer. */
internal fun browseCoverHeight(cardHeight: Dp): Dp =
    (cardHeight - 82.dp).coerceAtLeast(104.dp)
