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
    val iniRelativePath = path.substringBeforeLast('.') + ".ini"
    if (VPinballManager.isTablesPathSAF()) {
        VPinballManager.safFileSystem.delete(iniRelativePath)
    } else {
        File(iniPath).delete()
    }
}

fun Table.deleteFiles() {
    File(basePath).deleteRecursively()
}

fun Table.loadImage(): ImageBitmap? {
    if (image.isEmpty()) return null

    try {
        if (VPinballManager.isTablesPathSAF()) {
            val inputStream = VPinballManager.safFileSystem.openInputStream(image) ?: return null
            return inputStream.use { stream -> BitmapFactory.decodeStream(stream)?.asImageBitmap() }
        } else {
            return BitmapFactory.decodeFile(imagePath)?.asImageBitmap()
        }
    } catch (e: Exception) {
        VPinballManager.log(VPinballLogLevel.ERROR, "Failed to load image: ${e.message}")
        return null
    }
}

suspend fun Table.updateImage(bitmap: Bitmap) {
    val imageFileName = "${baseFilename}.jpg"

    val finalImagePath: String

    if (VPinballManager.isTablesPathSAF()) {
        val cacheDir = VPinballManager.getCacheDir()
        val tempFile = File(cacheDir, "temp_image_${uuid}.jpg")

        try {
            FileOutputStream(tempFile).use { outputStream -> bitmap.compress(Bitmap.CompressFormat.JPEG, MAX_IMAGE_QUALITY, outputStream) }
            finalImagePath = tempFile.absolutePath
        } catch (e: Exception) {
            tempFile.delete()
            throw e
        }
    } else {
        val imageFile = File(File(basePath), imageFileName)
        FileOutputStream(imageFile).use { outputStream -> bitmap.compress(Bitmap.CompressFormat.JPEG, MAX_IMAGE_QUALITY, outputStream) }
        finalImagePath = imageFile.absolutePath
    }

    org.vpinball.app.TableManager.getInstance().setTableImage(uuid, finalImagePath)
    org.vpinball.app.ui.screens.landing.LandingScreenViewModel.triggerRefresh()
}

suspend fun Table.resetImage() {
    org.vpinball.app.TableManager.getInstance().setTableImage(uuid, "")
    org.vpinball.app.ui.screens.landing.LandingScreenViewModel.triggerRefresh()
}
