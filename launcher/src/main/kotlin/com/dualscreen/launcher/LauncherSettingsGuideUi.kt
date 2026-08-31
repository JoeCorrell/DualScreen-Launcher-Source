package com.dualscreen.launcher

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Gamepad
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.RocketLaunch
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.SettingsApplications
import androidx.compose.material.icons.filled.Smartphone
import androidx.compose.material.icons.filled.Troubleshoot
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
internal fun SettingsPage(
    settings: LauncherSettings,
    installed: Map<String, InstalledGame>,
    gpuDrivers: List<LauncherGpuDriver>,
    gpuDriverImportState: GpuDriverImportState,
    gpuDriverMessage: String?,
    onKeepAwake: (Boolean) -> Unit,
    onResolution: (GameResolution) -> Unit,
    onGpuDriver: (String) -> Unit,
    onImportGpuDriver: () -> Unit,
) {
    val selectedDriver = gpuDrivers.firstOrNull { it.id == settings.gpuDriverId }
    Column(
        modifier = Modifier.fillMaxSize().padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(1.dp)) {
                Text(
                    "LAUNCHER SETTINGS",
                    color = LauncherAccent,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 8.sp,
                    letterSpacing = 1.6.sp,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Settings", fontSize = 24.sp, lineHeight = 26.sp, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.width(11.dp))
                    Text(
                        "Display, graphics, storage, and the verified game runtime.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 10.sp,
                    )
                }
            }
            Text(
                "${installed.size} READY  •  VERIFIED PRESETS",
                color = if (installed.isEmpty()) MaterialTheme.colorScheme.onSurfaceVariant else LauncherSuccess,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 8.sp,
            )
        }
        Spacer(Modifier.height(7.dp))
        LazyVerticalGrid(
            columns = GridCells.Fixed(2),
            modifier = Modifier.fillMaxWidth().weight(1f),
            horizontalArrangement = Arrangement.spacedBy(9.dp),
            verticalArrangement = Arrangement.spacedBy(9.dp),
        ) {
            item {
                LauncherCard(compact = true) {
                    SettingsSectionHeading(
                        icon = Icons.Default.Smartphone,
                        title = "Display & session",
                        detail = "Applied automatically before the game starts",
                    )
                    CompactSwitch(
                        title = "Keep displays awake",
                        detail = "Prevent either screen from sleeping during play.",
                        checked = settings.keepScreenAwake,
                        onCheckedChange = onKeepAwake,
                    )
                    Text("TOP-SCREEN GAME RESOLUTION", style = settingLabelStyle())
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                    ) {
                        GameResolution.entries.forEach { resolution ->
                            CompactChoice(
                                label = resolution.label,
                                selected = settings.resolution == resolution,
                                onClick = { onResolution(resolution) },
                                modifier = Modifier.weight(1f),
                            )
                        }
                    }
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(18.dp)) {
                        InformationRow(
                            "Top output",
                            settings.resolution.label,
                            emphasized = true,
                            modifier = Modifier.weight(1f),
                        )
                        InformationRow("Lower UI", "1240 × 1080", modifier = Modifier.weight(1f))
                    }
                }
            }

            item {
                LauncherCard(compact = true) {
                    SettingsSectionHeading(
                        icon = Icons.Default.SettingsApplications,
                        title = "GPU driver",
                        detail = "Bundled and uploaded AdrenoTools packages",
                    )
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(LauncherAccent.copy(alpha = 0.08f), RoundedCornerShape(8.dp))
                            .border(1.dp, LauncherAccent.copy(alpha = 0.26f), RoundedCornerShape(8.dp))
                            .padding(horizontal = 10.dp, vertical = 7.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("ACTIVE DRIVER", style = settingLabelStyle())
                        Text(
                            selectedDriver?.label ?: "Loading…",
                            color = LauncherAccent,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    if (gpuDrivers.isEmpty()) {
                        Text("Reading installed drivers…", color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 11.sp)
                    } else {
                        LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            items(gpuDrivers, key = { it.id }) { driver ->
                                val suffix = when (driver.source) {
                                    LauncherGpuDriver.Source.SYSTEM -> ""
                                    LauncherGpuDriver.Source.BUNDLED -> " • bundled"
                                    LauncherGpuDriver.Source.UPLOADED -> " • uploaded"
                                }
                                CompactChoice(
                                    label = driver.label + suffix,
                                    selected = settings.gpuDriverId == driver.id,
                                    onClick = { onGpuDriver(driver.id) },
                                )
                            }
                        }
                    }
                    OutlinedButton(
                        onClick = onImportGpuDriver,
                        enabled = gpuDriverImportState !is GpuDriverImportState.Importing,
                        modifier = Modifier.fillMaxWidth().height(39.dp),
                        shape = RoundedCornerShape(9.dp),
                    ) {
                        Icon(Icons.Default.FileDownload, contentDescription = null, modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(7.dp))
                        Text(
                            if (gpuDriverImportState is GpuDriverImportState.Importing) {
                                "Validating driver…"
                            } else {
                                "Upload GPU driver ZIP"
                            },
                            fontSize = 11.sp,
                        )
                    }
                    DriverMessage(gpuDriverImportState, gpuDriverMessage)
                }
            }

            item {
                LauncherCard(compact = true) {
                    SettingsSectionHeading(
                        icon = Icons.Default.Archive,
                        title = "Library & payloads",
                        detail = "Private, managed launcher storage",
                    )
                    InformationRow("Ready to play", "${installed.size} of ${GameCatalog.games.size}", emphasized = installed.isNotEmpty())
                    InformationRow("Managed game data", formatBytes(installed.values.sumOf { it.installedBytes }))
                    InformationRow("Runtime delivery", "Verified on first boot")
                    InformationRow("Game mods", "SHA-256 verified")
                    InformationRow("Save imports", "Documents / My Games")
                }
            }

            item {
                LauncherCard(compact = true) {
                    SettingsSectionHeading(
                        icon = Icons.Default.CheckCircle,
                        title = "Verified environment",
                        detail = "Game-tested settings stay locked per preset",
                    )
                    InformationRow("Proton", "11.0-5 ARM64EC 6")
                    InformationRow("FEX", "2608 stable 0")
                    InformationRow("DirectX", "DXVK 3.0-1 • VKD3D 2.9")
                    InformationRow("Graphics", "Turnip 26.2.0")
                    InformationRow("Presentation", "SurfaceFlinger • dual display")
                }
            }
        }
    }
}

@Composable
private fun SettingsSectionHeading(icon: ImageVector, title: String, detail: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(9.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(31.dp)
                .background(LauncherAccent.copy(alpha = 0.12f), RoundedCornerShape(8.dp))
                .border(1.dp, LauncherAccent.copy(alpha = 0.35f), RoundedCornerShape(8.dp)),
            contentAlignment = Alignment.Center,
        ) {
            Icon(icon, contentDescription = null, tint = LauncherAccent, modifier = Modifier.size(17.dp))
        }
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(1.dp)) {
            Text(title, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            Text(detail, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 9.sp)
        }
    }
}

@Composable
private fun CompactSwitch(
    title: String,
    detail: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f).padding(end = 12.dp)) {
            Text(title, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Text(detail, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 10.sp)
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange, modifier = Modifier.size(width = 47.dp, height = 28.dp))
    }
}

@Composable
private fun CompactChoice(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    if (selected) {
        Button(
            onClick = onClick,
            modifier = modifier.height(38.dp),
            shape = RoundedCornerShape(9.dp),
            colors = ButtonDefaults.buttonColors(containerColor = LauncherAccent, contentColor = Color(0xFF17120A)),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp),
        ) {
            Text(label, fontSize = 9.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    } else {
        OutlinedButton(
            onClick = onClick,
            modifier = modifier.height(38.dp),
            shape = RoundedCornerShape(9.dp),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp),
        ) {
            Text(label, fontSize = 9.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

@Composable
private fun DriverMessage(state: GpuDriverImportState, message: String?) {
    val text: String?
    val color: Color
    when (state) {
        is GpuDriverImportState.Failed -> {
            text = state.message
            color = MaterialTheme.colorScheme.error
        }
        is GpuDriverImportState.Installed -> {
            text = "Imported ${state.driver.label}."
            color = LauncherSuccess
        }
        GpuDriverImportState.Importing -> {
            text = "Inspecting driver package…"
            color = LauncherAccent
        }
        GpuDriverImportState.Idle -> {
            text = message
            color = MaterialTheme.colorScheme.onSurfaceVariant
        }
    }
    if (!text.isNullOrBlank()) Text(text, color = color, fontSize = 10.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
}

@Composable
private fun settingLabelStyle() = MaterialTheme.typography.labelSmall.copy(
    color = MaterialTheme.colorScheme.onSurfaceVariant,
    fontSize = 8.sp,
    fontFamily = FontFamily.Monospace,
    fontWeight = FontWeight.Bold,
    letterSpacing = 1.sp,
)

@Composable
internal fun GuidePage(onBrowseGames: () -> Unit) {
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(
            start = 22.dp,
            end = 22.dp,
            top = 13.dp,
            bottom = 22.dp,
        ),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.Bottom,
            ) {
                PageHeading(
                    eyebrow = "Quick start",
                    title = "Guide",
                    subtitle = "From your game archive to a working dual-screen session.",
                )
                Button(
                    onClick = onBrowseGames,
                    modifier = Modifier.height(40.dp),
                    shape = RoundedCornerShape(10.dp),
                ) {
                    Icon(Icons.Default.Gamepad, contentDescription = null, modifier = Modifier.size(17.dp))
                    Spacer(Modifier.width(7.dp))
                    Text("Browse Games", fontSize = 11.sp, fontWeight = FontWeight.Bold)
                }
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(11.dp),
                verticalAlignment = Alignment.Top,
            ) {
                GuideStep(
                    number = "01",
                    icon = Icons.Default.Archive,
                    title = "Prepare your ZIP",
                    body = "ZIP the game folder you legally own. The game executable and data folder may be at the ZIP root or inside one wrapper folder.",
                    modifier = Modifier.weight(1f),
                )
                GuideStep(
                    number = "02",
                    icon = Icons.Default.FolderOpen,
                    title = "Add the game",
                    body = "Choose a grey game card, open Game Details, then select Install Game. Pick the matching ZIP from device storage.",
                    modifier = Modifier.weight(1f),
                )
                GuideStep(
                    number = "03",
                    icon = Icons.Default.RocketLaunch,
                    title = "Let setup finish",
                    body = "The launcher validates, extracts, configures the tested container, installs the dual-screen mod, and verifies the result automatically.",
                    modifier = Modifier.weight(1f),
                )
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(11.dp),
                verticalAlignment = Alignment.Top,
            ) {
                GuideStep(
                    number = "04",
                    icon = Icons.Default.PlayArrow,
                    title = "Launch",
                    body = "Use Play Game from Game Details. The top display runs gameplay while the lower display hosts the mod UI and launcher console.",
                    modifier = Modifier.weight(1f),
                )
                GuideStep(
                    number = "05",
                    icon = Icons.Default.SettingsApplications,
                    title = "Game launcher",
                    body = "Use Open Game Launcher only when a title needs its own settings tool. Return to Game Details afterward and use Play Game.",
                    modifier = Modifier.weight(1f),
                )
                GuideStep(
                    number = "06",
                    icon = Icons.Default.Save,
                    title = "Import saves",
                    body = "Create a ZIP beginning with the game's folder inside Documents/My Games, then choose Import Save ZIP. Existing data is backed up when needed.",
                    modifier = Modifier.weight(1f),
                )
            }
        }

        item {
            LauncherCard {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Icon(Icons.Default.Troubleshoot, contentDescription = null, tint = LauncherAccent, modifier = Modifier.size(22.dp))
                    SectionTitle("Troubleshooting", "The lower-screen console always shows the current setup or launch stage")
                }
                InformationRow("Archive rejected", "Confirm the title and required executable")
                InformationRow("Game returns immediately", "Open the console and read the final launch error")
                InformationRow("Wrong resolution", "Choose a tested resolution in Settings")
                InformationRow("Graphics problem", "Select another installed GPU driver")
                InformationRow("No lower-screen UI", "Wait for world/save initialization, then check mod status")
            }
        }
    }
}

@Composable
private fun GuideStep(
    number: String,
    icon: ImageVector,
    title: String,
    body: String,
    modifier: Modifier = Modifier,
) {
    LauncherCard(modifier) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .size(31.dp)
                    .background(LauncherAccent.copy(alpha = 0.13f), RoundedCornerShape(8.dp))
                    .border(1.dp, LauncherAccent.copy(alpha = 0.4f), RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(icon, contentDescription = null, tint = LauncherAccent, modifier = Modifier.size(17.dp))
            }
            Text(number, color = Color(0xFF5E5C53), fontFamily = FontFamily.Monospace, fontSize = 18.sp, fontWeight = FontWeight.Bold)
        }
        Text(title, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        Text(body, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 10.sp, lineHeight = 13.sp)
    }
}
