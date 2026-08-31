package com.wemu.session.secondscreen

import androidx.compose.ui.platform.LocalContext
import androidx.compose.foundation.clickable
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.thor.core.designsystem.modifier.SurfaceLevel
import com.thor.core.designsystem.theme.ThorTheme
import com.thor.core.designsystem.modifier.thorSurface

@Composable
fun ModsPage(modifier: Modifier = Modifier) {
    val connection by SecondScreen.connectionState.collectAsStateWithLifecycle()
    val gameId by SecondScreen.gameId.collectAsStateWithLifecycle()

    val context = LocalContext.current
    LaunchedEffect(Unit) {
        StardewPrefs.load(context)
        SecondScreen.start()
    }

    DisposableEffect(Unit) {
        Discovery.start()
        onDispose { Discovery.stop() }
    }

    Box(modifier = modifier.fillMaxSize()) {
        when {
            gameId == SupportedGame.STARDEW.id -> StardewScreen(Modifier.fillMaxSize())
            gameId == SupportedGame.VALHEIM.id -> ValheimScreen(Modifier.fillMaxSize())
            else -> GameList(connection = connection, gameId = gameId)
        }
    }
}

@Composable
private fun FoundGames() {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val found by Discovery.found.collectAsStateWithLifecycle()
    if (found.isEmpty()) return

    Column(modifier = Modifier.fillMaxWidth().padding(top = dimens.spacingSmall)) {
        Text(
            text = "Found on this network",
            style = MaterialTheme.typography.labelSmall,
            color = colors.onSurfaceVariant,
        )
        found.forEach { game ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = dimens.spacingTiny)
                    .thorSurface(shape = ThorTheme.shapes.small, level = SurfaceLevel.RAISED)
                    .clickable {
                        StardewPrefs.update { it.copy(host = game.host) }
                        SecondScreen.connectTo(game.host)
                    }
                    .padding(horizontal = 10.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = game.save.ifEmpty { game.game.ifEmpty { "A game" } },
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium,
                        color = colors.onSurface,
                    )
                    Text(
                        text = game.host,
                        style = MaterialTheme.typography.labelSmall,
                        color = colors.onSurfaceVariant,
                    )
                }
                Text(
                    text = "Connect",
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Medium,
                    color = colors.cursor,
                )
            }
        }
    }
}

private enum class SupportedGame(val id: String, val title: String, val detail: String) {
    STARDEW(
        id = "stardew",
        title = "Stardew Valley",
        detail = "Day, map, machines, bundles, journal and bag",
    ),
    VALHEIM(
        id = "valheim",
        title = "Valheim",
        detail = "Health, stamina, food timers and your inventory",
    ),
}

@Composable
private fun GameList(connection: ConnectionState, gameId: String) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(dimens.spacing),
        verticalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
    ) {
        Text(
            text = "Second screen",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            color = colors.onSurface,
        )

        Text(
            text = when {
                connection is ConnectionState.Connected && gameId.isEmpty() ->
                    "A mod is connected. This fills in once a save is loaded."
                connection is ConnectionState.Connected ->
                    "The mod reported \"$gameId\", which this build has no screen for yet."
                connection is ConnectionState.Connecting ->
                    "Listening on port ${SecondScreen.PORT} for a game to start."
                else ->
                    "Install a second-screen mod into one of these and start it. " +
                        "Nothing needs setting up on this side."
            },
            style = MaterialTheme.typography.bodySmall,
            color = colors.onSurfaceVariant,
        )

        FoundGames()

        SupportedGame.entries.forEach { game ->
            val live = connection is ConnectionState.Connected && gameId == game.id

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .thorSurface(
                        shape = ThorTheme.shapes.panel,
                        color = if (live) colors.cursor.copy(alpha = 0.12f) else colors.surface,
                        level = SurfaceLevel.RAISED,
                    )
                    .padding(dimens.spacing),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = game.title,
                        style = MaterialTheme.typography.bodyLarge,
                        fontWeight = FontWeight.Medium,
                        color = colors.onSurface,
                    )
                    Text(
                        text = game.detail,
                        style = MaterialTheme.typography.labelSmall,
                        color = colors.onSurfaceVariant,
                    )
                }

                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .clip(ThorTheme.shapes.pill)
                        .background(
                            if (live) colors.cursor
                            else colors.onSurfaceVariant.copy(alpha = 0.35f),
                        ),
                )
            }
        }
    }
}
