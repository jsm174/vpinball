package org.vpinball.app.util

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import java.io.File
import java.io.FileOutputStream
import org.vpinball.app.jni.VPinballVPXTable
import org.vpinball.app.VPinballManager

private const val MAX_IMAGE_QUALITY = 80

val VPinballVPXTable.basePath: File
    get() = File(fullPath).parentFile ?: File(fullPath)

val VPinballVPXTable.tableFile: File
    get() = File(fullPath)

val VPinballVPXTable.baseFilename: String
    get() = fileName.substringBeforeLast('.', fileName)

val VPinballVPXTable.imageFile: File
    get() = if (artwork.isNotEmpty()) {
        File(VPinballManager.getTablesPath(), artwork)
    } else {
        File(basePath, "${baseFilename}.jpg")
    }

val VPinballVPXTable.iniFile: File
    get() = File(basePath, "${baseFilename}.ini")

val VPinballVPXTable.scriptFile: File
    get() = File(basePath, "${baseFilename}.vbs")

fun VPinballVPXTable.hasImageFile(): Boolean {
    return imageFile.exists()
}

fun VPinballVPXTable.hasIniFile(): Boolean {
    return iniFile.exists()
}

fun VPinballVPXTable.resetIni() {
    iniFile.delete()
}

fun VPinballVPXTable.hasScriptFile(): Boolean {
    return scriptFile.exists()
}

fun VPinballVPXTable.deleteFiles() {
    basePath.deleteRecursively()
}

fun VPinballVPXTable.loadImage(): ImageBitmap? {
    return if (hasImageFile()) {
        BitmapFactory.decodeFile(imageFile.absolutePath)?.asImageBitmap()
    } else {
        null
    }
}

fun VPinballVPXTable.updateImage(bitmap: Bitmap) {
    val artworkFileName = "${baseFilename}.jpg"
    val artworkFile = File(basePath, artworkFileName)
    
    FileOutputStream(artworkFile).use { outputStream -> 
        bitmap.compress(Bitmap.CompressFormat.JPEG, MAX_IMAGE_QUALITY, outputStream) 
    }
    
    val relativeArtworkPath = if (path.isNotEmpty()) {
        val parentPath = File(path).parent
        if (parentPath != null && parentPath.isNotEmpty()) {
            "$parentPath/$artworkFileName"
        } else {
            artworkFileName
        }
    } else {
        artworkFileName
    }
    
    VPinballManager.vpinballJNI.VPinballSetTableArtwork(uuid, relativeArtworkPath)
}

fun VPinballVPXTable.resetImage() {
    VPinballManager.vpinballJNI.VPinballSetTableArtwork(uuid, "")
}