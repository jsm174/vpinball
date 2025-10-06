package org.vpinball.app.jni

import java.io.File
import kotlinx.serialization.Serializable
import org.vpinball.app.VPinballManager

interface VPinballDisplayText {
    val text: String
}

// VPinball Enums

enum class VPinballLogLevel(val value: Int) {
    DEBUG(0),
    INFO(1),
    WARN(2),
    ERROR(3),
}

enum class VPinballStatus(val value: Int) {
    SUCCESS(0),
    FAILURE(1),
}

enum class VPinballSettingsSection(val value: String) {
    STANDALONE("Standalone"),
    PLAYER("Player");

    companion object {
        @JvmStatic
        fun fromValue(value: String): VPinballSettingsSection =
            entries.firstOrNull { it.value == value } ?: throw IllegalArgumentException("Unknown value: $value")
    }
}

enum class VPinballViewMode(val value: Int, override val text: String) : VPinballDisplayText {
    DESKTOP_FSS(0, "Desktop & FSS"),
    CABINET(1, "Cabinet"),
    DESKTOP_NO_FSS(2, "Desktop (no FSS)");

    companion object {
        @JvmStatic fun fromInt(value: Int): VPinballViewMode = entries.firstOrNull { it.value == value } ?: DESKTOP_FSS
    }
}

enum class VPinballMaxTexDimension(val value: Int, override val text: String) : VPinballDisplayText {
    MAX_256(256, "256"),
    MAX_384(384, "384"),
    MAX_512(512, "512"),
    MAX_768(768, "768"),
    MAX_1024(1024, "1024"),
    MAX_1280(1280, "1280"),
    MAX_1536(1536, "1536"),
    MAX_1792(1792, "1792"),
    MAX_2048(2048, "2048"),
    MAX_3172(3172, "3172"),
    MAX_4096(4096, "4096"),
    UNLIMITED(0, "Unlimited");

    companion object {
        @JvmStatic fun fromInt(value: Int): VPinballMaxTexDimension = entries.firstOrNull { it.value == value } ?: UNLIMITED
    }
}

enum class VPinballExternalDMD(val value: Int, override val text: String) : VPinballDisplayText {
    NONE(0, "None"),
    DMD_SERVER(1, "DMDServer"),
    ZEDMD_WIFI(2, "ZeDMD WiFi");

    companion object {
        @JvmStatic fun fromInt(value: Int): VPinballExternalDMD = entries.firstOrNull { it.value == value } ?: NONE
    }
}

enum class VPinballGfxBackend(val value: String, override val text: String) : VPinballDisplayText {
    OPENGLES("OpenGLES", "OpenGLES"),
    VULKAN("Vulkan", "Vulkan");

    companion object {
        fun fromString(value: String): VPinballGfxBackend = entries.firstOrNull { it.value.equals(value, ignoreCase = true) } ?: OPENGLES
    }
}

// VPinball Event Enums

enum class VPinballEvent(val value: Int) {
    ARCHIVE_UNCOMPRESSING(0),
    ARCHIVE_COMPRESSING(1),
    LOADING_ITEMS(2),
    LOADING_SOUNDS(3),
    LOADING_IMAGES(4),
    LOADING_FONTS(5),
    LOADING_COLLECTIONS(6),
    PRERENDERING(7),
    PLAYER_STARTED(8),
    RUMBLE(9),
    SCRIPT_ERROR(10),
    PLAYER_CLOSED(11),
    WEB_SERVER(12),
    TABLE_LIST_UPDATED(13);

    val text: String?
        get() =
            when (this) {
                ARCHIVE_UNCOMPRESSING -> "Uncompressing"
                ARCHIVE_COMPRESSING -> "Compressing"
                LOADING_ITEMS -> "Loading Items"
                LOADING_SOUNDS -> "Loading Sounds"
                LOADING_IMAGES -> "Loading Images"
                LOADING_FONTS -> "Loading Fonts"
                LOADING_COLLECTIONS -> "Loading Collections"
                PRERENDERING -> "Prerendering Static Parts"
                else -> null
            }
}

enum class VPinballScriptErrorType(val value: Int) {
    COMPILE(0),
    RUNTIME(1);

    val text: String
        get() =
            when (this) {
                COMPILE -> "Compile error"
                RUNTIME -> "Runtime error"
            }

    companion object {
        @JvmStatic
        fun fromInt(value: Int): VPinballScriptErrorType {
            return entries.firstOrNull { it.value == value } ?: throw IllegalArgumentException("Unknown value: $value")
        }
    }
}

// VPinball Touch Areas

data class VPinballTouchArea(val left: Float, val top: Float, val right: Float, val bottom: Float, val label: String)

val VPinballTouchAreas: List<List<VPinballTouchArea>> =
    listOf(
        listOf(VPinballTouchArea(left = 50f, top = 0f, right = 100f, bottom = 10f, label = "Menu")),
        listOf(VPinballTouchArea(left = 0f, top = 0f, right = 50f, bottom = 10f, label = "Coin")),
        listOf(
            VPinballTouchArea(left = 0f, top = 10f, right = 50f, bottom = 30f, label = "Left\nMagna-Save"),
            VPinballTouchArea(left = 50f, top = 10f, right = 100f, bottom = 30f, label = "Right\nMagna-Save"),
        ),
        listOf(
            VPinballTouchArea(left = 0f, top = 30f, right = 50f, bottom = 60f, label = "Left\nNudge"),
            VPinballTouchArea(left = 50f, top = 30f, right = 100f, bottom = 60f, label = "Right\nNudge"),
            VPinballTouchArea(left = 30f, top = 60f, right = 70f, bottom = 100f, label = "Center\nNudge"),
        ),
        listOf(
            VPinballTouchArea(left = 0f, top = 60f, right = 30f, bottom = 90f, label = "Left\nFlipper"),
            VPinballTouchArea(left = 70f, top = 60f, right = 100f, bottom = 90f, label = "Right\nFlipper"),
        ),
        listOf(VPinballTouchArea(left = 70f, top = 90f, right = 100f, bottom = 100f, label = "Plunger")),
        listOf(VPinballTouchArea(left = 0f, top = 90f, right = 30f, bottom = 100f, label = "Start")),
    )

// VPinball Callbacks (hybrid approach: JSON)

fun interface VPinballEventCallback {
    fun onEvent(event: Int, jsonData: String?)
}

// VPinball Objects

@Serializable data class VPinballProgressData(val progress: Int)

@Serializable data class VPinballRumbleData(val lowFrequencyRumble: Int, val highFrequencyRumble: Int, val durationMs: Int)

@Serializable data class VPinballScriptErrorData(val error: Int, val line: Int, val position: Int, val description: String)

@Serializable
data class Table(val uuid: String, val name: String, val path: String, val image: String, val createdAt: Long, val modifiedAt: Long) {
    val fileName: String
        get() = File(path).name

    val fullPath: String
        get() {
            val tablesPath = org.vpinball.app.VPinballManager.getTablesPath()
            return if (org.vpinball.app.VPinballManager.isTablesPathSAF()) {
                // Build content URI: tree URI + / + relative path
                "$tablesPath/$path"
            } else {
                File(tablesPath, path).absolutePath
            }
        }

    val baseURL: File
        get() =
            if (org.vpinball.app.VPinballManager.isTablesPathSAF()) {
                File("")
            } else {
                File(fullPath).parentFile ?: File("")
            }

    val basePath: String
        get() =
            if (org.vpinball.app.VPinballManager.isTablesPathSAF()) {
                fullPath.substringBeforeLast('/')
            } else {
                baseURL.absolutePath
            }

    val fullURL: File
        get() = File(fullPath)

    val scriptURL: File
        get() = File(fullPath.substringBeforeLast('.') + ".vbs")

    val scriptPath: String
        get() =
            if (org.vpinball.app.VPinballManager.isTablesPathSAF()) {
                fullPath.substringBeforeLast('.') + ".vbs"
            } else {
                scriptURL.absolutePath
            }

    val iniURL: File
        get() = File(fullPath.substringBeforeLast('.') + ".ini")

    val iniPath: String
        get() =
            if (org.vpinball.app.VPinballManager.isTablesPathSAF()) {
                fullPath.substringBeforeLast('.') + ".ini"
            } else {
                iniURL.absolutePath
            }

    val imagePath: String
        get() {
            val tablesPath = org.vpinball.app.VPinballManager.getTablesPath()
            return if (org.vpinball.app.VPinballManager.isTablesPathSAF()) {
                // SAF: Construct content:// URI
                "$tablesPath/$image"
            } else {
                // Regular filesystem
                File(tablesPath, image).absolutePath
            }
        }

    fun exists(): Boolean = VPinballManager.fileExists(fullPath)

    fun hasScriptFile(): Boolean = VPinballManager.fileExists(scriptPath)

    fun hasIniFile(): Boolean = VPinballManager.fileExists(iniPath)

    fun hasImageFile(): Boolean = image.isNotEmpty() && VPinballManager.fileExists(imagePath)
}

@Serializable data class VPXTablesResponse(val tableCount: Int, val tables: List<Table>)

@Serializable data class VPinballWebServerData(val url: String)
