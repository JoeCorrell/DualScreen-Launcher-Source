package com.dualscreen.launcher

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.animateContentSize
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
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Extension
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.dualscreen.launcher.switch.SwitchCatalog
import com.dualscreen.launcher.switch.SwitchContentKind
import com.dualscreen.launcher.switch.SwitchGameDefinition
import com.dualscreen.launcher.switch.SwitchGameInstallation
import com.dualscreen.launcher.switch.SwitchOperationState
import com.dualscreen.launcher.switch.SwitchPatch

@Composable
internal fun SwitchGamesPage(
    installed: Map<String, SwitchGameInstallation>,
    operation: SwitchOperationState,
    onSelectGame: (SwitchGameDefinition) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(start = 20.dp, end = 20.dp, top = 13.dp, bottom = 10.dp),
    ) {
        LibraryPageHeader(
            eyebrow = "EMBEDDED EDEN DS",
            title = "Switch Games",
            subtitle = "Select a title to add content, configure, or play.",
            ready = installed.size,
            supported = SwitchCatalog.games.size,
        )
        Spacer(Modifier.height(14.dp))
        LazyRow(
            modifier = Modifier.fillMaxWidth().weight(1f),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.Top,
        ) {
            items(SwitchCatalog.games, key = { it.id }) { game ->
                SwitchBrowseCard(
                    game = game,
                    installed = installed[game.id],
                    operation = operation.takeIf { it.gameId == game.id },
                    modifier = Modifier.width(164.dp).fillParentMaxHeight(),
                    onClick = { onSelectGame(game) },
                )
            }
        }
    }
}

@Composable
private fun SwitchBrowseCard(
    game: SwitchGameDefinition,
    installed: SwitchGameInstallation?,
    operation: SwitchOperationState?,
    modifier: Modifier,
    onClick: () -> Unit,
) {
    val inactive = installed == null
    val desaturated = ColorMatrix().apply { setToSaturation(if (inactive) 0.08f else 0.95f) }
    val borderColor by animateColorAsState(
        targetValue = when {
            operation?.error != null -> LauncherDanger.copy(alpha = 0.82f)
            operation?.running == true -> LauncherAccent.copy(alpha = 0.82f)
            installed != null -> LibraryCardOutline
            else -> LauncherOutline
        },
        label = "switch-game-card-border",
    )

    BoxWithConstraints(modifier = modifier) {
        val coverHeight = browseCoverHeight(maxHeight)
        PressScaleContainer(onClick = onClick, modifier = Modifier.fillMaxSize()) {
            Card(
                colors = CardDefaults.cardColors(containerColor = LauncherPanel),
                shape = RectangleShape,
                modifier = Modifier.fillMaxSize().border(1.dp, borderColor, RectangleShape),
            ) {
                Column(Modifier.fillMaxSize()) {
                    Box(
                        modifier = Modifier.fillMaxWidth().height(coverHeight).background(Color.Black),
                    ) {
                        Image(
                            painter = painterResource(switchGameArtwork(game.id)),
                            contentDescription = "${game.title} cover",
                            contentScale = ContentScale.Fit,
                            colorFilter = if (inactive) ColorFilter.colorMatrix(desaturated) else null,
                            modifier = Modifier.fillMaxSize(),
                        )
                        if (inactive) {
                            Box(Modifier.fillMaxSize().background(Color(0x6B4B4D49)))
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
                    }
                    SwitchGameCardFooter(
                        game = game,
                        installed = installed,
                        operation = operation,
                        modifier = Modifier.fillMaxWidth().weight(1f),
                    )
                }
            }
        }
    }
}

@Composable
private fun SwitchGameCardFooter(
    game: SwitchGameDefinition,
    installed: SwitchGameInstallation?,
    operation: SwitchOperationState?,
    modifier: Modifier = Modifier,
) {
    val status = when {
        operation?.error != null -> "NEEDS ATTENTION"
        operation?.running == true -> operation.kind?.let { "INSTALLING ${it.name}" } ?: "WORKING"
        installed != null -> "INSTALLED"
        else -> "NOT INSTALLED"
    }
    val statusColor = when {
        operation?.error != null -> LauncherDanger
        operation?.running == true -> LauncherAccent
        installed != null -> LauncherAccent.copy(alpha = 0.78f)
        else -> Color(0xFF85867F)
    }
    Box(
        modifier = modifier
            .background(Brush.verticalGradient(listOf(Color(0xFF181A14), LibraryCardFooter)))
            .border(1.dp, LibraryCardOutline.copy(alpha = 0.72f)),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(start = 11.dp, end = 10.dp, top = 9.dp, bottom = 8.dp),
        ) {
            Text(
                game.title,
                modifier = Modifier.fillMaxWidth().height(29.dp).padding(end = 17.dp),
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
            if (operation?.running == true) {
                Spacer(Modifier.weight(1f))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text(
                        operation.detail.uppercase(),
                        modifier = Modifier.weight(1f),
                        color = LauncherAccent,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 7.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        "${installPercent(operation.fraction)}%",
                        color = LauncherAccent,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 7.sp,
                    )
                }
                Spacer(Modifier.height(4.dp))
                LauncherProgress(operation.fraction, Modifier.fillMaxWidth().height(4.dp))
            }
        }
        Icon(
            Icons.Default.MoreVert,
            contentDescription = "Open ${game.title}",
            tint = LauncherAccent,
            modifier = Modifier.align(Alignment.TopEnd).padding(top = 8.dp, end = 4.dp).size(17.dp),
        )
    }
}

@Composable
internal fun SwitchGameDetailsPage(
    game: SwitchGameDefinition,
    installed: SwitchGameInstallation?,
    operation: SwitchOperationState,
    onBack: () -> Unit,
    onAddBase: () -> Unit,
    onAddUpdate: () -> Unit,
    onAddDlc: () -> Unit,
    onAddMod: () -> Unit,
    onPlay: () -> Unit,
    onImportSave: () -> Unit,
    onExportSave: () -> Unit,
    onRefresh: () -> Unit,
    onOpenSettings: () -> Unit,
    onPatchEnabled: (SwitchPatch, Boolean) -> Unit,
    onRemovePatch: (SwitchPatch) -> Unit,
) {
    val active = operation.gameId == game.id
    val busy = active && operation.running
    val overviewPanelHeight = 360.dp
    val managementPanelHeight = 220.dp
    val informationPanelHeight = 205.dp

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(11.dp),
    ) {
        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(
                    onClick = onBack,
                    modifier = Modifier.size(38.dp).border(1.dp, LauncherOutline, RectangleShape),
                ) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                }
                Spacer(Modifier.width(11.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        "SWITCH GAME  •  ${game.titleId}",
                        color = LauncherAccent,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 8.sp,
                    )
                    Text(
                        game.title,
                        fontSize = 23.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                if (installed != null) {
                    PrimaryAction(
                        label = "Play",
                        icon = Icons.Default.PlayArrow,
                        onClick = onPlay,
                        enabled = !busy,
                        primary = true,
                        modifier = Modifier.width(112.dp),
                    )
                }
            }
        }

        if (active && (operation.running || operation.error != null)) {
            item {
                LauncherCard(modifier = Modifier.animateContentSize()) {
                    Text(
                        operation.error ?: operation.detail,
                        color = if (operation.error == null) MaterialTheme.colorScheme.onSurface else LauncherDanger,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                    if (operation.running) {
                        LauncherProgress(operation.fraction, Modifier.fillMaxWidth().height(5.dp))
                    }
                }
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(11.dp),
                verticalAlignment = Alignment.Top,
            ) {
                LauncherCard(modifier = Modifier.weight(0.72f).height(overviewPanelHeight)) {
                    Box(
                        modifier = Modifier.fillMaxWidth().height(222.dp).background(Color.Black),
                        contentAlignment = Alignment.Center,
                    ) {
                        SwitchGameArtwork(game, Modifier.fillMaxSize())
                    }
                    Text(
                        game.summary,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 9.sp,
                        lineHeight = 12.sp,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        MetricBlock("TESTED", game.recommendedVersion, Modifier.weight(1f))
                        MetricBlock("RUNTIME", "EDEN DS", Modifier.weight(1f))
                    }
                }
                Column(
                    modifier = Modifier.weight(1.28f).height(overviewPanelHeight),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().weight(1f),
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        SwitchContentTile(
                            title = "Base game",
                            subtitle = installed?.let { "${it.baseVersion} • ${it.displayName}" }
                                ?: "Required before launch",
                            action = if (installed == null) "Add base NSP" else "Replace base NSP",
                            icon = Icons.Default.FolderOpen,
                            enabled = !busy,
                            onAction = onAddBase,
                            modifier = Modifier.weight(1f).fillMaxHeight(),
                        )
                        SwitchContentTile(
                            title = "Game update",
                            subtitle = installed?.installedUpdate?.let { it.version.ifBlank { it.name } }
                                ?: "No update installed",
                            action = "Add update NSP",
                            icon = Icons.Default.Download,
                            enabled = installed != null && !busy,
                            onAction = onAddUpdate,
                            modifier = Modifier.weight(1f).fillMaxHeight(),
                        )
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth().weight(1f),
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        SwitchContentTile(
                            title = "DLC content",
                            subtitle = if (game.supportsDlc) "${installed?.dlc?.size ?: 0} installed"
                            else "Not used by this preset",
                            action = "Add DLC NSP",
                            icon = Icons.Default.Extension,
                            enabled = game.supportsDlc && installed != null && !busy,
                            onAction = onAddDlc,
                            modifier = Modifier.weight(1f).fillMaxHeight(),
                        )
                        SwitchContentTile(
                            title = "Game configuration",
                            subtitle = "All Eden CPU, graphics, input, audio, and system controls",
                            action = "Configure Eden",
                            icon = Icons.Default.SportsEsports,
                            enabled = installed != null && !busy,
                            primary = true,
                            onAction = onOpenSettings,
                            modifier = Modifier.weight(1f).fillMaxHeight(),
                        )
                    }
                }
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(11.dp),
                verticalAlignment = Alignment.Top,
            ) {
                LauncherCard(modifier = Modifier.weight(1f).height(managementPanelHeight)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        SectionTitle("Mods", "Per-game Eden load directory")
                        Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                            IconButton(onClick = onRefresh, enabled = installed != null && !busy) {
                                Icon(Icons.Default.Refresh, contentDescription = "Refresh content")
                            }
                            PrimaryAction(
                                label = "Add mod folder",
                                icon = Icons.Default.Extension,
                                onClick = onAddMod,
                                enabled = installed != null && !busy,
                                modifier = Modifier.width(132.dp),
                            )
                        }
                    }
                    val mods = installed?.mods.orEmpty()
                    if (mods.isEmpty()) {
                        EmptyManagementState("No mods installed", Modifier.weight(1f))
                    } else {
                        LazyColumn(
                            modifier = Modifier.fillMaxWidth().weight(1f),
                            verticalArrangement = Arrangement.spacedBy(6.dp),
                        ) {
                            items(mods, key = { "${it.kind}:${it.name}:${it.version}" }) { patch ->
                                SwitchPatchRow(patch, onPatchEnabled, onRemovePatch)
                            }
                        }
                    }
                }
                LauncherCard(modifier = Modifier.weight(1f).height(managementPanelHeight)) {
                    SectionTitle("Installed content", "Updates and DLC verified by Eden")
                    val content = installed?.patches.orEmpty().filter { it.kind != SwitchContentKind.MOD }
                    if (content.isEmpty()) {
                        EmptyManagementState("No update or DLC installed", Modifier.weight(1f))
                    } else {
                        LazyColumn(
                            modifier = Modifier.fillMaxWidth().weight(1f),
                            verticalArrangement = Arrangement.spacedBy(6.dp),
                        ) {
                            items(content, key = { "${it.kind}:${it.name}:${it.version}" }) { patch ->
                                SwitchPatchRow(patch, onPatchEnabled, onRemovePatch, allowToggle = false)
                            }
                        }
                    }
                }
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(11.dp),
                verticalAlignment = Alignment.Top,
            ) {
                LauncherCard(modifier = Modifier.weight(1f).height(informationPanelHeight)) {
                    SectionTitle("Runtime paths", "One predictable location for every title")
                    InformationRow("Save data", "Eden user/save/${game.titleId}")
                    InformationRow("Mods", "Eden load/${game.titleId}")
                    InformationRow("Shader cache", "Isolated per game")
                    InformationRow("Dual screen", "Verified preset", emphasized = true)
                }
                LauncherCard(modifier = Modifier.weight(1f).height(informationPanelHeight)) {
                    SectionTitle("Save management", "Portable backups without leaving the launcher")
                    Text(
                        "Import or export this game's Eden save directory as one ZIP archive.",
                        color = LauncherTextMuted,
                        fontSize = 9.sp,
                    )
                    Spacer(Modifier.weight(1f))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        PrimaryAction(
                            label = "Import save ZIP",
                            icon = Icons.Default.FolderOpen,
                            onClick = onImportSave,
                            enabled = installed != null && !busy,
                            modifier = Modifier.weight(1f),
                        )
                        PrimaryAction(
                            label = "Export save ZIP",
                            icon = Icons.Default.Download,
                            onClick = onExportSave,
                            enabled = installed != null && !busy,
                            modifier = Modifier.weight(1f),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SwitchContentTile(
    title: String,
    subtitle: String,
    action: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    enabled: Boolean,
    onAction: () -> Unit,
    modifier: Modifier = Modifier,
    primary: Boolean = false,
) {
    LauncherCard(modifier = modifier.heightIn(min = 145.dp), compact = true) {
        Text(title, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        Text(
            subtitle,
            color = LauncherTextMuted,
            fontSize = 9.sp,
            maxLines = 2,
            minLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        Spacer(Modifier.weight(1f))
        PrimaryAction(
            label = action,
            icon = icon,
            onClick = onAction,
            enabled = enabled,
            primary = primary,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun MetricBlock(label: String, value: String, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .background(LauncherPanelRaised)
            .border(1.dp, LauncherOutline, RectangleShape)
            .padding(7.dp),
    ) {
        Text(label, color = LauncherTextMuted, fontFamily = FontFamily.Monospace, fontSize = 7.sp)
        Text(
            value,
            color = LauncherAccent,
            fontWeight = FontWeight.Bold,
            fontSize = 9.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun EmptyManagementState(message: String, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 62.dp)
            .background(LauncherPanelRaised)
            .border(1.dp, LauncherOutline, RectangleShape),
        contentAlignment = Alignment.Center,
    ) {
        Text(message, color = LauncherTextMuted, fontSize = 9.sp)
    }
}

@Composable
private fun SwitchPatchRow(
    patch: SwitchPatch,
    onEnabled: (SwitchPatch, Boolean) -> Unit,
    onRemove: (SwitchPatch) -> Unit,
    allowToggle: Boolean = true,
) {
    Row(
        modifier = Modifier.fillMaxWidth().border(1.dp, LauncherOutline, RectangleShape).padding(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(patch.name, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
            Text(
                "${patch.kind.name}  ${patch.version}",
                color = LauncherTextMuted,
                fontFamily = FontFamily.Monospace,
                fontSize = 8.sp,
            )
        }
        if (allowToggle) {
            Switch(checked = patch.enabled, onCheckedChange = { onEnabled(patch, it) })
        }
        if (patch.removable) {
            IconButton(onClick = { onRemove(patch) }) {
                Icon(Icons.Default.Delete, contentDescription = "Remove ${patch.name}")
            }
        }
    }
}

@Composable
private fun SwitchGameArtwork(
    game: SwitchGameDefinition,
    modifier: Modifier,
) {
    Image(
        painter = painterResource(switchGameArtwork(game.id)),
        contentDescription = "${game.title} cover",
        modifier = modifier,
        contentScale = ContentScale.Fit,
    )
}
