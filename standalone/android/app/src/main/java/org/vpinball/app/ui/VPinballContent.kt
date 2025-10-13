package org.vpinball.app.ui

import android.content.Intent
import android.view.WindowInsets
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.core.content.FileProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import java.io.File
import kotlinx.coroutines.launch
import org.vpinball.app.CodeLanguage
import org.vpinball.app.Link
import org.vpinball.app.VPinballManager
import org.vpinball.app.VPinballViewModel
import org.vpinball.app.ui.screens.common.CodeWebViewDialog
import org.vpinball.app.ui.screens.landing.LandingScreen
import org.vpinball.app.ui.screens.loading.LoadingScreen
import org.vpinball.app.ui.screens.splash.SplashScreen
import org.vpinball.app.ui.theme.VPinballTheme
import org.vpinball.app.ui.theme.VpxRed
import org.vpinball.app.ui.util.koinActivityViewModel
import org.vpinball.app.util.getActivity

@Composable
fun VPinballContent(isAppInit: Boolean = false, viewModel: VPinballViewModel = koinActivityViewModel()) {
    val context = LocalContext.current
    val activity = context.getActivity()
    val state by viewModel.state.collectAsStateWithLifecycle()
    var codeFile by remember { mutableStateOf<File?>(null) }
    val scope = rememberCoroutineScope()
    var showSplash by remember { mutableStateOf(false) }

    LaunchedEffect(state.playing, isAppInit) {
        activity?.window?.decorView?.windowInsetsController?.let { controller ->
            if (state.playing) {
                controller.hide(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
            } else {
                controller.show(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                controller.setSystemBarsAppearance(0, android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS)
            }
        }
    }

    LaunchedEffect(isAppInit) {
        if (isAppInit && state.splash) {
            kotlinx.coroutines.delay(150)
            showSplash = true
            VPinballManager.startup()
            org.vpinball.app.TableManager.initialize(context.applicationContext)
            viewModel.startSplashTimer()
        }
    }

    VPinballTheme {
        if (!isAppInit || !showSplash) {
            // Wait for SDL initialization and status bar to settle
        } else if (state.splash) {
            SplashScreen()
        } else {
            AnimatedVisibility(visible = !(state.playing || state.loading), modifier = Modifier.fillMaxSize()) {
                LandingScreen(
                    webServerURL = viewModel.webServerURL,
                    progress = viewModel.progress,
                    status = viewModel.status,
                    onTableImported = { uuid, path ->
                        // With the new unified import system, the C++ library handles everything internally
                        // Table list refresh happens automatically via TABLE_LIST_UPDATED event
                    },
                    onRenameTable = { table, name ->
                        VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.INFO, "Renaming table ${table.uuid} to: $name")
                        scope.launch {
                            org.vpinball.app.TableManager.getInstance().renameTable(table.uuid, name)
                            org.vpinball.app.ui.screens.landing.LandingScreenViewModel.triggerRefresh()
                        }
                    },
                    onChangeTableImage = { table ->
                        // Table Image changes are handled by the UI directly
                    },
                    onDeleteTable = { table ->
                        VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.INFO, "Deleting table: ${table.uuid}")
                        scope.launch {
                            org.vpinball.app.TableManager.getInstance().deleteTable(table.uuid)
                            org.vpinball.app.ui.screens.landing.LandingScreenViewModel.triggerRefresh()
                        }
                    },
                    onViewFile = { file -> codeFile = file },
                )
            }

            state.table?.let { table ->
                if (state.loading) {
                    LoadingScreen(table, state.progress, state.status)
                }
            }

            if (state.error != null) {
                AlertDialog(
                    title = { Text(text = "TILT!", style = MaterialTheme.typography.titleMedium) },
                    text = { Text(state.error!!) },
                    onDismissRequest = {},
                    confirmButton = {
                        TextButton(onClick = { viewModel.clearError() }) {
                            Text(
                                text = "OK",
                                color = Color.VpxRed,
                                fontSize = MaterialTheme.typography.titleMedium.fontSize,
                                fontWeight = FontWeight.SemiBold,
                            )
                        }
                    },
                    dismissButton = {
                        TextButton(
                            onClick = {
                                viewModel.clearError()
                                Link.TROUBLESHOOTING.open(context = context)
                            }
                        ) {
                            Text(
                                text = "Learn More",
                                color = Color.VpxRed,
                                fontSize = MaterialTheme.typography.titleMedium.fontSize,
                                fontWeight = FontWeight.SemiBold,
                            )
                        }
                    },
                )
            }

            codeFile?.let {
                CodeWebViewDialog(
                    file = it,
                    canClear = CodeLanguage.fromFile(it) == CodeLanguage.LOG,
                    onDismissRequest = { codeFile = null },
                    onShare = {
                        val fileUri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", it)
                        val shareIntent =
                            Intent(Intent.ACTION_SEND).apply {
                                type = "application/octet-stream"
                                putExtra(Intent.EXTRA_STREAM, fileUri)
                                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                            }
                        context.startActivity(Intent.createChooser(shareIntent, "Share File: $it"))
                    },
                )
            }
        }
    }
}
