package org.vpinball.app.util

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import java.io.File
import java.io.FileOutputStream
import org.vpinball.app.VPinballManager
import org.vpinball.app.jni.Table
import org.vpinball.app.jni.VPinballLogLevel

private const val MAX_IMAGE_QUALITY = 80

val Table.baseFilename: String
    get() = fileName.substringBeforeLast('.', fileName)

val Table.imageFile: File
    get() = File(imagePath)

val Table.tableFile: File
    get() = File(fullPath)

fun Table.resetIni() {
    VPinballManager.vpinballJNI.VPinballDeleteFile(iniPath)
}

fun Table.deleteFiles() {
    File(basePath).deleteRecursively()
}

fun Table.loadImage(): ImageBitmap? {
    if (!hasImageFile()) return null

    try {
        if (VPinballManager.isTablesPathSAF()) {
            // SAF: Use InputStream
            val inputStream = VPinballManager.safFileSystem.openInputStream(image) ?: return null
            return inputStream.use { stream -> BitmapFactory.decodeStream(stream)?.asImageBitmap() }
        } else {
            // Regular filesystem
            return BitmapFactory.decodeFile(imagePath)?.asImageBitmap()
        }
    } catch (e: Exception) {
        VPinballManager.log(VPinballLogLevel.ERROR, "Failed to load image: ${e.message}")
        return null
    }
}

fun Table.updateImage(bitmap: Bitmap) {
    val imageFileName = "${baseFilename}.jpg"

    val finalRelativePath: String

    if (VPinballManager.isTablesPathSAF()) {
        // SAF: Save to cache first, then copy to SAF
        val cacheDir = VPinballManager.getCacheDir()
        val tempFile = File(cacheDir, "temp_image_${uuid}.jpg")

        try {
            // Save bitmap to temp file
            FileOutputStream(tempFile).use { outputStream -> bitmap.compress(Bitmap.CompressFormat.JPEG, MAX_IMAGE_QUALITY, outputStream) }

            // Build SAF destination path
            val parentPath = if (path.isNotEmpty()) File(path).parent else null
            val safRelativePath =
                if (parentPath != null && parentPath.isNotEmpty()) {
                    "$parentPath/$imageFileName"
                } else {
                    imageFileName
                }

            val tablesPath = VPinballManager.getTablesPath()
            val safImagePath = "$tablesPath/$safRelativePath"

            // Copy from cache to SAF using FileSystem
            val copySuccess = VPinballManager.vpinballJNI.VPinballCopyFile(tempFile.absolutePath, safImagePath)

            if (!copySuccess) {
                throw Exception("Failed to copy image to SAF storage")
            }

            // Clean up temp file
            tempFile.delete()

            finalRelativePath = safRelativePath
        } catch (e: Exception) {
            tempFile.delete()
            throw e
        }
    } else {
        // Regular filesystem
        val imageFile = File(File(basePath), imageFileName)

        FileOutputStream(imageFile).use { outputStream -> bitmap.compress(Bitmap.CompressFormat.JPEG, MAX_IMAGE_QUALITY, outputStream) }

        finalRelativePath =
            if (path.isNotEmpty()) {
                val parentPath = File(path).parent
                if (parentPath != null && parentPath.isNotEmpty()) {
                    "$parentPath/$imageFileName"
                } else {
                    imageFileName
                }
            } else {
                imageFileName
            }
    }

    VPinballManager.vpinballJNI.VPinballSetTableImage(uuid, finalRelativePath)
}

fun Table.resetImage() {
    VPinballManager.vpinballJNI.VPinballSetTableImage(uuid, "")
}
