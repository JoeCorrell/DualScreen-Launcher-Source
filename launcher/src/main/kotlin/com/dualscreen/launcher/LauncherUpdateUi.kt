package com.dualscreen.launcher

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
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
            .background(LauncherAccent.copy(alpha = 0.12f))
            .padding(horizontal = 18.dp, vertical = 5.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(
                Icons.Default.SystemUpdate,
                contentDescription = null,
                tint = LauncherAccent,
            )
            Spacer(Modifier.width(8.dp))
            Text(
                "DUALSCREEN LAUNCHER ${available.latestVersion} IS AVAILABLE",
                color = LauncherAccent,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp,
            )
        }
        Button(
            onClick = { onOpenUpdate(available.downloadUrl) },
            modifier = Modifier.height(32.dp),
            shape = RoundedCornerShape(7.dp),
        ) {
            Text("DOWNLOAD UPDATE", color = Color(0xFF17120A), fontSize = 9.sp, fontWeight = FontWeight.Bold)
        }
    }
}
