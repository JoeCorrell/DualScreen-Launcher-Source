package com.dualscreen.launcher

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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.ColorMatrix
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.sp

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
            .padding(start = 16.dp, end = 16.dp, top = 7.dp, bottom = 8.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(1.dp)) {
                Text(
                    "YOUR LIBRARY",
                    color = LauncherAccent,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 8.sp,
                    letterSpacing = 1.6.sp,
                )
                Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text("Games", fontSize = 24.sp, lineHeight = 26.sp, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.width(11.dp))
                    Text(
                        "Select a title to install, configure, or play.",
                        modifier = Modifier.weight(1f),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 10.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Text(
                "${installed.size} READY  •  ${GameCatalog.games.size} SUPPORTED",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                fontFamily = FontFamily.Monospace,
                fontSize = 9.sp,
            )
        }
        Spacer(Modifier.height(7.dp))
        LazyRow(
            modifier = Modifier.fillMaxWidth().weight(1f),
            horizontalArrangement = Arrangement.spacedBy(11.dp),
            verticalAlignment = Alignment.Top,
        ) {
            items(GameCatalog.games, key = { it.id }) { game ->
                BrowseGameCard(
                    game = game,
                    installed = installed[game.id],
                    progress = progress[game.id],
                    saveProgress = saveProgress[game.id],
                    modifier = Modifier.width(158.dp).fillParentMaxHeight(),
                    onClick = { onSelectGame(game) },
                )
            }
        }
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
    val fraction = when (state) {
        GameInstallState.IMPORTING_SAVE -> saveProgress?.fraction ?: 0f
        GameInstallState.INSTALLING -> progress?.fraction ?: 0f
        else -> 0f
    }
    val desaturated = ColorMatrix().apply { setToSaturation(if (inactive) 0.08f else 0.84f) }

    BoxWithConstraints(modifier = modifier) {
        val coverHeight = browseCoverHeight(maxHeight)
        PressScaleContainer(
            onClick = onClick,
            modifier = Modifier.fillMaxSize(),
        ) {
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF171812)),
            shape = RoundedCornerShape(13.dp),
            modifier = Modifier
                .fillMaxSize()
                .border(
                    1.dp,
                    if (state == GameInstallState.READY) Color(0xFF545140) else Color(0xFF37372F),
                    RoundedCornerShape(13.dp),
                ),
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
                                .background(Color(0x624E504E)),
                        )
                    }
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .height(52.dp)
                            .align(Alignment.BottomCenter)
                            .background(
                                Brush.verticalGradient(
                                    listOf(Color.Transparent, Color(0xE810110E)),
                                ),
                            ),
                    )
                    if (game.isBeta) {
                        Text(
                            "BETA",
                            modifier = Modifier
                                .align(Alignment.TopEnd)
                                .padding(7.dp)
                                .clip(RoundedCornerShape(6.dp))
                                .background(Color(0xFFE6A144))
                                .padding(horizontal = 7.dp, vertical = 3.dp),
                            color = Color(0xFF17120A),
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold,
                            fontSize = 8.sp,
                        )
                    }
                }
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .padding(horizontal = 11.dp, vertical = 9.dp),
                    verticalArrangement = Arrangement.SpaceBetween,
                ) {
                    Column(verticalArrangement = Arrangement.spacedBy(5.dp)) {
                        Text(
                            gameShortTitle(game.id),
                            modifier = Modifier.height(30.dp),
                            fontWeight = FontWeight.Bold,
                            fontSize = 13.sp,
                            lineHeight = 15.sp,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                        StatusPill(state)
                    }
                    if (state == GameInstallState.INSTALLING || state == GameInstallState.IMPORTING_SAVE) {
                        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            LinearProgressIndicator(
                                progress = { fraction },
                                modifier = Modifier.fillMaxWidth().height(3.dp).clip(RoundedCornerShape(3.dp)),
                                color = LauncherAccent,
                                trackColor = Color(0xFF34342C),
                            )
                            Text(
                                "${installPercent(fraction)}%  •  VIEW DETAILS",
                                color = LauncherAccent,
                                fontFamily = FontFamily.Monospace,
                                fontSize = 8.sp,
                            )
                        }
                    } else {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                if (inactive) "SET UP GAME" else "VIEW GAME",
                                color = if (inactive) Color(0xFFB4B0A5) else LauncherAccent,
                                fontFamily = FontFamily.Monospace,
                                fontSize = 8.sp,
                                fontWeight = FontWeight.Bold,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                            Icon(
                                Icons.Default.ChevronRight,
                                contentDescription = null,
                                tint = if (inactive) Color(0xFF7D7B73) else LauncherAccent,
                                modifier = Modifier.size(17.dp),
                            )
                        }
                    }
                }
            }
        }
    }
}
}

/**
 * Always preserve enough room for a two-line title, the install-status pill,
 * and the bottom action/progress row. Only the artwork contracts.
 */
internal fun browseCoverHeight(cardHeight: Dp): Dp =
    (cardHeight - 116.dp).coerceAtLeast(120.dp)
