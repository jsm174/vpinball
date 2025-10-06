package org.vpinball.app

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Size
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import org.koin.core.component.KoinComponent
import org.vpinball.app.jni.*
import org.vpinball.app.jni.VPinballSettingsSection.STANDALONE
import org.vpinball.app.ui.screens.landing.LandingScreenViewModel
import org.vpinball.app.util.FileUtils

object VPinballManager : KoinComponent {
    val vpinballJNI: VPinballJNI = VPinballJNI()

    internal lateinit var activity: VPinballActivity
    private lateinit var filesDir: File
    private lateinit var cacheDir: File
    private lateinit var displaySize: Size
    private lateinit var vibrator: Vibrator
    lateinit var safFileSystem: SAFFileSystem

    private var activeTable: Table? = null
    private var error: String? = null

    fun initialize(activity: VPinballActivity) {
        this.activity = activity
        this.safFileSystem = SAFFileSystem()

        filesDir = activity.filesDir
        cacheDir = activity.cacheDir

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
    }

    fun startup() {
        runCatching { FileUtils.copyAssets(activity.assets, "", activity.filesDir) }

        vpinballJNI.VPinballInit { value, jsonData ->
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
                    val progressData =
                        jsonData?.let { jsonStr ->
                            try {
                                Json.decodeFromString<VPinballProgressData>(jsonStr)
                            } catch (e: Exception) {
                                log(VPinballLogLevel.WARN, "Failed to parse progress data JSON: $jsonStr - ${e.message}")
                                null
                            }
                        }
                    log(VPinballLogLevel.INFO, "event=${event.name}, data=${progressData}")
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
                    }
                }
                VPinballEvent.RUMBLE -> {
                    if (loadValue(STANDALONE, "Haptics", true)) {
                        val rumbleData =
                            jsonData?.let { jsonStr ->
                                try {
                                    Json.decodeFromString<VPinballRumbleData>(jsonStr)
                                } catch (e: Exception) {
                                    log(VPinballLogLevel.WARN, "Failed to parse rumble data JSON: $jsonStr - ${e.message}")
                                    null
                                }
                            }
                        rumbleData?.let { rumble(it) }
                    }
                }
                VPinballEvent.SCRIPT_ERROR -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}, error=$error, jsonData=$jsonData")
                    if (error == null) {
                        val scriptErrorData =
                            jsonData?.let { jsonStr ->
                                try {
                                    Json.decodeFromString<VPinballScriptErrorData>(jsonStr)
                                } catch (e: Exception) {
                                    log(VPinballLogLevel.WARN, "Failed to parse script error data JSON: $jsonStr - ${e.message}")
                                    null
                                }
                            }
                        error =
                            scriptErrorData?.let {
                                val errorType = VPinballScriptErrorType.fromInt(it.error)
                                "${errorType.text} on line ${it.line}, position ${it.position}:\n\n${it.description}"
                            } ?: "Script error."
                        log(VPinballLogLevel.INFO, "SCRIPT_ERROR: set error to: $error")
                    } else {
                        log(VPinballLogLevel.INFO, "SCRIPT_ERROR: error already set, ignoring")
                    }
                }
                VPinballEvent.PLAYER_CLOSED -> {
                    log(VPinballLogLevel.INFO, "event=${event.name}")
                    activeTable = null
                    CoroutineScope(Dispatchers.Main).launch {
                        viewModel.playing(false)
                        viewModel.stopped()
                        error?.let { error ->
                            log(VPinballLogLevel.INFO, "STOPPED: showing error: $error")
                            delay(500)
                            showError(error)
                        } ?: log(VPinballLogLevel.INFO, "STOPPED: no error to show")
                    }
                }
                VPinballEvent.WEB_SERVER -> {
                    val webServerData =
                        jsonData?.let { jsonStr ->
                            try {
                                Json.decodeFromString<VPinballWebServerData>(jsonStr)
                            } catch (e: Exception) {
                                null
                            }
                        }
                    log(VPinballLogLevel.INFO, "event=${event.name}, data=${webServerData}")
                    webServerData?.let { CoroutineScope(Dispatchers.Main).launch { viewModel.webServerURL = webServerData.url } }
                }
                VPinballEvent.TABLE_LIST_UPDATED -> {
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
                        LandingScreenViewModel.triggerRefresh()

                        focusUuid?.let { uuid ->
                            delay(100)
                            LandingScreenViewModel.triggerScrollToTable(uuid)
                        }
                    }
                }
                else -> {
                    log(VPinballLogLevel.WARN, "event=${event}")
                }
            }
        }
    }

    fun getDisplaySize(): Size {
        return displaySize
    }

    fun getFilesDir(): File {
        return filesDir
    }

    fun getCacheDir(): File {
        return cacheDir
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

    fun getVPinballCacheDir(): File = cacheDir

    fun play(table: Table) {
        if (activeTable != null) return
        activeTable = table
        error = null
        CoroutineScope(Dispatchers.IO).launch {
            if (loadValue(STANDALONE, "ResetLogOnPlay", true)) {
                vpinballJNI.VPinballResetLog()
            }
            withContext(Dispatchers.Main) { activity.viewModel.loading(true, table) }
            if (vpinballJNI.VPinballLoadTable(table.uuid) == VPinballStatus.SUCCESS.value) {
                vpinballJNI.VPinballPlay()
            } else {
                delay(500)
                withContext(Dispatchers.Main) {
                    activity.viewModel.stopped()
                    showError("Unable to load table.")
                    activeTable = null
                }
            }
        }
    }

    fun stop() {
        vpinballJNI.VPinballStop()
    }

    fun fileExists(path: String): Boolean {
        return vpinballJNI.VPinballFileExists(path)
    }

    fun stageFile(path: String): String? {
        return vpinballJNI.VPinballStageFile(path)
    }

    fun showError(message: String) {
        CoroutineScope(Dispatchers.Main).launch {
            delay(250)
            activity.viewModel.setError(message)
        }
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

    fun RefreshTables() {
        log(VPinballLogLevel.INFO, "VPinballManager: RefreshTables() called")
        CoroutineScope(Dispatchers.IO).launch {
            val status = vpinballJNI.VPinballRefreshTables()
            if (status == VPinballStatus.SUCCESS.value) {
                log(VPinballLogLevel.INFO, "VPinballManager: Successfully reloaded tables path")
            } else {
                log(VPinballLogLevel.ERROR, "VPinballManager: Failed to reload tables path - status: $status")
            }
        }
    }
}
