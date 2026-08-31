package com.dualscreen.launcher

import androidx.compose.foundation.background
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
import androidx.compose.foundation.shape.RoundedCornerShape
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
    onOpenUpdate: (String) -> Unit,
) {
    val available = state as? LauncherUpdateState.Available ?: return
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(36.dp)
            .background(LauncherAccent.copy(alpha = 0.12f))
            .padding(horizontal = 16.dp),
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
                tint = LauncherAccent,
                modifier = Modifier.size(17.dp),
            )
            Spacer(Modifier.width(7.dp))
            Text(
                "UPDATE ${available.latestVersion} AVAILABLE",
                color = LauncherAccent,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 9.sp,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Box(
            modifier = Modifier
                .height(28.dp)
                .clip(RoundedCornerShape(6.dp))
                .background(LauncherAccent)
                .clickable { onOpenUpdate(available.downloadUrl) }
                .padding(horizontal = 11.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                "DOWNLOAD",
                color = Color(0xFF17120A),
                fontSize = 8.sp,
                fontWeight = FontWeight.Bold,
                maxLines = 1,
            )
        }
    }
}
