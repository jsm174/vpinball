package org.vpinball.app.util

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import java.io.File
import java.io.FileOutputStream
import org.vpinball.app.VPinballManager
import org.vpinball.app.jni.Table

private const val MAX_IMAGE_QUALITY = 80

val Table.baseFilename: String
    get() = fileName.substringBeforeLast('.', fileName)

val Table.imageFile: File
    get() = File(artworkPath)

val Table.tableFile: File
    get() = File(fullPath)

fun Table.resetIni() {
    File(iniPath).delete()
}

fun Table.deleteFiles() {
    File(basePath).deleteRecursively()
}

fun Table.loadImage(): ImageBitmap? {
    return if (hasImageFile()) {
        BitmapFactory.decodeFile(artworkPath)?.asImageBitmap()
    } else {
        null
    }
}

fun Table.updateImage(bitmap: Bitmap) {
    val artworkFileName = "${baseFilename}.jpg"
    val artworkFile = File(File(basePath), artworkFileName)

    FileOutputStream(artworkFile).use { outputStream -> bitmap.compress(Bitmap.CompressFormat.JPEG, MAX_IMAGE_QUALITY, outputStream) }

    val relativeArtworkPath =
        if (path.isNotEmpty()) {
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

fun Table.resetImage() {
    VPinballManager.vpinballJNI.VPinballSetTableArtwork(uuid, "")
}
