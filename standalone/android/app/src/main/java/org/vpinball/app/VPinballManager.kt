package org.vpinball.app

import android.content.Context
import android.graphics.BitmapFactory
import androidx.compose.ui.graphics.asImageBitmap
import android.net.Uri
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import android.util.Size
import androidx.compose.ui.graphics.asImageBitmap
import androidx.lifecycle.viewmodel.compose.viewModel
import java.io.File
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.datetime.toLocalDateTime
import org.koin.core.component.KoinComponent
import org.koin.core.component.inject
import org.vpinball.app.jni.*
import org.vpinball.app.jni.VPinballSettingsSection.STANDALONE
import kotlinx.serialization.json.Json
import kotlinx.serialization.encodeToString
import org.vpinball.app.util.FileUtils
import org.vpinball.app.util.loadImage

object VPinballManager : KoinComponent {
    enum class ScreenshotMode(val value: Int) {
        INSTRUCTIONS(0),
        ARTWORK(1),
        QUIT(2),
    }

    private const val TAG = "VPinballManager"

    val vpinballJNI: VPinballJNI = VPinballJNI()

    private lateinit var activity: VPinballActivity
    private lateinit var filesDir: File
    private lateinit var cacheDir: File
    private lateinit var displaySize: Size
    private lateinit var vibrator: Vibrator

    private var activeTable: VPinballVPXTable? = null
    private var haptics = false
    private var error: String? = null
    private var screenshotMode: ScreenshotMode? = null

    fun initialize(activity: VPinballActivity) {
        this.activity = activity

        filesDir = activity.filesDir
        cacheDir = activity.cacheDir
        
        // Tables path is now set automatically by VPinballTableManager

        val displayMetrics = activity.resources.displayMetrics
        val width = displayMetrics.widthPixels
        val height = displayMetrics.heightPixels
        displaySize = if (width > height) Size(height, width) else Size(width, height)

        vibrator =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = activity.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                vibratorManager.defaultVibrator
            } else {
                activity.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
            }

        vpinballJNI.VPinballInit { value, jsonData, rawData ->
            val viewModel = activity.viewModel
            val event = VPinballEvent.entries.find { it.ordinal == value }
            when (event) {
                VPinballEvent.ARCHIVE_UNCOMPRESSING,
                VPinballEvent.ARCHIVE_COMPRESSING,
                VPinballEvent.LOADING_ITEMS,
                VPinballEvent.LOADING_SOUNDS,
                VPinballEvent.LOADING_IMAGES,
                VPinballEvent.LOADING_FONTS,
                VPinballEvent.LOADING_COLLECTIONS,
                VPinballEvent.PRERENDERING,
                VPinballEvent.REFRESHING_TABLE_LIST -> {
                    // Parse JSON progress data
                    val progressData = jsonData?.let { jsonStr ->
                        try {
                            Json.decodeFromString<VPinballProgressData>(jsonStr)
                        } catch (e: Exception) {
                            Log.w(TAG, "Failed to parse progress data JSON: $jsonStr", e)
                            null
                        }
                    }
                    Log.v(TAG, "event=${event.name}, data=${progressData}")
                    progressData?.let {
                        CoroutineScope(Dispatchers.Main).launch {
                            viewModel.title(activeTable?.name ?: "")
                            viewModel.progress(progressData.progress)
                            viewModel.status(event.text)
                        }
                    }
                }
                VPinballEvent.PLAYER_STARTED -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                    CoroutineScope(Dispatchers.Main).launch {
                        viewModel.playing(true)
                        delay(500)
                        viewModel.loading(false)
                        if (loadValue(STANDALONE, "TouchInstructions", true)) {
                            viewModel.touchInstructions(true)
                        }
                        if (loadValue(STANDALONE, "TouchOverlay", false)) {
                            viewModel.touchOverlay(true)
                        }
                    }
                    vpinballJNI.VPinballSetWebServerUpdated()
                }
                VPinballEvent.RUMBLE -> {
                    if (haptics) {
                        val rumbleData = jsonData?.let { jsonStr ->
                            try {
                                Json.decodeFromString<VPinballRumbleData>(jsonStr)
                            } catch (e: Exception) {
                                Log.w(TAG, "Failed to parse rumble data JSON: $jsonStr", e)
                                null
                            }
                        }
                        rumbleData?.let { rumble(it) }
                    }
                }
                VPinballEvent.SCRIPT_ERROR -> {
                    if (error == null) {
                        val scriptErrorData = jsonData?.let { jsonStr ->
                            try {
                                Json.decodeFromString<VPinballScriptErrorData>(jsonStr)
                            } catch (e: Exception) {
                                Log.w(TAG, "Failed to parse script error data JSON: $jsonStr", e)
                                null
                            }
                        }
                        error =
                            scriptErrorData?.let { 
                                val errorType = VPinballScriptErrorType.fromInt(it.error)
                                "${errorType.text} on line ${it.line}, position ${it.position}:\n\n${it.description}"
                            } ?: "Script error."
                    }
                }
                VPinballEvent.LIVE_UI_TOGGLE -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                    CoroutineScope(Dispatchers.Main).launch {
                        viewModel.toggleLiveUI()
                        setPlayState(!viewModel.isLiveUI())
                    }
                }
                VPinballEvent.LIVE_UI_UPDATE -> {}
                VPinballEvent.PLAYER_CLOSING -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                }
                VPinballEvent.PLAYER_CLOSED -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                }
                VPinballEvent.STOPPED -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                    activeTable = null
                    CoroutineScope(Dispatchers.Main).launch {
                        viewModel.stopped()
                        error?.let { error ->
                            delay(500)
                            showError(error)
                        }
                    }
                    vpinballJNI.VPinballSetWebServerUpdated()
                }
                VPinballEvent.WEB_SERVER -> {
                    val webServerData = jsonData?.let { jsonStr ->
                        try {
                            Json.decodeFromString<VPinballWebServerData>(jsonStr)
                        } catch (e: Exception) {
                            Log.w(TAG, "Failed to parse web server data JSON: $jsonStr", e)
                            null
                        }
                    }
                    log(VPinballLogLevel.INFO, "event=${event.name}, data=${webServerData}")
                    webServerData?.let { CoroutineScope(Dispatchers.Main).launch { viewModel.webServerURL = webServerData.url } }
                }
                VPinballEvent.CAPTURE_SCREENSHOT -> {
                    val captureScreenshotData = jsonData?.let { jsonStr ->
                        try {
                            Json.decodeFromString<VPinballCaptureScreenshotData>(jsonStr)
                        } catch (e: Exception) {
                            Log.w(TAG, "Failed to parse capture screenshot data JSON: $jsonStr", e)
                            null
                        }
                    }
                    log(VPinballLogLevel.INFO, "event=${event.name}, data=$captureScreenshotData")

                    when (screenshotMode) {
                        ScreenshotMode.INSTRUCTIONS ->
                            viewModel.instructionsImage = BitmapFactory.decodeFile(File(cacheDir, "haze-bg.jpg").absolutePath)?.asImageBitmap()

                        ScreenshotMode.ARTWORK -> {
                            activeTable?.let { table ->
                                // Load image for VPinballVPXTable
                                viewModel.artworkImage = table.loadImage()
                            }
                        }

                        ScreenshotMode.QUIT -> stop()
                        else -> {}
                    }
                }
                VPinballEvent.TABLE_LIST_REFRESH_COMPLETE -> {
                    log(VPinballLogLevel.INFO, "Received TABLE_LIST_REFRESH_COMPLETE event - refreshing Android table list")
                    CoroutineScope(Dispatchers.Main).launch {
                        viewModel.loading(false)
                        // Trigger table list refresh in the landing screen
                        org.vpinball.app.ui.screens.landing.LandingScreenViewModel.triggerRefresh()
                    }
                }
                VPinballEvent.TABLE_LIST -> {
                    return@VPinballInit handleTableList()
                }
                VPinballEvent.TABLE_IMPORT -> {
                    jsonData?.let { jsonStr ->
                        handleTableImport(jsonStr)
                    }
                }
                VPinballEvent.TABLE_RENAME -> {
                    jsonData?.let { jsonStr ->
                        handleTableRename(jsonStr)
                    }
                }
                VPinballEvent.TABLE_DELETE -> {
                    jsonData?.let { jsonStr ->
                        handleTableDelete(jsonStr)
                    }
                }
                else -> {
                    log(VPinballLogLevel.WARN, "event=${event}")
                }
            }
        }

        CoroutineScope(Dispatchers.Main).launch {
            delay(500)
            
            // Scan for tables on startup
            CoroutineScope(Dispatchers.IO).launch {
                val scanResult = vpinballJNI.VPinballRefreshTables()
                if (scanResult == VPinballStatus.SUCCESS.value) {
                    log(VPinballLogLevel.INFO, "Table scan completed successfully on startup")
                } else {
                    log(VPinballLogLevel.ERROR, "Table scan failed on startup")
                }
            }
            
            updateWebServer()
        }
    }

    fun getDisplaySize(): Size {
        return displaySize
    }

    fun getFilesDir(): File {
        return filesDir
    }

    fun log(level: VPinballLogLevel, message: String) {
        vpinballJNI.VPinballLog(level.value, message)
    }

    private fun rumble(data: VPinballRumbleData) {
        if (data.lowFrequencyRumble > 0 || data.highFrequencyRumble > 0) {
            val amplitude =
                when {
                    data.lowFrequencyRumble == data.highFrequencyRumble -> VibrationEffect.DEFAULT_AMPLITUDE
                    data.lowFrequencyRumble > 20000 || data.highFrequencyRumble > 20000 -> 255
                    data.lowFrequencyRumble > 10000 || data.highFrequencyRumble > 10000 -> 200
                    data.lowFrequencyRumble > 1000 || data.highFrequencyRumble > 1000 -> 100
                    else -> 50
                }
            vibrator.vibrate(VibrationEffect.createOneShot(15, amplitude))
        }
    }

    fun updateWebServer() {
        vpinballJNI.VPinballUpdateWebServer()
    }

    fun getVersionString(): String = vpinballJNI.VPinballGetVersionStringFull()

    fun loadValue(section: VPinballSettingsSection, key: String, defaultValue: Int): Int =
        vpinballJNI.VPinballLoadValueInt(section.value, key, defaultValue)

    fun loadValue(section: VPinballSettingsSection, key: String, defaultValue: Float): Float =
        vpinballJNI.VPinballLoadValueFloat(section.value, key, defaultValue)

    fun loadValue(section: VPinballSettingsSection, key: String, defaultValue: Boolean): Boolean =
        loadValue(section, key, if (defaultValue) 1 else 0) == 1

    fun loadValue(section: VPinballSettingsSection, key: String, defaultValue: String): String =
        vpinballJNI.VPinballLoadValueString(section.value, key, defaultValue)

    fun saveValue(section: VPinballSettingsSection, key: String, value: Int) {
        vpinballJNI.VPinballSaveValueInt(section.value, key, value)
    }

    fun saveValue(section: VPinballSettingsSection, key: String, value: Float) {
        vpinballJNI.VPinballSaveValueFloat(section.value, key, value)
    }

    fun saveValue(section: VPinballSettingsSection, key: String, value: Boolean) {
        saveValue(section, key, if (value) 1 else 0)
    }

    fun saveValue(section: VPinballSettingsSection, key: String, value: String) {
        vpinballJNI.VPinballSaveValueString(section.value, key, value)
    }

    fun resetIni() {
        vpinballJNI.VPinballResetIni()
    }

    fun toggleFPS() {
        vpinballJNI.VPinballToggleFPS()
    }

    fun setPlayState(enable: Boolean) {
        vpinballJNI.VPinballSetPlayState(if (enable) 1 else 0)
    }

    fun getCustomTableOptions(): List<CustomTableOption> {
        return try {
            val jsonString = vpinballJNI.VPinballGetCustomTableOptions()
            val options = Json.decodeFromString<List<CustomTableOption>>(jsonString)
            vpinballJNI.VPinballFreeString(jsonString)
            options
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "getCustomTableOptions: Exception: ${e.message}")
            emptyList()
        }
    }

    fun setCustomTableOption(customTableOption: CustomTableOption) {
        try {
            val jsonString = Json.encodeToString(customTableOption)
            vpinballJNI.VPinballSetCustomTableOption(jsonString)
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "setCustomTableOption: Exception: ${e.message}")
        }
    }

    fun resetCustomTableOptions() {
        vpinballJNI.VPinballResetCustomTableOptions()
    }

    fun saveCustomTableOptions() {
        vpinballJNI.VPinballSaveCustomTableOptions()
    }

    fun getTableOptions(): TableOptions? {
        return try {
            val jsonString = vpinballJNI.VPinballGetTableOptions()
            val options = Json.decodeFromString<TableOptions>(jsonString)
            vpinballJNI.VPinballFreeString(jsonString)
            options
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "getTableOptions: Exception: ${e.message}")
            null
        }
    }

    fun setTableOptions(tableOptions: TableOptions) {
        try {
            val jsonString = Json.encodeToString(tableOptions)
            vpinballJNI.VPinballSetTableOptions(jsonString)
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "setTableOptions: Exception: ${e.message}")
        }
    }

    fun resetTableOptions() {
        vpinballJNI.VPinballResetTableOptions()
    }

    fun saveTableOptions() {
        vpinballJNI.VPinballSaveTableOptions()
    }

    fun getViewSetup(): ViewSetup? {
        return try {
            val jsonString = vpinballJNI.VPinballGetViewSetup()
            val viewSetup = Json.decodeFromString<ViewSetup>(jsonString)
            vpinballJNI.VPinballFreeString(jsonString)
            viewSetup
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "getViewSetup: Exception: ${e.message}")
            null
        }
    }

    fun setViewSetup(viewSetup: ViewSetup) {
        try {
            val jsonString = Json.encodeToString(viewSetup)
            vpinballJNI.VPinballSetViewSetup(jsonString)
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "setViewSetup: Exception: ${e.message}")
        }
    }

    fun setDefaultViewSetup() {
        vpinballJNI.VPinballSetDefaultViewSetup()
    }

    fun resetViewSetup() {
        vpinballJNI.VPinballResetViewSetup()
    }

    fun saveViewSetup() {
        vpinballJNI.VPinballSaveViewSetup()
    }

    fun hasScreenshot(): Boolean {
        return activeTable?.hasImage ?: false
    }

    fun captureScreenshot(mode: ScreenshotMode) {
        activeTable?.let { table ->
            screenshotMode = mode
            val path =
                when (mode) {
                    ScreenshotMode.INSTRUCTIONS -> File(cacheDir, "haze-bg.jpg").absolutePath
                    else -> table.fullPath.substringBeforeLast('.') + ".jpg"
                }
            vpinballJNI.VPinballCaptureScreenshot(path)
        }
    }

    //
    // Import process:
    //
    // Delete contents of cacheDir
    // Determine filename from uri
    // Copy uri to cacheDir
    //
    // If zip or vpxz, uncompress, and delete zip or vpxz
    // If vpx, do nothing
    //
    // Search for first vpx file in cacheDir
    // If found, generate uuid folder in filesDir,
    // and recursively copy from vpx file folder into uuid folder
    //

    fun importUri(
        context: Context,
        uri: Uri,
        onUpdate: (Int, String) -> Unit,
        onComplete: (uuid: String, path: String) -> Unit,
        onError: () -> Unit,
    ) {
        CoroutineScope(Dispatchers.IO).launch {
            try {
                // Clean up cache directory
                cacheDir.deleteRecursively()
                cacheDir.mkdir()
                
                // Get filename from URI
                val filename = FileUtils.filenameFromUri(context, uri)
                if (filename == null) {
                    log(VPinballLogLevel.ERROR, "Unable to get filename: uri=$uri")
                    withContext(Dispatchers.Main) {
                        onError()
                        showErrorAndReset("Unable to import table.")
                    }
                    return@launch
                }
                
                withContext(Dispatchers.Main) { onUpdate(20, "Copying file") }
                
                // Copy file to temp location for processing
                val tempFile = File(cacheDir, filename)
                FileUtils.copyFile(context, uri, tempFile) { progress -> 
                    launch(Dispatchers.Main) { onUpdate((20 + progress * 0.3).toInt(), "Copying file") } 
                }
                
                withContext(Dispatchers.Main) { onUpdate(60, "Importing table") }
                
                // Extract table name from filename (remove extension)
                // Use the new unified import method
                val importResult = vpinballJNI.VPinballImportTableFile(tempFile.absolutePath)
                
                // Clean up temp file
                tempFile.delete()
                
                if (importResult == VPinballStatus.SUCCESS.value) {
                    log(VPinballLogLevel.INFO, "Successfully imported table: $filename")
                    
                    withContext(Dispatchers.Main) { onUpdate(90, "Refreshing table list") }
                    
                    // Trigger table list refresh
                    vpinballJNI.VPinballRefreshTables()
                    
                    // For Android, we need to find the imported table since we don't know the UUID
                    // We'll return a placeholder - the calling code should refresh the table list
                    withContext(Dispatchers.Main) { 
                        onUpdate(100, "Import complete")
                        onComplete("imported", filename) 
                    }
                } else {
                    log(VPinballLogLevel.ERROR, "Failed to import table: $filename")
                    withContext(Dispatchers.Main) {
                        onError()
                        showErrorAndReset("Unable to import table.")
                    }
                }
                
            } catch (e: Exception) {
                log(VPinballLogLevel.ERROR, "Import error: ${e.message}")
                withContext(Dispatchers.Main) {
                    onError()
                    showErrorAndReset("Unable to import table.")
                }
            }
        }
    }

    fun extractScript(table: VPinballVPXTable, onComplete: () -> Unit, onError: () -> Unit) {
        if (activeTable != null) return
        activeTable = table
        CoroutineScope(Dispatchers.IO).launch {
            if (!File(table.fullPath).exists()) {
                withContext(Dispatchers.Main) {
                    onError()
                    showErrorAndReset("Unable to extract script.")
                }
                return@launch
            }
            val status = vpinballJNI.VPinballExtractScript(table.fullPath)
            withContext(Dispatchers.Main) {
                if (status == VPinballStatus.SUCCESS.value) {
                    onComplete()
                } else {
                    onError()
                    showErrorAndReset("Unable to extract script.")
                }
                activeTable = null
            }
        }
    }

    fun share(table: VPinballVPXTable, onComplete: (file: File) -> Unit, onError: () -> Unit) {
        if (activeTable != null) return
        activeTable = table
        CoroutineScope(Dispatchers.IO).launch {
            val tableFile = File(table.fullPath)
            if (!tableFile.exists()) {
                withContext(Dispatchers.Main) {
                    onError()
                    showErrorAndReset("Unable to share table.")
                }
                return@launch
            }
            try {
                val exportPath = vpinballJNI.VPinballExportTable(table.uuid)
                withContext(Dispatchers.Main) {
                    if (exportPath != null) {
                        val exportFile = File(exportPath)
                        onComplete(exportFile)
                        activeTable = null
                    } else {
                        onError()
                        showErrorAndReset("Failed to export table.")
                    }
                }
            } catch (e: Exception) {
                log(VPinballLogLevel.ERROR, "An error occurred: ${e.message}")
                showErrorAndReset("Unable to share table.")
            }
        }
    }

    fun play(table: VPinballVPXTable) {
        if (activeTable != null) return
        activeTable = table
        error = null
        CoroutineScope(Dispatchers.IO).launch {
            if (!File(table.fullPath).exists()) {
                showErrorAndReset("Unable to load table.")
                return@launch
            }
            if (loadValue(STANDALONE, "ResetLogOnPlay", true)) {
                vpinballJNI.VPinballResetLog()
            }
            haptics = loadValue(STANDALONE, "Haptics", true)
            withContext(Dispatchers.Main) { activity.viewModel.loading(true, table) }
            if (vpinballJNI.VPinballLoad(table.fullPath) == VPinballStatus.SUCCESS.value) {
                vpinballJNI.VPinballPlay()
            } else {
                delay(500)
                activity.viewModel.stopped()
                showErrorAndReset("Unable to load table.")
            }
        }
    }

    fun stop() {
        vpinballJNI.VPinballStop()
    }

    fun showError(message: String) {
        activity.viewModel.setError(message)
    }

    private fun showErrorAndReset(message: String) {
        CoroutineScope(Dispatchers.Main).launch {
            delay(250)
            showError(message)
            activeTable = null
        }
    }

    private fun handleTableList(): VPinballTablesData {
        return try {
            // Use C++ library to get VPX tables as JSON
            val jsonString = vpinballJNI.VPinballGetVPXTables()
            log(VPinballLogLevel.DEBUG, "handleTableList: Received JSON response")
            
            // Parse JSON response
            val tablesResponse = Json.decodeFromString<VPXTablesResponse>(jsonString)
            vpinballJNI.VPinballFreeString(jsonString)
            
            log(VPinballLogLevel.DEBUG, "handleTableList: Found ${tablesResponse.tableCount} tables")
            
            // Convert to legacy format for backward compatibility
            val tableInfoList = tablesResponse.tables.map { table -> 
                VPinballTableInfo(tableId = table.uuid, name = table.name) 
            }
            
            VPinballTablesData(tables = tableInfoList, success = tablesResponse.success)
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "handleTableList: Exception: ${e.message}")
            VPinballTablesData(tables = emptyList(), success = false)
        }
    }

    private fun handleTableImport(jsonString: String) {
        try {
            val eventData = Json.decodeFromString<VPinballTableEventData>(jsonString)
            val filePath = eventData.path ?: ""

            kotlinx.coroutines.runBlocking(Dispatchers.IO) {
                try {
                    log(VPinballLogLevel.DEBUG, "handleTableImport: Importing table from $filePath")
                    
                    // Use C++ library to import the table file instead of manual process
                    val status = vpinballJNI.VPinballImportTableFile(filePath)
                    
                    val success = (status == VPinballStatus.SUCCESS.value)
                    log(VPinballLogLevel.DEBUG, "handleTableImport: Import ${if (success) "succeeded" else "failed"}")
                    
                    if (success) {
                        withContext(Dispatchers.Main) {
                            vpinballJNI.VPinballSetWebServerUpdated()
                        }
                    }
                } catch (e: Exception) {
                    log(VPinballLogLevel.ERROR, "Failed to import table: ${e.message}")
                }
            }
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "Failed to decode table import event data: ${e.message}")
        }
    }

    private fun handleTableRename(jsonString: String) {
        try {
            val eventData = Json.decodeFromString<VPinballTableEventData>(jsonString)
            val tableId = eventData.tableId ?: ""
            val newName = eventData.newName ?: ""

            kotlinx.coroutines.runBlocking(Dispatchers.IO) {
                try {
                    log(VPinballLogLevel.DEBUG, "handleTableRename: Renaming table $tableId to $newName")
                    
                    // Use C++ library to rename the table instead of Room database
                    val status = vpinballJNI.VPinballRenameVPXTable(tableId, newName)
                    
                    val success = (status == VPinballStatus.SUCCESS.value)
                    log(VPinballLogLevel.DEBUG, "handleTableRename: Rename ${if (success) "succeeded" else "failed"}")
                    
                    if (success) {
                        withContext(Dispatchers.Main) {
                            vpinballJNI.VPinballSetWebServerUpdated()
                        }
                    }
                } catch (e: Exception) {
                    log(VPinballLogLevel.ERROR, "Failed to rename table: ${e.message}")
                }
            }
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "Failed to decode table rename event data: ${e.message}")
        }
    }

    private fun handleTableDelete(jsonString: String) {
        try {
            val eventData = Json.decodeFromString<VPinballTableEventData>(jsonString)
            val tableId = eventData.tableId ?: ""

            kotlinx.coroutines.runBlocking(Dispatchers.IO) {
                try {
                    log(VPinballLogLevel.DEBUG, "handleTableDelete: Deleting table $tableId")
                    
                    // Use C++ library to remove the table instead of Room database
                    val status = vpinballJNI.VPinballRemoveVPXTable(tableId)
                    
                    val success = (status == VPinballStatus.SUCCESS.value)
                    log(VPinballLogLevel.DEBUG, "handleTableDelete: Delete ${if (success) "succeeded" else "failed"}")
                    
                    if (success) {
                        withContext(Dispatchers.Main) {
                            vpinballJNI.VPinballSetWebServerUpdated()
                        }
                    }
                } catch (e: Exception) {
                    log(VPinballLogLevel.ERROR, "Failed to delete table: ${e.message}")
                }
            }
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "Failed to decode table delete event data: ${e.message}")
        }
    }

    fun setWebLastUpdate() {
        vpinballJNI.VPinballSetWebServerUpdated()
    }

    fun getTablesPath(): File {
        val tablesPath = vpinballJNI.VPinballGetTablesPath()
        return File(tablesPath)
    }
}
