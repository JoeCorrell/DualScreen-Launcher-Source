package com.dualscreen.launcher

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
internal fun LauncherUpdateBanner(
    state: LauncherUpdateState,
    onInstall: () -> Unit,
    onRetry: () -> Unit,
    onRequestPermission: () -> Unit,
) {
    val visible = when (state) {
        is LauncherUpdateState.Available,
        is LauncherUpdateState.Downloading,
        is LauncherUpdateState.Preparing,
        is LauncherUpdateState.AwaitingConfirmation,
        is LauncherUpdateState.PermissionRequired,
        is LauncherUpdateState.Failed,
        -> true
        else -> false
    }
    val tint = if (state is LauncherUpdateState.Failed) LauncherDanger else LauncherAccent
    val label = when (state) {
        is LauncherUpdateState.Available -> "UPDATE ${state.latestVersion} AVAILABLE"
        is LauncherUpdateState.Downloading -> "DOWNLOADING UPDATE ${installPercent(state.fraction)}%"
        is LauncherUpdateState.Preparing -> "VERIFYING UPDATE ${state.latestVersion}"
        is LauncherUpdateState.AwaitingConfirmation -> "CONFIRM UPDATE ${state.latestVersion} IN ANDROID"
        is LauncherUpdateState.PermissionRequired -> "ALLOW IN-APP UPDATES TO INSTALL ${state.update.latestVersion}"
        is LauncherUpdateState.Failed -> "UPDATE FAILED: ${state.reason}"
        else -> ""
    }
    AnimatedVisibility(
        visible = visible,
        enter = expandVertically() + fadeIn(),
        exit = shrinkVertically() + fadeOut(),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(38.dp)
                .background(tint.copy(alpha = 0.11f))
                .border(1.dp, tint.copy(alpha = 0.28f), RectangleShape)
                .padding(horizontal = 18.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(
                    Icons.Default.SystemUpdate,
                    contentDescription = null,
                    tint = tint,
                    modifier = Modifier.size(17.dp),
                )
                Spacer(Modifier.width(7.dp))
                Text(
                    label,
                    color = tint,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 9.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            when (state) {
                is LauncherUpdateState.Available,
                is LauncherUpdateState.PermissionRequired,
                is LauncherUpdateState.Failed,
                -> Box(
                    modifier = Modifier
                        .height(28.dp)
                        .clip(RectangleShape)
                        .background(tint)
                        .clickable {
                            when (state) {
                                is LauncherUpdateState.Available -> onInstall()
                                is LauncherUpdateState.PermissionRequired -> onRequestPermission()
                                else -> onRetry()
                            }
                        }
                        .padding(horizontal = 13.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        when (state) {
                            is LauncherUpdateState.Available -> "UPDATE NOW"
                            is LauncherUpdateState.PermissionRequired -> "ALLOW"
                            else -> "RETRY"
                        },
                        color = Color(0xFF17120A),
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                    )
                }
                is LauncherUpdateState.Downloading -> LauncherProgress(
                    fraction = state.fraction,
                    modifier = Modifier.width(150.dp).height(5.dp),
                )
                else -> Unit
            }
        }
    }
}
