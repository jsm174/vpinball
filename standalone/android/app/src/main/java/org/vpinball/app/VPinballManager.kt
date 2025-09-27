package org.vpinball.app

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.provider.DocumentsContract
import android.util.Log
import android.util.Size
import androidx.lifecycle.viewmodel.compose.viewModel
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import org.json.JSONArray
import org.koin.core.component.KoinComponent
import org.vpinball.app.jni.*
import org.vpinball.app.jni.VPinballSettingsSection.STANDALONE
import org.vpinball.app.util.FileUtils

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

    private var activeTable: Table? = null
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

        vpinballJNI.VPinballSetEventCallback { value, jsonData, rawData ->
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
                VPinballEvent.PRERENDERING -> {
                    // Parse JSON progress data
                    val progressData =
                        jsonData?.let { jsonStr ->
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
                }
                VPinballEvent.RUMBLE -> {
                    if (haptics) {
                        val rumbleData =
                            jsonData?.let { jsonStr ->
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
                        val scriptErrorData =
                            jsonData?.let { jsonStr ->
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
                VPinballEvent.PLAYER_CLOSING -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                }
                VPinballEvent.PLAYER_CLOSED -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                    activeTable = null
                    CoroutineScope(Dispatchers.Main).launch { viewModel.playing(false) }
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
                }
                VPinballEvent.WEB_SERVER -> {
                    val webServerData =
                        jsonData?.let { jsonStr ->
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
                    // TODO: Implement screenshot handling
                    /*
                    val captureScreenshotData =
                        jsonData?.let { jsonStr ->
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
                                // Load image for Table
                                viewModel.artworkImage = table.loadImage()
                            }
                        }

                        ScreenshotMode.QUIT -> stop()
                        else -> {}
                    }
                    */
                }
                VPinballEvent.TABLE_LIST_UPDATED -> {
                    log(VPinballLogLevel.INFO, "Received TABLE_LIST_UPDATED event - refreshing Android table list")

                    // Check if there's a focusUuid in the JSON
                    val focusUuid =
                        jsonData?.let { jsonStr ->
                            try {
                                @Serializable data class TableListUpdatedData(val focusUuid: String? = null)
                                val data = Json.decodeFromString<TableListUpdatedData>(jsonStr)
                                data.focusUuid
                            } catch (e: Exception) {
                                null
                            }
                        }

                    CoroutineScope(Dispatchers.Main).launch {
                        viewModel.loading(false)
                        // Trigger table list refresh in the landing screen
                        org.vpinball.app.ui.screens.landing.LandingScreenViewModel.triggerRefresh()

                        // If there's a focusUuid, scroll to it
                        focusUuid?.let { uuid -> org.vpinball.app.ui.screens.landing.LandingScreenViewModel.triggerScrollToTable(uuid) }
                    }
                }
                else -> {
                    log(VPinballLogLevel.WARN, "event=${event}")
                }
            }
        }

        CoroutineScope(Dispatchers.Main).launch {
            delay(1000)

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

    private fun sendCopyProgressEvent(current: Int, total: Int) {
        val progressPercent = if (total > 0) (current * 100) / total else 0

        // Update UI progress
        activity.runOnUiThread {
            activity.viewModel.progress(progressPercent)
            activity.viewModel.status("Copying table files... ($current/$total)")
        }
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
        // TODO: Implement web server update
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
            // TODO: Implement screenshot capture
            // vpinballJNI.VPinballCaptureScreenshot(path)
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

                // Use the new unified import method
                val status = vpinballJNI.VPinballImportTable(tempFile.absolutePath)

                // Clean up temp file
                tempFile.delete()

                if (status == VPinballStatus.SUCCESS.value) {
                    log(VPinballLogLevel.INFO, "Successfully imported table: $filename")

                    withContext(Dispatchers.Main) {
                        onUpdate(100, "Import complete")
                        onComplete("", filename)
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

    fun extractScript(table: Table, onComplete: () -> Unit, onError: () -> Unit) {
        if (activeTable != null) return
        activeTable = table
        CoroutineScope(Dispatchers.IO).launch {
            // Load the table - C++ side handles validation for both normal and SAF paths
            val loadStatus = vpinballJNI.VPinballLoad(table.uuid)
            if (loadStatus != VPinballStatus.SUCCESS.value) {
                withContext(Dispatchers.Main) {
                    onError()
                    showErrorAndReset("Unable to load table.")
                    activeTable = null
                }
                return@launch
            }
            // Then extract script from the loaded table
            val extractStatus = vpinballJNI.VPinballExtractScript()
            withContext(Dispatchers.Main) {
                if (extractStatus == VPinballStatus.SUCCESS.value) {
                    onComplete()
                } else {
                    onError()
                    showErrorAndReset("Unable to extract script.")
                }
                activeTable = null
            }
        }
    }

    fun share(table: Table, onComplete: (file: File) -> Unit, onError: () -> Unit) {
        if (activeTable != null) return
        activeTable = table
        CoroutineScope(Dispatchers.IO).launch {
            if (!table.exists()) {
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

    fun play(table: Table) {
        if (activeTable != null) return
        activeTable = table
        error = null
        CoroutineScope(Dispatchers.IO).launch {
            if (loadValue(STANDALONE, "ResetLogOnPlay", true)) {
                vpinballJNI.VPinballResetLog()
            }
            haptics = loadValue(STANDALONE, "Haptics", true)
            withContext(Dispatchers.Main) { activity.viewModel.loading(true, table) }
            // Load the table - C++ side handles validation for both normal and SAF paths
            if (vpinballJNI.VPinballLoad(table.uuid) == VPinballStatus.SUCCESS.value) {
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

    fun fileExists(path: String): Boolean {
        return vpinballJNI.VPinballFileExists(path)
    }

    fun prepareFileForViewing(path: String): String? {
        return vpinballJNI.VPinballPrepareFileForViewing(path)
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

    fun setWebLastUpdate() {
        // Web server update removed
    }

    fun getTablesPath(): String {
        return vpinballJNI.VPinballGetTablesPath()
    }

    fun getCurrentTablesPath(): String {
        return vpinballJNI.VPinballGetTablesPath()
    }

    fun isTablesPathSAF(): Boolean {
        return getTablesPath().startsWith("content://")
    }

    fun logAvailableStoragePaths() {
        log(VPinballLogLevel.INFO, "=== TESTING getExternalFilesDirs() ===")
        val externalDirs = activity.getExternalFilesDirs(null)
        log(VPinballLogLevel.INFO, "Found ${externalDirs.size} external storage locations:")
        externalDirs.forEachIndexed { index, file ->
            if (file != null) {
                log(VPinballLogLevel.INFO, "  [$index] ${file.absolutePath}")
                log(VPinballLogLevel.INFO, "       Exists: ${file.exists()}, CanWrite: ${file.canWrite()}")
            } else {
                log(VPinballLogLevel.INFO, "  [$index] null")
            }
        }
        log(VPinballLogLevel.INFO, "=== END STORAGE TEST ===")
    }

    fun reloadTablesPath() {
        log(VPinballLogLevel.INFO, "VPinballManager: reloadTablesPath() called")
        CoroutineScope(Dispatchers.IO).launch {
            val status = vpinballJNI.VPinballReloadTablesPath()
            if (status == VPinballStatus.SUCCESS.value) {
                log(VPinballLogLevel.INFO, "VPinballManager: Successfully reloaded tables path")
            } else {
                log(VPinballLogLevel.ERROR, "VPinballManager: Failed to reload tables path - status: $status")
            }
        }
    }

    // ===== SAF (Storage Access Framework) Support =====

    fun setExternalStorageUri(uri: Uri) {
        log(VPinballLogLevel.INFO, "VPinballManager: setExternalStorageUri: $uri")

        // Take persistent permission
        try {
            activity.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "VPinballManager: Failed to take persistent permission: ${e.message}")
            return
        }

        // Save tree URI directly to TablesPath
        saveValue(VPinballSettingsSection.STANDALONE, "TablesPath", uri.toString())

        log(VPinballLogLevel.INFO, "VPinballManager: External storage URI saved to TablesPath")
    }

    fun getExternalStorageUri(): Uri? {
        val tablesPath = getTablesPath()
        return if (isTablesPathSAF()) {
            Uri.parse(tablesPath).also {
                log(VPinballLogLevel.INFO, "VPinballManager: External storage URI from TablesPath: $it")
            }
        } else {
            null
        }
    }

    fun clearExternalStorageUri() {
        saveValue(VPinballSettingsSection.STANDALONE, "TablesPath", "")
        log(VPinballLogLevel.INFO, "VPinballManager: Cleared TablesPath")
    }

    fun getExternalStorageDisplayPath(): String {
        val uri = getExternalStorageUri() ?: return ""

        try {
            val docId = DocumentsContract.getTreeDocumentId(uri)
            log(VPinballLogLevel.INFO, "VPinballManager: getExternalStorageDisplayPath - URI: $uri")
            log(VPinballLogLevel.INFO, "VPinballManager: getExternalStorageDisplayPath - docId: $docId")

            val split = docId.split(":")

            if (split.size >= 2) {
                val type = split[0]
                val path = split[1]

                val displayPath = when {
                    type.equals("primary", ignoreCase = true) -> "/storage/emulated/0/$path"
                    else -> "/storage/$type/$path"
                }

                log(VPinballLogLevel.INFO, "VPinballManager: getExternalStorageDisplayPath - result: $displayPath")
                return displayPath
            }

            log(VPinballLogLevel.WARN, "VPinballManager: getExternalStorageDisplayPath - returning docId: $docId")
            return docId
        } catch (e: Exception) {
            log(VPinballLogLevel.WARN, "VPinballManager: Failed to parse URI: ${e.message}")
            e.printStackTrace()
            return uri.toString()
        }
    }

    // SAF file operations (called from C++ via JNI)

    fun safWriteFile(relativePath: String, content: String): Boolean {
        log(VPinballLogLevel.INFO, "SAF: writeFile: $relativePath (${content.length} bytes)")

        val uri = getExternalStorageUri()
        if (uri == null) {
            log(VPinballLogLevel.ERROR, "SAF: writeFile: No external storage URI set")
            return false
        }

        return try {
            val docUri = buildDocumentUri(uri, relativePath, createIfMissing = true)
            if (docUri == null) {
                log(VPinballLogLevel.ERROR, "SAF: writeFile: Failed to build document URI for: $relativePath")
                return false
            }

            activity.contentResolver.openOutputStream(docUri, "wt")?.use { output ->
                output.write(content.toByteArray())
                output.flush()
            }

            log(VPinballLogLevel.INFO, "SAF: writeFile: Success")
            true
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: writeFile: Exception: ${e.message}")
            e.printStackTrace()
            false
        }
    }

    fun safReadFile(relativePath: String): String? {
        log(VPinballLogLevel.INFO, "SAF: readFile: $relativePath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            log(VPinballLogLevel.ERROR, "SAF: readFile: No external storage URI set")
            return null
        }

        return try {
            val docUri = buildDocumentUri(uri, relativePath)
            if (docUri == null) {
                log(VPinballLogLevel.WARN, "SAF: readFile: File not found: $relativePath")
                return null
            }

            val content = activity.contentResolver.openInputStream(docUri)?.use { input ->
                input.bufferedReader().readText()
            }

            log(VPinballLogLevel.INFO, "SAF: readFile: Read ${content?.length ?: 0} bytes")
            content
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: readFile: Exception: ${e.message}")
            e.printStackTrace()
            null
        }
    }

    fun safExists(relativePath: String): Boolean {
        val uri = getExternalStorageUri() ?: return false
        val docUri = buildDocumentUri(uri, relativePath)
        val exists = docUri != null
        log(VPinballLogLevel.INFO, "SAF: exists: $relativePath -> $exists")
        return exists
    }

    fun safListFilesRecursive(extension: String): String {
        log(VPinballLogLevel.INFO, "SAF: listFilesRecursive: extension=$extension")

        val uri = getExternalStorageUri()
        if (uri == null) {
            log(VPinballLogLevel.ERROR, "SAF: listFilesRecursive: No external storage URI set")
            return "[]"
        }

        val results = mutableListOf<String>()

        fun scanRecursive(docUri: Uri, currentPath: String = "") {
            try {
                val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                    uri,
                    DocumentsContract.getDocumentId(docUri)
                )

                activity.contentResolver.query(
                    childrenUri,
                    arrayOf(
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_MIME_TYPE,
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID
                    ),
                    null,
                    null,
                    null
                )?.use { cursor ->
                    val nameIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME)
                    val mimeIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE)
                    val idIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID)

                    while (cursor.moveToNext()) {
                        val name = cursor.getString(nameIndex)
                        val mimeType = cursor.getString(mimeIndex)
                        val documentId = cursor.getString(idIndex)

                        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
                            // Recurse into subdirectory
                            val childUri = DocumentsContract.buildDocumentUriUsingTree(uri, documentId)
                            scanRecursive(childUri, "$currentPath$name/")
                        } else if (name.endsWith(extension, ignoreCase = true)) {
                            // Found matching file
                            results.add("$currentPath$name")
                        }
                    }
                }
            } catch (e: Exception) {
                log(VPinballLogLevel.ERROR, "SAF: scanRecursive: Exception in $currentPath: ${e.message}")
                e.printStackTrace()
            }
        }

        // Start scanning from the root document URI
        val treeDocumentId = DocumentsContract.getTreeDocumentId(uri)
        val rootDocUri = DocumentsContract.buildDocumentUriUsingTree(uri, treeDocumentId)
        scanRecursive(rootDocUri)

        val jsonArray = JSONArray(results)
        log(VPinballLogLevel.INFO, "SAF: listFilesRecursive: Found ${results.size} files")
        return jsonArray.toString()
    }

    private fun buildDocumentUri(treeUri: Uri, relativePath: String, createIfMissing: Boolean = false): Uri? {
        if (relativePath.isEmpty()) return treeUri

        log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: path='$relativePath' create=$createIfMissing")

        try {
            // Start with the root document ID from the tree URI
            val treeDocumentId = DocumentsContract.getTreeDocumentId(treeUri)
            var currentUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, treeDocumentId)

            val segments = relativePath.split("/").filter { it.isNotEmpty() }

            log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: segments=${segments.joinToString("/")}")

            for ((index, segment) in segments.withIndex()) {
                val isLastSegment = (index == segments.size - 1)

                // Query for this segment
                val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                    treeUri,
                    DocumentsContract.getDocumentId(currentUri)
                )

                var foundUri: Uri? = null

                activity.contentResolver.query(
                    childrenUri,
                    arrayOf(
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_MIME_TYPE
                    ),
                    null,
                    null,
                    null
                )?.use { cursor ->
                    val nameIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME)
                    val idIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID)

                    while (cursor.moveToNext()) {
                        val name = cursor.getString(nameIndex)
                        if (name == segment) {
                            val documentId = cursor.getString(idIndex)
                            foundUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId)
                            break
                        }
                    }
                }

                if (foundUri != null) {
                    log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Found segment '$segment'")
                    currentUri = foundUri!!
                } else if (createIfMissing) {
                    // Create missing directory or file
                    val mimeType = if (isLastSegment && segment.contains(".")) {
                        "application/octet-stream" // File
                    } else {
                        DocumentsContract.Document.MIME_TYPE_DIR // Directory
                    }

                    log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Creating '$segment' (type=$mimeType)")

                    val newUri = DocumentsContract.createDocument(
                        activity.contentResolver,
                        currentUri,
                        mimeType,
                        segment
                    )

                    if (newUri != null) {
                        log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Created '$segment' -> $newUri")
                        currentUri = newUri
                    } else {
                        log(VPinballLogLevel.ERROR, "SAF: buildDocumentUri: Failed to create: $segment")
                        return null
                    }
                } else {
                    log(VPinballLogLevel.WARN, "SAF: buildDocumentUri: Segment '$segment' not found and create=false")
                    return null
                }
            }

            log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Success -> $currentUri")
            return currentUri
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: buildDocumentUri: Exception: ${e.message}")
            e.printStackTrace()
            return null
        }
    }

    fun safCopyFile(sourcePath: String, destPath: String): Boolean {
        log(VPinballLogLevel.INFO, "SAF: copyFile: $sourcePath -> $destPath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            log(VPinballLogLevel.ERROR, "SAF: copyFile: No external storage URI set")
            return false
        }

        try {
            val sourceUri = buildDocumentUri(uri, sourcePath, createIfMissing = false)
            if (sourceUri == null) {
                log(VPinballLogLevel.ERROR, "SAF: copyFile: Source not found: $sourcePath")
                return false
            }

            val destUri = buildDocumentUri(uri, destPath, createIfMissing = true)
            if (destUri == null) {
                log(VPinballLogLevel.ERROR, "SAF: copyFile: Failed to create destination: $destPath")
                return false
            }

            activity.contentResolver.openInputStream(sourceUri)?.use { input ->
                activity.contentResolver.openOutputStream(destUri, "wt")?.use { output ->
                    input.copyTo(output)
                    return true
                }
            }

            log(VPinballLogLevel.ERROR, "SAF: copyFile: Failed to open streams")
            return false
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: copyFile: Exception: ${e.message}")
            e.printStackTrace()
            return false
        }
    }

    fun safCopySAFToFilesystem(safRelativePath: String, destPath: String): Boolean {
        log(VPinballLogLevel.INFO, "SAF: copySAFToFilesystem: $safRelativePath -> $destPath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            log(VPinballLogLevel.ERROR, "SAF: copySAFToFilesystem: No external storage URI")
            return false
        }

        // Track progress
        var totalFiles = 0
        var copiedFiles = 0

        // First pass: count total files
        fun countFiles(srcRelPath: String): Int {
            val srcUri = buildDocumentUri(uri, srcRelPath) ?: return 0
            var count = 0

            try {
                activity.contentResolver.query(
                    srcUri,
                    arrayOf(DocumentsContract.Document.COLUMN_MIME_TYPE),
                    null, null, null
                )?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val mimeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                        val mimeType = cursor.getString(mimeIndex)

                        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
                            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                                uri,
                                DocumentsContract.getDocumentId(srcUri)
                            )

                            activity.contentResolver.query(
                                childrenUri,
                                arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME),
                                null, null, null
                            )?.use { childCursor ->
                                val nameIndex = childCursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME)

                                while (childCursor.moveToNext()) {
                                    val name = childCursor.getString(nameIndex)
                                    val childPath = if (srcRelPath.isEmpty()) name else "$srcRelPath/$name"
                                    count += countFiles(childPath)
                                }
                            }
                        } else {
                            count = 1
                        }
                    }
                }
            } catch (e: Exception) {
                log(VPinballLogLevel.ERROR, "SAF: countFiles: Exception: ${e.message}")
            }

            return count
        }

        // Count total files first
        totalFiles = countFiles(safRelativePath)
        log(VPinballLogLevel.INFO, "SAF: copySAFToFilesystem: Total files to copy: $totalFiles")

        // Send initial progress event
        sendCopyProgressEvent(0, totalFiles)

        fun copyRecursive(srcRelPath: String, dstPath: String): Boolean {
            val srcUri = buildDocumentUri(uri, srcRelPath)
            if (srcUri == null) {
                log(VPinballLogLevel.ERROR, "SAF: copySAFToFilesystem: Source not found: $srcRelPath")
                return false
            }

            try {
                activity.contentResolver.query(
                    srcUri,
                    arrayOf(DocumentsContract.Document.COLUMN_MIME_TYPE),
                    null, null, null
                )?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val mimeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                        val mimeType = cursor.getString(mimeIndex)

                        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
                            java.io.File(dstPath).mkdirs()

                            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                                uri,
                                DocumentsContract.getDocumentId(srcUri)
                            )

                            activity.contentResolver.query(
                                childrenUri,
                                arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME),
                                null, null, null
                            )?.use { childCursor ->
                                val nameIndex = childCursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME)

                                while (childCursor.moveToNext()) {
                                    val name = childCursor.getString(nameIndex)
                                    val childSrcPath = if (srcRelPath.isEmpty()) name else "$srcRelPath/$name"
                                    val childDstPath = "$dstPath/$name"

                                    if (!copyRecursive(childSrcPath, childDstPath)) {
                                        return false
                                    }
                                }
                            }

                            return true
                        } else {
                            activity.contentResolver.openInputStream(srcUri)?.use { input ->
                                java.io.FileOutputStream(dstPath).use { output ->
                                    val bytes = input.copyTo(output)
                                    log(VPinballLogLevel.INFO, "SAF: copySAFToFilesystem: Copied $bytes bytes: $srcRelPath -> $dstPath")
                                }
                            }
                            copiedFiles++
                            sendCopyProgressEvent(copiedFiles, totalFiles)
                            return true
                        }
                    }
                }
            } catch (e: Exception) {
                log(VPinballLogLevel.ERROR, "SAF: copySAFToFilesystem: Exception: ${e.message}")
                return false
            }

            return false
        }

        val result = copyRecursive(safRelativePath, destPath)

        // Send completion event
        if (result) {
            activity.runOnUiThread {
                activity.viewModel.progress(100)
                activity.viewModel.status("Copy complete!")
            }
        }

        return result
    }

    fun safCopyDirectory(sourcePath: String, destPath: String): Boolean {
        log(VPinballLogLevel.INFO, "SAF: copyDirectory: $sourcePath -> $destPath")

        try {
            val sourceFile = java.io.File(sourcePath)
            if (!sourceFile.exists()) {
                log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Source doesn't exist: $sourcePath")
                return false
            }

            // Handle both files and directories
            if (sourceFile.isFile) {
                log(VPinballLogLevel.INFO, "SAF: copyDirectory: Source is a file, copying as single file")
                val uri = getExternalStorageUri()
                if (uri == null) {
                    log(VPinballLogLevel.ERROR, "SAF: copyDirectory: No external storage URI")
                    return false
                }

                val destUri = buildDocumentUri(uri, destPath, createIfMissing = true)
                if (destUri == null) {
                    log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to create: $destPath")
                    return false
                }

                try {
                    val fileSize = sourceFile.length()
                    log(VPinballLogLevel.INFO, "SAF: copyDirectory: Copying file (${fileSize} bytes)")

                    java.io.FileInputStream(sourceFile).use { input ->
                        activity.contentResolver.openOutputStream(destUri, "w")?.use { output ->
                            val bytesCopied = input.copyTo(output)
                            output.flush()
                            log(VPinballLogLevel.INFO, "SAF: copyDirectory: Copied $bytesCopied bytes")
                            if (bytesCopied != fileSize) {
                                log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Size mismatch! Expected $fileSize, got $bytesCopied")
                                return false
                            }
                        } ?: run {
                            log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to open output stream")
                            return false
                        }
                    }
                    log(VPinballLogLevel.INFO, "SAF: copyDirectory: Successfully copied file: $destPath")
                    return true
                } catch (e: Exception) {
                    log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to copy file: ${e.message}")
                    e.printStackTrace()
                    return false
                }
            }

            if (!sourceFile.isDirectory) {
                log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Source is neither file nor directory: $sourcePath")
                return false
            }

            fun copyRecursive(srcFile: java.io.File, destRelativePath: String): Boolean {
                if (srcFile.isDirectory) {
                    val children = srcFile.listFiles() ?: emptyArray()
                    for (child in children) {
                        val childDestPath = if (destRelativePath.isEmpty()) {
                            child.name
                        } else {
                            "$destRelativePath/${child.name}"
                        }
                        if (!copyRecursive(child, childDestPath)) {
                            return false
                        }
                    }
                    return true
                } else {
                    val uri = getExternalStorageUri()
                    if (uri == null) {
                        log(VPinballLogLevel.ERROR, "SAF: copyDirectory: No external storage URI")
                        return false
                    }

                    val destUri = buildDocumentUri(uri, destRelativePath, createIfMissing = true)
                    if (destUri == null) {
                        log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to create: $destRelativePath")
                        return false
                    }

                    try {
                        java.io.FileInputStream(srcFile).use { input ->
                            activity.contentResolver.openOutputStream(destUri, "wt")?.use { output ->
                                input.copyTo(output)
                            } ?: return false
                        }
                        log(VPinballLogLevel.INFO, "SAF: copyDirectory: Copied file: $destRelativePath")
                        return true
                    } catch (e: Exception) {
                        log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to copy file: ${e.message}")
                        return false
                    }
                }
            }

            return copyRecursive(sourceFile, destPath)
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Exception: ${e.message}")
            e.printStackTrace()
            return false
        }
    }

    fun safDelete(relativePath: String): Boolean {
        log(VPinballLogLevel.INFO, "SAF: delete: $relativePath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            log(VPinballLogLevel.ERROR, "SAF: delete: No external storage URI")
            return false
        }

        val docUri = buildDocumentUri(uri, relativePath)
        if (docUri == null) {
            log(VPinballLogLevel.ERROR, "SAF: delete: Path not found: $relativePath")
            return false
        }

        return try {
            val deleted = DocumentsContract.deleteDocument(activity.contentResolver, docUri)
            log(VPinballLogLevel.INFO, "SAF: delete: Result=$deleted for $relativePath")
            deleted
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: delete: Exception: ${e.message}")
            false
        }
    }

    fun safIsDirectory(relativePath: String): Boolean {
        val uri = getExternalStorageUri() ?: return false
        val docUri = buildDocumentUri(uri, relativePath) ?: return false

        return try {
            activity.contentResolver.query(
                docUri,
                arrayOf(DocumentsContract.Document.COLUMN_MIME_TYPE),
                null, null, null
            )?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val mimeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                    val mimeType = cursor.getString(mimeIndex)
                    val isDir = mimeType == DocumentsContract.Document.MIME_TYPE_DIR
                    log(VPinballLogLevel.INFO, "SAF: isDirectory: $relativePath -> $isDir (mime=$mimeType)")
                    isDir
                } else {
                    log(VPinballLogLevel.ERROR, "SAF: isDirectory: No results for $relativePath")
                    false
                }
            } ?: false
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: isDirectory: Exception: ${e.message}")
            false
        }
    }

    fun safListDirectory(relativePath: String): String {
        log(VPinballLogLevel.INFO, "SAF: listDirectory: path=$relativePath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            log(VPinballLogLevel.ERROR, "SAF: listDirectory: No external storage URI")
            return "[]"
        }

        val docUri = if (relativePath.isEmpty()) {
            val treeDocumentId = DocumentsContract.getTreeDocumentId(uri)
            DocumentsContract.buildDocumentUriUsingTree(uri, treeDocumentId)
        } else {
            buildDocumentUri(uri, relativePath)
        }

        if (docUri == null) {
            log(VPinballLogLevel.ERROR, "SAF: listDirectory: Path not found: $relativePath")
            return "[]"
        }

        val results = mutableListOf<String>()

        try {
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                uri,
                DocumentsContract.getDocumentId(docUri)
            )

            activity.contentResolver.query(
                childrenUri,
                arrayOf(
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE
                ),
                null, null, null
            )?.use { cursor ->
                val nameIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME)

                while (cursor.moveToNext()) {
                    val name = cursor.getString(nameIndex)
                    val childPath = if (relativePath.isEmpty()) name else "$relativePath/$name"
                    results.add(childPath)
                }
            }

            log(VPinballLogLevel.INFO, "SAF: listDirectory: Found ${results.size} entries")
        } catch (e: Exception) {
            log(VPinballLogLevel.ERROR, "SAF: listDirectory: Exception: ${e.message}")
        }

        return JSONArray(results).toString()
    }
}
