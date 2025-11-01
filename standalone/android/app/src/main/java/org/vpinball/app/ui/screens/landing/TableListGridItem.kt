package org.vpinball.app.ui.screens.landing

import androidx.compose.animation.Crossfade
import androidx.compose.foundation.Image
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import dev.chrisbanes.haze.HazeDefaults
import dev.chrisbanes.haze.HazeState
import dev.chrisbanes.haze.HazeTint
import dev.chrisbanes.haze.haze
import dev.chrisbanes.haze.hazeChild
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.vpinball.app.R
import org.vpinball.app.Table
import org.vpinball.app.TableImageState
import org.vpinball.app.util.drawWithGradient
import org.vpinball.app.util.loadImage

@Composable
fun TableListGridItem(
    table: Table,
    onPlay: (table: Table) -> Unit,
    onRename: (table: Table) -> Unit,
    onTableImage: (table: Table) -> Unit,
    onViewScript: (table: Table) -> Unit,
    onShare: (table: Table) -> Unit,
    onReset: (table: Table) -> Unit,
    onDelete: (table: Table) -> Unit,
) {
    val focusManager = LocalFocusManager.current

    val contextMenuExpanded = remember { mutableStateOf(false) }
    var globalTouchOffset by remember { mutableStateOf(Offset.Zero) }
    val bitmap by
        produceState<ImageBitmap?>(null, table.uuid, table.image, table.modifiedAt) { value = withContext(Dispatchers.IO) { table.loadImage() } }

    val imageState by remember {
        derivedStateOf {
            if (bitmap != null) {
                TableImageState.IMAGE_LOADED
            } else {
                TableImageState.NO_IMAGE
            }
        }
    }

    val hazeState = remember { HazeState() }

    // Match sample: card height = width * 1.5 -> aspectRatio = 2/3
    val ratio = 2f / 3f

    Box {
        Box(
            modifier =
                Modifier.fillMaxSize()
                    .aspectRatio(ratio)
                    .clip(RoundedCornerShape(8.dp))
                    .padding(all = 4.dp)
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onTap = { onPlay(table) },
                            onLongPress = { offset ->
                                focusManager.clearFocus()
                                globalTouchOffset = offset
                                contextMenuExpanded.value = true
                            },
                        )
                    }
                    .onGloballyPositioned { layoutCoordinates: LayoutCoordinates ->
                        globalTouchOffset = layoutCoordinates.localToRoot(globalTouchOffset)
                    }
        ) {
            Column(modifier = Modifier.haze(state = hazeState)) {
                Crossfade(imageState, label = "table_grid_item_cross_fade") { imageState ->
                    when (imageState) {
                        TableImageState.IMAGE_LOADED -> {
                            bitmap?.let { bitmap ->
                                Box(modifier = Modifier.fillMaxSize()) {
                                    Image(
                                        bitmap = bitmap,
                                        contentDescription = null,
                                        modifier = Modifier.fillMaxSize(),
                                        contentScale = ContentScale.Fit,
                                        alignment = Alignment.Center,
                                    )
                                }
                            }
                        }

                        TableImageState.LOADING_IMAGE -> {
                            Box {}
                        }

                        else -> {
                            Box(modifier = Modifier.fillMaxSize()) {
                                Image(
                                    painter = painterResource(R.drawable.img_table_placeholder),
                                    contentDescription = null,
                                    modifier = Modifier.fillMaxSize().drawWithGradient(),
                                    contentScale = ContentScale.Fit,
                                    alignment = Alignment.Center,
                                )
                            }
                        }
                    }
                }
            }

            Box(
                modifier =
                    Modifier.align(Alignment.BottomCenter).fillMaxWidth().clip(RoundedCornerShape(6.dp)).hazeChild(state = hazeState) {
                        backgroundColor = Color.Black
                        tints = listOf(HazeTint(Color.White.copy(alpha = 0.1f)))
                        blurRadius = 40.dp
                        noiseFactor = HazeDefaults.noiseFactor
                    }
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 7.dp, vertical = 5.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Text(text = table.name, color = Color.White, textAlign = TextAlign.Center, style = MaterialTheme.typography.titleSmall)
                }
            }
        }

        TableListItemDropdownMenu(
            table = table,
            expanded = contextMenuExpanded,
            onRename = { onRename(table) },
            onTableImage = { onTableImage(table) },
            onViewScript = { onViewScript(table) },
            onShare = { onShare(table) },
            onReset = { onReset(table) },
            onDelete = { onDelete(table) },
            offsetProvider = { globalTouchOffset },
        )
    }
}
