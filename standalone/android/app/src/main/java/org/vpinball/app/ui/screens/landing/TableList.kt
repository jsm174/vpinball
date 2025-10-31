package org.vpinball.app.ui.screens.landing

import android.graphics.ImageDecoder
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.selection.TextSelectionColors
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.SheetState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.vpinball.app.Table
import org.vpinball.app.TableListMode
import org.vpinball.app.VPinballManager
import org.vpinball.app.jni.VPinballLogLevel
import org.vpinball.app.ui.screens.common.RoundedCard
import org.vpinball.app.ui.theme.VpxRed
import org.vpinball.app.util.resetImage
import org.vpinball.app.util.resetIni
import org.vpinball.app.util.resizeWithAspectFit
import org.vpinball.app.util.updateImage

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TablesList(
    tables: List<Table>,
    mode: TableListMode,
    onPlay: (table: Table) -> Unit,
    onRename: (table: Table, name: String) -> Unit,
    onViewScript: (table: Table) -> Unit,
    onShare: (table: Table) -> Unit,
    onDelete: (table: Table) -> Unit,
    modifier: Modifier = Modifier,
    lazyGridState: LazyGridState = rememberLazyGridState(),
    lazyListState: LazyListState = rememberLazyListState(),
    availableHeightOverride: Dp? = null,
) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()

    var currentTable by remember { mutableStateOf<Table?>(null) }

    var showRenameAlertDialog by remember { mutableStateOf(false) }
    var renameName by remember { mutableStateOf(TextFieldValue("")) }

    var showTableImageSheet by remember { mutableStateOf(false) }
    val tableImageSheetState: SheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    val focusRequester = remember { FocusRequester() }

    val photoPickerLauncher =
        rememberLauncherForActivityResult(
            contract = ActivityResultContracts.GetContent(),
            onResult = { uri ->
                if (uri != null) {
                    try {
                        val source = ImageDecoder.createSource(context.contentResolver, uri)
                        val bitmap = ImageDecoder.decodeBitmap(source) { decoder, _, _ -> decoder.isMutableRequired = true }
                        val resizedBitmap =
                            bitmap.resizeWithAspectFit(
                                newWidth = VPinballManager.getDisplaySize().width,
                                newHeight = VPinballManager.getDisplaySize().height,
                            )
                        val tableToUpdate = currentTable!!
                        coroutineScope.launch { withContext(Dispatchers.IO) { tableToUpdate.updateImage(resizedBitmap) } }
                    } catch (e: Exception) {
                        VPinballManager.log(VPinballLogLevel.ERROR, "Unable to change image: ${e.message}")
                    }
                }
            },
        )

    when (mode) {
        TableListMode.LIST -> {
            LazyColumn(verticalArrangement = Arrangement.spacedBy(0.dp), modifier = modifier, state = lazyListState) {
                items(tables.size, key = { tables[it].uuid }) { index ->
                    val table = tables[index]
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        if (index == 0) {
                            HorizontalDivider()
                        }

                        TableListRowItem(
                            table = table,
                            onPlay = onPlay,
                            onRename = {
                                currentTable = table
                                renameName = renameName.copy(text = table.name)
                                showRenameAlertDialog = true
                            },
                            onTableImage = {
                                currentTable = table
                                showTableImageSheet = true
                            },
                            onViewScript = { onViewScript(table) },
                            onShare = { onShare(table) },
                            onReset = { table.resetIni() },
                            onDelete = { onDelete(table) },
                        )

                        HorizontalDivider()
                    }
                }
            }
        }
        else -> {
            androidx.compose.foundation.layout.BoxWithConstraints(modifier = modifier) {
                val maxW = this.maxWidth
                val maxH = availableHeightOverride ?: this.maxHeight

                val layout = computeColumns(containerW = maxW - 32.dp, availableH = (maxH - 32.dp).coerceAtLeast(60.dp), mode = mode)

                LazyVerticalGrid(
                    columns = GridCells.Fixed(layout.cols),
                    verticalArrangement = Arrangement.spacedBy(layout.gap),
                    horizontalArrangement = Arrangement.spacedBy(layout.gap),
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 16.dp),
                    state = lazyGridState,
                ) {
                    items(tables.size, key = { tables[it].uuid }) {
                        val table = tables[it]
                        androidx.compose.foundation.layout.Box(
                            modifier = Modifier.fillMaxWidth(),
                            contentAlignment = androidx.compose.ui.Alignment.Center,
                        ) {
                            androidx.compose.foundation.layout.Box(modifier = Modifier.width(layout.cardW).height(layout.cardW * 1.5f)) {
                                TableListGridItem(
                                    table = table,
                                    onPlay = onPlay,
                                    onRename = {
                                        currentTable = table
                                        renameName = renameName.copy(text = table.name)
                                        showRenameAlertDialog = true
                                    },
                                    onTableImage = {
                                        currentTable = table
                                        showTableImageSheet = true
                                    },
                                    onViewScript = { onViewScript(table) },
                                    onShare = { onShare(table) },
                                    onReset = { table.resetIni() },
                                    onDelete = { onDelete(table) },
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    if (showRenameAlertDialog) {
        AlertDialog(
            title = { Text(text = "Rename Table", style = MaterialTheme.typography.titleMedium) },
            text = {
                OutlinedTextField(
                    value = renameName,
                    onValueChange = { renameName = it },
                    colors =
                        OutlinedTextFieldDefaults.colors(
                            cursorColor = Color.VpxRed,
                            selectionColors = TextSelectionColors(handleColor = Color.Transparent, backgroundColor = Color.VpxRed.copy(alpha = 0.5f)),
                            focusedBorderColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        ),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth().focusRequester(focusRequester),
                )
                LaunchedEffect(Unit) {
                    renameName = renameName.copy(selection = TextRange(renameName.text.length))
                    focusRequester.requestFocus()
                }
            },
            onDismissRequest = {},
            confirmButton = {
                TextButton(
                    onClick = {
                        onRename(currentTable!!, renameName.text)
                        showRenameAlertDialog = false
                    },
                    enabled = renameName.text.isNotBlank(),
                ) {
                    Text(
                        text = "OK",
                        color = if (renameName.text.isNotBlank()) Color.VpxRed else Color.Gray,
                        fontSize = MaterialTheme.typography.titleMedium.fontSize,
                        fontWeight = FontWeight.SemiBold,
                    )
                }
            },
            dismissButton = {
                TextButton(onClick = { showRenameAlertDialog = false }) {
                    Text(
                        text = "Cancel",
                        color = Color.VpxRed,
                        fontSize = MaterialTheme.typography.titleMedium.fontSize,
                        fontWeight = FontWeight.SemiBold,
                    )
                }
            },
        )
    }

    if (showTableImageSheet) {
        ModalBottomSheet(
            onDismissRequest = { showTableImageSheet = false },
            sheetState = tableImageSheetState,
            containerColor = MaterialTheme.colorScheme.surface,
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                RoundedCard {
                    TextButton(
                        onClick = {
                            showTableImageSheet = false
                            photoPickerLauncher.launch("image/*")
                        },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(text = "Photo Library", color = Color.VpxRed, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Normal)
                    }

                    HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

                    TextButton(
                        onClick = {
                            showTableImageSheet = false
                            val tableToReset = currentTable!!
                            coroutineScope.launch { withContext(Dispatchers.IO) { tableToReset.resetImage() } }
                        },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(text = "Reset", color = Color.VpxRed, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Normal)
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                RoundedCard {
                    TextButton(onClick = { showTableImageSheet = false }, modifier = Modifier.fillMaxWidth()) {
                        Text(text = "Cancel", color = Color.VpxRed, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                    }
                }
            }
        }
    }
}

private data class GridLayout(val cols: Int, val cardW: androidx.compose.ui.unit.Dp, val gap: androidx.compose.ui.unit.Dp)

private fun heightFactor(mode: TableListMode): Float =
    when (mode) {
        TableListMode.SMALL -> 0.72f
        TableListMode.MEDIUM -> 0.88f
        TableListMode.LARGE -> 1.0f
        TableListMode.LIST -> 1.0f
    }

private fun computeColumns(containerW: androidx.compose.ui.unit.Dp, availableH: androidx.compose.ui.unit.Dp, mode: TableListMode): GridLayout {
    val baseGap = 12.dp
    val ratio = 2f / 3f
    val minReadableW = 120f
    val minFloorRegular = 48f
    val minFloorCompact = 40f

    val gap = if (availableH < 420.dp) 8.dp else baseGap
    val minFloor = if (availableH < 420.dp) minFloorCompact else minFloorRegular

    fun kFor(capFactor: Float): Int {
        var minW = minReadableW
        val effCap = availableH.value.coerceAtLeast(60f) * ratio * capFactor
        var effMin = kotlin.math.min(minW, effCap)
        var K = kotlin.math.floor(((containerW.value + gap.value) / (effMin + gap.value))).toInt()
        while (K < 3 && minW > minFloor) {
            minW -= 6
            effMin = kotlin.math.min(minW, effCap)
            K = kotlin.math.floor(((containerW.value + gap.value) / (effMin + gap.value))).toInt()
        }
        return K.coerceAtLeast(1)
    }

    val kSmallRaw = kFor(0.72f)
    val kMedRaw = kFor(0.88f)
    val kLargeRaw = kFor(1.0f)

    var small = kotlin.math.max(3, kSmallRaw)
    var medium = kotlin.math.min(kMedRaw, small - 1)
    if (medium < 2) medium = kotlin.math.max(2, small - 1)
    var large = kotlin.math.min(kLargeRaw, medium - 1)
    if (large < 1) large = 1

    // Enforce ordering across modes with cap: Small <= 7, Medium < Small, Large < Medium
    val effSmall = kotlin.math.min(small, 6)
    var effMedium = kotlin.math.min(medium, effSmall - 1)
    if (effMedium < 2) effMedium = kotlin.math.max(1, effSmall - 1)
    var effLarge = kotlin.math.min(large, effMedium - 1)
    if (effLarge < 1) effLarge = 1
    val cols =
        when (mode) {
            TableListMode.SMALL -> effSmall
            TableListMode.MEDIUM -> effMedium
            TableListMode.LARGE -> effLarge
            TableListMode.LIST -> effMedium
        }
    // perCol width (minus gaps), cap by height
    val perCol = (containerW - gap * (cols - 1)) / cols
    val cap = availableH.coerceAtLeast(60.dp) * ratio * heightFactor(mode)
    val cardW = if (perCol < cap) perCol else cap

    return GridLayout(cols = cols, cardW = cardW, gap = gap)
}
