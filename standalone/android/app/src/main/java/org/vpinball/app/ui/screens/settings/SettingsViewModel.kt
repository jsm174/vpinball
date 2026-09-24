package org.vpinball.app.ui.screens.settings

import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.vpinball.app.GpuDriverInstallResult
import org.vpinball.app.GpuDriverManager
import org.vpinball.app.GpuDriverOption
import org.vpinball.app.SAFFileSystem
import org.vpinball.app.VPinballManager
import org.vpinball.app.jni.VPinballExternalDMD
import org.vpinball.app.jni.VPinballGfxBackend
import org.vpinball.app.jni.VPinballMaxTexDimension
import org.vpinball.app.jni.VPinballPath
import org.vpinball.app.jni.VPinballSettingsSection.PLAYER
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_DMDUTIL
import org.vpinball.app.jni.VPinballSettingsSection.STANDALONE
import org.vpinball.app.jni.VPinballStorageMode
import org.vpinball.app.ui.screens.landing.LandingScreenViewModel

class SettingsViewModel : ViewModel() {
    // General

    var renderingModeOverride by mutableStateOf(false)
        private set

    var gfxBackend by mutableStateOf(VPinballGfxBackend.OPENGLES)
        private set

    val gpuDriverSupported: Boolean
        get() = GpuDriverManager.isSupported

    var gpuDriverOptions by mutableStateOf<List<GpuDriverOption>>(listOf(GpuDriverOption.System))
        private set

    var gpuDriverOption by mutableStateOf<GpuDriverOption>(GpuDriverOption.System)
        private set

    var gpuDriverInfo by mutableStateOf<String?>(null)
        private set

    var gpuDriverEnvironment by mutableStateOf("")
        private set

    var gpuDriverError by mutableStateOf<String?>(null)
        private set

    var storageMode by mutableStateOf(VPinballStorageMode.INTERNAL)
        private set

    var currentTablesPath by mutableStateOf("")
        private set

    // Display

    // Performance

    var maxTexDimension by mutableStateOf(VPinballMaxTexDimension.MAX_768)
        private set

    // External DMD

    var externalDMD by mutableStateOf(VPinballExternalDMD.NONE)
        private set

    var dmdServerAddr by mutableStateOf("0.0.0.0")
        private set

    var dmdServerPort by mutableIntStateOf(6789)
        private set

    var zedmdWiFiAddr by mutableStateOf("zedmd-wifi.local")
        private set

    // Web Server

    var webServer by mutableStateOf(false)
        private set

    var webServerPort by mutableIntStateOf(0)
        private set

    var needsTableReload by mutableStateOf(false)
        private set

    fun loadSettings() {
        // General

        renderingModeOverride = (VPinballManager.loadValue(STANDALONE, "RenderingModeOverride", -1) == 2)
        gfxBackend = VPinballGfxBackend.fromString(VPinballManager.loadValue(PLAYER, "GfxBackend", VPinballGfxBackend.OPENGLES.value))

        loadGpuDrivers()

        val savedSAFPath = VPinballManager.loadValue(STANDALONE, "SAFPath", "")
        storageMode = VPinballStorageMode.fromSAFPath(savedSAFPath)

        currentTablesPath =
            when (storageMode) {
                VPinballStorageMode.INTERNAL -> VPinballManager.getPath(VPinballPath.TABLES)
                VPinballStorageMode.CUSTOM -> {
                    if (SAFFileSystem.isUsingSAF()) {
                        val displayPath = SAFFileSystem.getExternalStorageDisplayPath()
                        if (displayPath.isNotEmpty()) displayPath else savedSAFPath
                    } else {
                        savedSAFPath
                    }
                }
            }

        // Performance

        maxTexDimension =
            VPinballMaxTexDimension.fromInt(VPinballManager.loadValue(PLAYER, "MaxTexDimension", VPinballMaxTexDimension.MAX_1024.value))

        // External DMD

        externalDMD =
            when {
                VPinballManager.loadValue(PLUGIN_DMDUTIL, "DMDServer", false) -> VPinballExternalDMD.DMD_SERVER

                VPinballManager.loadValue(PLUGIN_DMDUTIL, "ZeDMDWiFiEnabled", false) -> VPinballExternalDMD.ZEDMD_WIFI

                else -> VPinballExternalDMD.NONE
            }

        dmdServerAddr = VPinballManager.loadValue(PLUGIN_DMDUTIL, "DMDServerAddr", "0.0.0.0")
        dmdServerPort = VPinballManager.loadValue(PLUGIN_DMDUTIL, "DMDServerPort", 6789)
        zedmdWiFiAddr = VPinballManager.loadValue(PLUGIN_DMDUTIL, "ZeDMDWiFiAddr", "zedmd-wifi.local")

        // Web Server

        webServer = VPinballManager.loadValue(STANDALONE, "WebServer", false)
        webServerPort = VPinballManager.loadValue(STANDALONE, "WebServerPort", 2112)
    }

    // General

    fun handleRenderingModeOverride(value: Boolean) {
        renderingModeOverride = value
        VPinballManager.saveValue(STANDALONE, "RenderingModeOverride", if (renderingModeOverride) 2 else -1)
    }

    fun handleGfxBackend(value: VPinballGfxBackend) {
        gfxBackend = value
        VPinballManager.saveValue(PLAYER, "GfxBackend", value.value)
    }

    fun handleGpuDriver(option: GpuDriverOption) {
        val driver = (option as? GpuDriverOption.Custom)?.driver
        if (!GpuDriverManager.select(driver)) {
            GpuDriverManager.select(null)
            gpuDriverError = "Unable to load the selected GPU driver. The system driver will be used instead."
        }
        loadGpuDrivers()
    }

    fun handleInstallGpuDriver(uri: Uri) {
        CoroutineScope(Dispatchers.IO).launch {
            val result = GpuDriverManager.install(uri)
            withContext(Dispatchers.Main) {
                when (result) {
                    is GpuDriverInstallResult.Success -> handleGpuDriver(GpuDriverOption.Custom(result.driver))
                    is GpuDriverInstallResult.Error -> gpuDriverError = result.message
                }
            }
        }
    }

    fun handleGpuDriverEnvironment(value: String) {
        if (!GpuDriverManager.setEnvironment(value)) {
            GpuDriverManager.select(null)
            gpuDriverError = "Unable to reload the GPU driver with the new environment. The system driver will be used instead."
        }
        loadGpuDrivers()
    }

    fun handleUninstallGpuDriver() {
        (gpuDriverOption as? GpuDriverOption.Custom)?.let { GpuDriverManager.uninstall(it.driver) }
        loadGpuDrivers()
    }

    fun clearGpuDriverError() {
        gpuDriverError = null
    }

    private fun loadGpuDrivers() {
        if (!gpuDriverSupported) {
            return
        }

        val drivers = GpuDriverManager.installedDrivers()
        val selectedId = GpuDriverManager.selectedDriverId()
        gpuDriverOptions = listOf(GpuDriverOption.System) + drivers.map { GpuDriverOption.Custom(it) }
        gpuDriverOption = drivers.firstOrNull { it.id == selectedId }?.let { GpuDriverOption.Custom(it) } ?: GpuDriverOption.System
        gpuDriverEnvironment = GpuDriverManager.environment()

        gpuDriverInfo = null
        CoroutineScope(Dispatchers.IO).launch {
            val info = runCatching { GpuDriverManager.driverInfo().summary }.getOrNull()
            withContext(Dispatchers.Main) { gpuDriverInfo = info }
        }
    }

    fun handleStorageMode(mode: VPinballStorageMode) {
        when (mode) {
            VPinballStorageMode.INTERNAL -> {
                SAFFileSystem.clearExternalStorageUri()
                storageMode = VPinballStorageMode.INTERNAL
                currentTablesPath = VPinballManager.getPath(VPinballPath.TABLES)
                needsTableReload = true
            }
            VPinballStorageMode.CUSTOM -> {
                storageMode = VPinballStorageMode.CUSTOM
            }
        }
    }

    fun handleCustomStorageUri(uri: android.net.Uri) {
        SAFFileSystem.setExternalStorageUri(uri)
        storageMode = VPinballStorageMode.CUSTOM

        val displayPath = SAFFileSystem.getExternalStorageDisplayPath()
        currentTablesPath = if (displayPath.isNotEmpty()) displayPath else VPinballManager.getPath(VPinballPath.TABLES)

        needsTableReload = true
    }

    fun triggerTableReloadIfNeeded() {
        if (needsTableReload) {
            needsTableReload = false
            CoroutineScope(Dispatchers.Main).launch { LandingScreenViewModel.triggerRefresh() }
        }
    }

    // Display

    // Performance

    fun handleMaxTexDimension(value: VPinballMaxTexDimension) {
        maxTexDimension = value
        VPinballManager.saveValue(PLAYER, "MaxTexDimension", value.value)
    }

    // External DMD

    fun handleExternalDMD(value: VPinballExternalDMD) {
        externalDMD = value
        VPinballManager.saveValue(PLUGIN_DMDUTIL, "DMDServer", externalDMD == VPinballExternalDMD.DMD_SERVER)
        VPinballManager.saveValue(PLUGIN_DMDUTIL, "ZeDMDWiFiEnabled", externalDMD == VPinballExternalDMD.ZEDMD_WIFI)
        VPinballManager.saveValue(PLUGIN_DMDUTIL, "Enable", externalDMD != VPinballExternalDMD.NONE)
    }

    fun handleDMDServerAddr(value: String) {
        dmdServerAddr = value
        VPinballManager.saveValue(PLUGIN_DMDUTIL, "DMDServerAddr", dmdServerAddr)
    }

    fun handleDMDServerPort(value: Int) {
        dmdServerPort = value
        VPinballManager.saveValue(PLUGIN_DMDUTIL, "DMDServerPort", dmdServerPort)
    }

    fun handleZeDMDWiFiAddr(value: String) {
        zedmdWiFiAddr = value
        VPinballManager.saveValue(PLUGIN_DMDUTIL, "ZeDMDWiFiAddr", zedmdWiFiAddr)
    }

    // Web Server

    fun handleWebServer(value: Boolean) {
        webServer = value
        VPinballManager.saveValue(STANDALONE, "WebServer", webServer)
        VPinballManager.updateWebServer()
    }

    fun handleWebServerPort(value: Int) {
        webServerPort = value
        VPinballManager.saveValue(STANDALONE, "WebServerPort", webServerPort)
        VPinballManager.updateWebServer()
    }

    // Reset

    fun handleResetAllSettings() {
        VPinballManager.resetIni()
    }
}
