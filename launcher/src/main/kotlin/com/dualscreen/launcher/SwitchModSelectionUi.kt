package com.dualscreen.launcher

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.dualscreen.launcher.switch.SwitchModFolder

@Composable
internal fun SwitchModSelectionDialog(
    selectionKey: String,
    loader: suspend () -> Result<List<SwitchModFolder>>,
    onDismiss: () -> Unit,
    onInstall: (List<SwitchModFolder>) -> Unit,
) {
    var mods by remember(selectionKey) { mutableStateOf<List<SwitchModFolder>?>(null) }
    var error by remember(selectionKey) { mutableStateOf<String?>(null) }
    var selected by remember(selectionKey) { mutableStateOf(emptySet<String>()) }
    LaunchedEffect(selectionKey) {
        loader().onSuccess {
            mods = it
            if (it.size == 1) selected = setOf(it.single().id)
        }.onFailure { error = it.message ?: "Could not read mod folders." }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Choose mods to install") },
        text = {
            Column {
                when {
                    error != null -> Text(error.orEmpty())
                    mods == null -> Text("Finding mods in this folder…")
                    else -> {
                        Text("Only checked mods will be installed. Existing mods will not be overwritten.")
                        LazyColumn(Modifier.fillMaxWidth().heightIn(max = 320.dp)) {
                            items(mods.orEmpty(), key = { it.id }) { mod ->
                                Row(
                                    Modifier.fillMaxWidth().toggleable(
                                        value = mod.id in selected,
                                        role = Role.Checkbox,
                                        onValueChange = { checked ->
                                            selected = if (checked) selected + mod.id else selected - mod.id
                                        },
                                    ).padding(vertical = 8.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                ) {
                                    Checkbox(checked = mod.id in selected, onCheckedChange = null)
                                    Column(Modifier.weight(1f).padding(start = 8.dp)) {
                                        Text(mod.name)
                                        Text(mod.location, fontSize = 10.sp)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = mods != null && error == null && selected.isNotEmpty(),
                onClick = { onInstall(mods.orEmpty().filter { it.id in selected }) },
            ) { Text("Install selected (${selected.size})") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
