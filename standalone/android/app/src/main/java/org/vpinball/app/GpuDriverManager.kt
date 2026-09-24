package org.vpinball.app

import android.content.Context
import android.net.Uri
import android.os.Build
import java.io.File
import java.util.zip.ZipFile
import org.json.JSONObject
import org.vpinball.app.jni.VPinballDisplayText
import org.vpinball.app.jni.VPinballLogLevel
import org.vpinball.app.jni.VPinballSettingsSection.PLAYER

data class GpuDriver(
    val id: String,
    val name: String,
    val description: String,
    val author: String,
    val vendor: String,
    val driverVersion: String,
    val minApi: Int,
    val libraryName: String,
    val dir: File,
)

sealed class GpuDriverOption : VPinballDisplayText {
    data object System : GpuDriverOption() {
        override val text: String = "System"
    }

    data class Custom(val driver: GpuDriver) : GpuDriverOption() {
        override val text: String
            get() = driver.name
    }
}

data class GpuDriverInfo(
    val custom: Boolean,
    val library: String,
    val deviceName: String?,
    val driverName: String?,
    val driverInfo: String?,
    val apiVersion: String?,
    val error: String?,
) {
    val summary: String
        get() =
            when {
                error != null -> "Vulkan unavailable: $error"
                driverName != null ->
                    listOfNotNull(driverName, driverInfo?.takeIf { it.isNotEmpty() }, apiVersion?.let { "(Vulkan $it)" }).joinToString(" ")
                deviceName != null -> listOfNotNull(deviceName, apiVersion?.let { "(Vulkan $it)" }).joinToString(" ")
                else -> "Unknown"
            }
}

sealed class GpuDriverInstallResult {
    data class Success(val driver: GpuDriver) : GpuDriverInstallResult()

    data class Error(val message: String) : GpuDriverInstallResult()
}

object GpuDriverManager {
    private const val SETTING_KEY = "GpuDriver"
    private const val ENVIRONMENT_SETTING_KEY = "GpuDriverEnv"
    private const val META_FILE = "meta.json"
    private const val DRIVERS_DIR = "gpu_drivers"
    private const val PENDING_MARKER = "gpu_driver_pending"
    private const val MAX_META_SIZE = 512L * 1024L
    private const val MAX_ID_LENGTH = 64

    private lateinit var context: Context

    val isSupported: Boolean by lazy { !BuildConfig.IS_QUEST && Build.SUPPORTED_64_BIT_ABIS.contains("arm64-v8a") && File("/dev/kgsl-3d0").exists() }

    fun initialize(context: Context) {
        this.context = context.applicationContext
    }

    fun selectedDriverId(): String = VPinballManager.loadValue(PLAYER, SETTING_KEY, "")

    fun environment(): String = VPinballManager.loadValue(PLAYER, ENVIRONMENT_SETTING_KEY, "")

    fun setEnvironment(value: String): Boolean {
        VPinballManager.saveValue(PLAYER, ENVIRONMENT_SETTING_KEY, value.trim())
        return loadDriver(selectedDriver())
    }

    fun selectedDriver(): GpuDriver? = selectedDriverId().takeIf { it.isNotEmpty() }?.let { parseDriver(File(driversDir(), it)) }

    fun installedDrivers(): List<GpuDriver> =
        driversDir().listFiles()?.filter { it.isDirectory }?.mapNotNull { parseDriver(it) }?.sortedBy { it.name.lowercase() } ?: emptyList()

    fun applySelectedDriver() {
        if (!isSupported) {
            return
        }

        if (pendingMarker().exists()) {
            pendingMarker().delete()
            if (selectedDriverId().isNotEmpty()) {
                VPinballManager.log(
                    VPinballLogLevel.WARN,
                    "Previous session with a custom GPU driver did not start playing, reverting to system driver",
                )
                VPinballManager.saveValue(PLAYER, SETTING_KEY, "")
            }
        }

        val driver = selectedDriver()
        if (driver == null && selectedDriverId().isNotEmpty()) {
            VPinballManager.saveValue(PLAYER, SETTING_KEY, "")
        }

        loadDriver(driver)
    }

    fun select(driver: GpuDriver?): Boolean {
        VPinballManager.saveValue(PLAYER, SETTING_KEY, driver?.id ?: "")
        return loadDriver(driver)
    }

    fun onPlayerStarting() {
        if (isSupported && selectedDriverId().isNotEmpty()) {
            runCatching { pendingMarker().createNewFile() }
        }
    }

    fun onPlayerStarted() {
        runCatching { pendingMarker().delete() }
    }

    fun driverInfo(): GpuDriverInfo {
        val json = JSONObject(VPinballManager.vpinballJNI.VPinballGetGpuDriverInfo())
        return GpuDriverInfo(
            custom = json.optBoolean("custom", false),
            library = json.optString("library", ""),
            deviceName = json.optString("deviceName").takeIf { json.has("deviceName") },
            driverName = json.optString("driverName").takeIf { json.has("driverName") },
            driverInfo = json.optString("driverInfo").takeIf { json.has("driverInfo") },
            apiVersion = json.optString("apiVersion").takeIf { json.has("apiVersion") },
            error = json.optString("error").takeIf { json.has("error") },
        )
    }

    fun install(uri: Uri): GpuDriverInstallResult {
        val stamp = System.currentTimeMillis()
        val tempZip = File(context.cacheDir, "gpu_driver_$stamp.zip")
        val tempDir = File(context.cacheDir, "gpu_driver_$stamp")

        try {
            context.contentResolver.openInputStream(uri)?.use { input -> tempZip.outputStream().use { output -> input.copyTo(output) } }
                ?: return GpuDriverInstallResult.Error("Unable to open the selected file.")

            return ZipFile(tempZip).use { zip -> extract(zip, tempDir) }
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to install GPU driver: ${e.message}")
            return GpuDriverInstallResult.Error("The selected file is not a valid driver package.")
        } finally {
            tempZip.delete()
            tempDir.deleteRecursively()
        }
    }

    fun uninstall(driver: GpuDriver) {
        if (selectedDriverId() == driver.id) {
            select(null)
        }
        driver.dir.deleteRecursively()
    }

    private fun extract(zip: ZipFile, tempDir: File): GpuDriverInstallResult {
        val entries = zip.entries().toList().filter { !it.isDirectory }
        val metaEntry =
            entries.firstOrNull { it.name.substringAfterLast('/') == META_FILE }
                ?: return GpuDriverInstallResult.Error("The selected file does not contain a $META_FILE.")

        if (metaEntry.size > MAX_META_SIZE) {
            return GpuDriverInstallResult.Error("The driver package metadata is invalid.")
        }

        val meta = JSONObject(zip.getInputStream(metaEntry).bufferedReader().readText())
        val name = meta.optString("name").trim()
        val libraryName = meta.optString("libraryName").trim()
        if (name.isEmpty() || libraryName.isEmpty() || libraryName.contains('/')) {
            return GpuDriverInstallResult.Error("The driver package metadata is invalid.")
        }

        val minApi = meta.optInt("minApi", 0)
        if (minApi > Build.VERSION.SDK_INT) {
            return GpuDriverInstallResult.Error("This driver requires Android API level $minApi.")
        }

        val prefix = metaEntry.name.substringBeforeLast('/', "")
        tempDir.mkdirs()

        for (entry in entries) {
            val relative = if (prefix.isEmpty()) entry.name else entry.name.removePrefix("$prefix/")
            if ((prefix.isNotEmpty() && relative == entry.name) || relative.contains('/')) {
                continue
            }

            val outFile = File(tempDir, relative)
            if (!outFile.canonicalPath.startsWith(tempDir.canonicalPath + File.separator)) {
                return GpuDriverInstallResult.Error("The driver package contains an invalid entry.")
            }

            zip.getInputStream(entry).use { input -> outFile.outputStream().use { output -> input.copyTo(output) } }
        }

        if (!File(tempDir, libraryName).isFile) {
            return GpuDriverInstallResult.Error("The driver library $libraryName was not found in the package.")
        }

        val target = File(driversDir(), driverId(name, meta.optString("packageVersion")))
        if (target.exists()) {
            target.deleteRecursively()
        }

        if (!tempDir.renameTo(target)) {
            tempDir.copyRecursively(target, overwrite = true)
        }

        val driver = parseDriver(target) ?: return GpuDriverInstallResult.Error("The driver package could not be installed.")
        return GpuDriverInstallResult.Success(driver)
    }

    private fun loadDriver(driver: GpuDriver?): Boolean {
        val hookLibDir = context.applicationInfo.nativeLibraryDir + File.separator
        val driverDir = driver?.let { it.dir.absolutePath + File.separator } ?: ""
        val loaded = VPinballManager.vpinballJNI.VPinballInitGpuDriver(hookLibDir, driverDir, driver?.libraryName ?: "", environment())
        if (!loaded) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Unable to load GPU driver: ${driver?.name}")
        }
        return loaded
    }

    private fun parseDriver(dir: File): GpuDriver? {
        val metaFile = File(dir, META_FILE)
        if (!metaFile.isFile || metaFile.length() > MAX_META_SIZE) {
            return null
        }

        return try {
            val meta = JSONObject(metaFile.readText())
            val libraryName = meta.optString("libraryName").trim()
            if (libraryName.isEmpty() || !File(dir, libraryName).isFile) {
                return null
            }

            GpuDriver(
                id = dir.name,
                name = meta.optString("name").trim().ifEmpty { dir.name },
                description = meta.optString("description"),
                author = meta.optString("author"),
                vendor = meta.optString("vendor"),
                driverVersion = meta.optString("driverVersion"),
                minApi = meta.optInt("minApi", 0),
                libraryName = libraryName,
                dir = dir,
            )
        } catch (_: Exception) {
            null
        }
    }

    private fun driverId(name: String, packageVersion: String): String {
        val base = if (packageVersion.isBlank()) name else "$name-v$packageVersion"
        return base.replace(Regex("[^A-Za-z0-9._ -]"), "_").trim().take(MAX_ID_LENGTH).ifEmpty { "driver" }
    }

    private fun driversDir(): File = File(context.filesDir, DRIVERS_DIR).apply { mkdirs() }

    private fun pendingMarker(): File = File(context.filesDir, PENDING_MARKER)
}
