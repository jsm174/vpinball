package org.vpinball.app

import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import org.json.JSONArray
import org.vpinball.app.jni.VPinballLogLevel
import org.vpinball.app.jni.VPinballSettingsSection

class SAFFileSystem {
    private val activity: VPinballActivity
        get() = VPinballManager.activity

    private fun sendCopyProgressEvent(current: Int, total: Int) {
        val progressPercent = if (total > 0) (current * 100) / total else 0

        activity.runOnUiThread {
            activity.viewModel.progress(progressPercent)
            activity.viewModel.status("Copying table files... ($current/$total)")
        }
    }

    fun setExternalStorageUri(uri: Uri) {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: setExternalStorageUri: $uri")

        try {
            activity.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
            )
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: Failed to take persistent permission: ${e.message}")
            return
        }

        VPinballManager.saveValue(VPinballSettingsSection.STANDALONE, "TablesPath", uri.toString())

        VPinballManager.log(VPinballLogLevel.INFO, "SAF: External storage URI saved to TablesPath")
    }

    fun getExternalStorageUri(): Uri? {
        val tablesPath = VPinballManager.getTablesPath()
        return if (tablesPath.startsWith("content://")) {
            Uri.parse(tablesPath).also { VPinballManager.log(VPinballLogLevel.INFO, "SAF: External storage URI from TablesPath: $it") }
        } else {
            null
        }
    }

    fun clearExternalStorageUri() {
        VPinballManager.saveValue(VPinballSettingsSection.STANDALONE, "TablesPath", "")
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: Cleared TablesPath")
    }

    fun getExternalStorageDisplayPath(): String {
        val uri = getExternalStorageUri() ?: return ""

        try {
            val docId = DocumentsContract.getTreeDocumentId(uri)
            VPinballManager.log(VPinballLogLevel.INFO, "SAF: getExternalStorageDisplayPath - URI: $uri")
            VPinballManager.log(VPinballLogLevel.INFO, "SAF: getExternalStorageDisplayPath - docId: $docId")

            val split = docId.split(":")

            if (split.size >= 2) {
                val type = split[0]
                val path = split[1]

                val displayPath =
                    when {
                        type.equals("primary", ignoreCase = true) -> "/storage/emulated/0/$path"
                        else -> "/storage/$type/$path"
                    }

                VPinballManager.log(VPinballLogLevel.INFO, "SAF: getExternalStorageDisplayPath - result: $displayPath")
                return displayPath
            }

            VPinballManager.log(VPinballLogLevel.WARN, "SAF: getExternalStorageDisplayPath - returning docId: $docId")
            return docId
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.WARN, "SAF: Failed to parse URI: ${e.message}")
            e.printStackTrace()
            return uri.toString()
        }
    }

    fun writeFile(relativePath: String, content: String): Boolean {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: writeFile: $relativePath (${content.length} bytes)")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: writeFile: No external storage URI set")
            return false
        }

        return try {
            val docUri = buildDocumentUri(uri, relativePath, createIfMissing = true)
            if (docUri == null) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: writeFile: Failed to build document URI for: $relativePath")
                return false
            }

            activity.contentResolver.openOutputStream(docUri, "wt")?.use { output ->
                output.write(content.toByteArray())
                output.flush()
            }

            VPinballManager.log(VPinballLogLevel.INFO, "SAF: writeFile: Success")
            true
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: writeFile: Exception: ${e.message}")
            e.printStackTrace()
            false
        }
    }

    fun readFile(relativePath: String): String? {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: readFile: $relativePath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: readFile: No external storage URI set")
            return null
        }

        return try {
            val docUri = buildDocumentUri(uri, relativePath)
            if (docUri == null) {
                VPinballManager.log(VPinballLogLevel.WARN, "SAF: readFile: File not found: $relativePath")
                return null
            }

            val content = activity.contentResolver.openInputStream(docUri)?.use { input -> input.bufferedReader().readText() }

            VPinballManager.log(VPinballLogLevel.INFO, "SAF: readFile: Read ${content?.length ?: 0} bytes")
            content
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: readFile: Exception: ${e.message}")
            e.printStackTrace()
            null
        }
    }

    fun openInputStream(relativePath: String): java.io.InputStream? {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: openInputStream: $relativePath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: openInputStream: No external storage URI set")
            return null
        }

        return try {
            val docUri = buildDocumentUri(uri, relativePath)
            if (docUri == null) {
                VPinballManager.log(VPinballLogLevel.WARN, "SAF: openInputStream: File not found: $relativePath")
                return null
            }

            activity.contentResolver.openInputStream(docUri)
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: openInputStream: Exception: ${e.message}")
            e.printStackTrace()
            null
        }
    }

    fun exists(relativePath: String): Boolean {
        val uri = getExternalStorageUri() ?: return false
        val docUri = buildDocumentUri(uri, relativePath)
        val exists = docUri != null
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: exists: $relativePath -> $exists")
        return exists
    }

    fun listFilesRecursive(extension: String): String {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: listFilesRecursive: extension=$extension")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: listFilesRecursive: No external storage URI set")
            return "[]"
        }

        val results = mutableListOf<String>()

        fun scanRecursive(docUri: Uri, currentPath: String = "") {
            try {
                val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, DocumentsContract.getDocumentId(docUri))

                activity.contentResolver
                    .query(
                        childrenUri,
                        arrayOf(
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE,
                            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        ),
                        null,
                        null,
                        null,
                    )
                    ?.use { cursor ->
                        val nameIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME)
                        val mimeIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE)
                        val idIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID)

                        while (cursor.moveToNext()) {
                            val name = cursor.getString(nameIndex)
                            val mimeType = cursor.getString(mimeIndex)
                            val documentId = cursor.getString(idIndex)

                            if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
                                val childUri = DocumentsContract.buildDocumentUriUsingTree(uri, documentId)
                                scanRecursive(childUri, "$currentPath$name/")
                            } else if (name.endsWith(extension, ignoreCase = true)) {
                                results.add("$currentPath$name")
                            }
                        }
                    }
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: scanRecursive: Exception in $currentPath: ${e.message}")
                e.printStackTrace()
            }
        }

        val treeDocumentId = DocumentsContract.getTreeDocumentId(uri)
        val rootDocUri = DocumentsContract.buildDocumentUriUsingTree(uri, treeDocumentId)
        scanRecursive(rootDocUri)

        val jsonArray = JSONArray(results)
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: listFilesRecursive: Found ${results.size} files")
        return jsonArray.toString()
    }

    private fun buildDocumentUri(treeUri: Uri, relativePath: String, createIfMissing: Boolean = false): Uri? {
        if (relativePath.isEmpty()) return treeUri

        VPinballManager.log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: path='$relativePath' create=$createIfMissing")

        try {
            val treeDocumentId = DocumentsContract.getTreeDocumentId(treeUri)
            var currentUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, treeDocumentId)

            val segments = relativePath.split("/").filter { it.isNotEmpty() }

            VPinballManager.log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: segments=${segments.joinToString("/")}")

            for ((index, segment) in segments.withIndex()) {
                val isLastSegment = (index == segments.size - 1)

                val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, DocumentsContract.getDocumentId(currentUri))

                var foundUri: Uri? = null

                activity.contentResolver
                    .query(
                        childrenUri,
                        arrayOf(
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_MIME_TYPE,
                        ),
                        null,
                        null,
                        null,
                    )
                    ?.use { cursor ->
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
                    VPinballManager.log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Found segment '$segment'")
                    currentUri = foundUri!!
                } else if (createIfMissing) {
                    val mimeType =
                        if (isLastSegment && segment.contains(".")) {
                            "application/octet-stream"
                        } else {
                            DocumentsContract.Document.MIME_TYPE_DIR
                        }

                    VPinballManager.log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Creating '$segment' (type=$mimeType)")

                    val newUri = DocumentsContract.createDocument(activity.contentResolver, currentUri, mimeType, segment)

                    if (newUri != null) {
                        VPinballManager.log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Created '$segment' -> $newUri")
                        currentUri = newUri
                    } else {
                        VPinballManager.log(VPinballLogLevel.ERROR, "SAF: buildDocumentUri: Failed to create: $segment")
                        return null
                    }
                } else {
                    VPinballManager.log(VPinballLogLevel.WARN, "SAF: buildDocumentUri: Segment '$segment' not found and create=false")
                    return null
                }
            }

            VPinballManager.log(VPinballLogLevel.INFO, "SAF: buildDocumentUri: Success -> $currentUri")
            return currentUri
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: buildDocumentUri: Exception: ${e.message}")
            e.printStackTrace()
            return null
        }
    }

    fun copyFile(sourcePath: String, destPath: String): Boolean {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: copyFile: $sourcePath -> $destPath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyFile: No external storage URI set")
            return false
        }

        try {
            val sourceUri = buildDocumentUri(uri, sourcePath, createIfMissing = false)
            if (sourceUri == null) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyFile: Source not found: $sourcePath")
                return false
            }

            val destUri = buildDocumentUri(uri, destPath, createIfMissing = true)
            if (destUri == null) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyFile: Failed to create destination: $destPath")
                return false
            }

            activity.contentResolver.openInputStream(sourceUri)?.use { input ->
                activity.contentResolver.openOutputStream(destUri, "wt")?.use { output ->
                    input.copyTo(output)
                    return true
                }
            }

            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyFile: Failed to open streams")
            return false
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyFile: Exception: ${e.message}")
            e.printStackTrace()
            return false
        }
    }

    fun copySAFToFilesystem(safRelativePath: String, destPath: String): Boolean {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: copySAFToFilesystem: $safRelativePath -> $destPath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copySAFToFilesystem: No external storage URI")
            return false
        }

        var totalFiles = 0
        var copiedFiles = 0

        fun countFiles(srcRelPath: String): Int {
            val srcUri = buildDocumentUri(uri, srcRelPath) ?: return 0
            var count = 0

            try {
                activity.contentResolver.query(srcUri, arrayOf(DocumentsContract.Document.COLUMN_MIME_TYPE), null, null, null)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val mimeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                        val mimeType = cursor.getString(mimeIndex)

                        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
                            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, DocumentsContract.getDocumentId(srcUri))

                            activity.contentResolver
                                .query(childrenUri, arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null)
                                ?.use { childCursor ->
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
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: countFiles: Exception: ${e.message}")
            }

            return count
        }

        totalFiles = countFiles(safRelativePath)
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: copySAFToFilesystem: Total files to copy: $totalFiles")

        sendCopyProgressEvent(0, totalFiles)

        fun copyRecursive(srcRelPath: String, dstPath: String): Boolean {
            val srcUri = buildDocumentUri(uri, srcRelPath)
            if (srcUri == null) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copySAFToFilesystem: Source not found: $srcRelPath")
                return false
            }

            try {
                activity.contentResolver.query(srcUri, arrayOf(DocumentsContract.Document.COLUMN_MIME_TYPE), null, null, null)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val mimeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                        val mimeType = cursor.getString(mimeIndex)

                        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
                            File(dstPath).mkdirs()

                            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, DocumentsContract.getDocumentId(srcUri))

                            activity.contentResolver
                                .query(childrenUri, arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null)
                                ?.use { childCursor ->
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
                                FileOutputStream(dstPath).use { output ->
                                    val bytes = input.copyTo(output)
                                    VPinballManager.log(
                                        VPinballLogLevel.INFO,
                                        "SAF: copySAFToFilesystem: Copied $bytes bytes: $srcRelPath -> $dstPath",
                                    )
                                }
                            }
                            copiedFiles++
                            sendCopyProgressEvent(copiedFiles, totalFiles)
                            return true
                        }
                    }
                }
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copySAFToFilesystem: Exception: ${e.message}")
                return false
            }

            return false
        }

        val result = copyRecursive(safRelativePath, destPath)

        if (result) {
            activity.runOnUiThread {
                activity.viewModel.progress(100)
                activity.viewModel.status("Copy complete!")
            }
        }

        return result
    }

    fun copyDirectory(sourcePath: String, destPath: String): Boolean {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: copyDirectory: $sourcePath -> $destPath")

        try {
            val sourceFile = File(sourcePath)
            if (!sourceFile.exists()) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Source doesn't exist: $sourcePath")
                return false
            }

            if (sourceFile.isFile) {
                VPinballManager.log(VPinballLogLevel.INFO, "SAF: copyDirectory: Source is a file, copying as single file")
                val uri = getExternalStorageUri()
                if (uri == null) {
                    VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: No external storage URI")
                    return false
                }

                val destUri = buildDocumentUri(uri, destPath, createIfMissing = true)
                if (destUri == null) {
                    VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to create: $destPath")
                    return false
                }

                try {
                    val fileSize = sourceFile.length()
                    VPinballManager.log(VPinballLogLevel.INFO, "SAF: copyDirectory: Copying file (${fileSize} bytes)")

                    FileInputStream(sourceFile).use { input ->
                        activity.contentResolver.openOutputStream(destUri, "w")?.use { output ->
                            val bytesCopied = input.copyTo(output)
                            output.flush()
                            VPinballManager.log(VPinballLogLevel.INFO, "SAF: copyDirectory: Copied $bytesCopied bytes")
                            if (bytesCopied != fileSize) {
                                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Size mismatch! Expected $fileSize, got $bytesCopied")
                                return false
                            }
                        }
                            ?: run {
                                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to open output stream")
                                return false
                            }
                    }
                    VPinballManager.log(VPinballLogLevel.INFO, "SAF: copyDirectory: Successfully copied file: $destPath")
                    return true
                } catch (e: Exception) {
                    VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to copy file: ${e.message}")
                    e.printStackTrace()
                    return false
                }
            }

            if (!sourceFile.isDirectory) {
                VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Source is neither file nor directory: $sourcePath")
                return false
            }

            fun copyRecursive(srcFile: File, destRelativePath: String): Boolean {
                if (srcFile.isDirectory) {
                    val children = srcFile.listFiles() ?: emptyArray()
                    for (child in children) {
                        val childDestPath =
                            if (destRelativePath.isEmpty()) {
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
                        VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: No external storage URI")
                        return false
                    }

                    val destUri = buildDocumentUri(uri, destRelativePath, createIfMissing = true)
                    if (destUri == null) {
                        VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to create: $destRelativePath")
                        return false
                    }

                    try {
                        FileInputStream(srcFile).use { input ->
                            activity.contentResolver.openOutputStream(destUri, "wt")?.use { output -> input.copyTo(output) } ?: return false
                        }
                        VPinballManager.log(VPinballLogLevel.INFO, "SAF: copyDirectory: Copied file: $destRelativePath")
                        return true
                    } catch (e: Exception) {
                        VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Failed to copy file: ${e.message}")
                        return false
                    }
                }
            }

            return copyRecursive(sourceFile, destPath)
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: copyDirectory: Exception: ${e.message}")
            e.printStackTrace()
            return false
        }
    }

    fun delete(relativePath: String): Boolean {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: delete: $relativePath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: delete: No external storage URI")
            return false
        }

        val docUri = buildDocumentUri(uri, relativePath)
        if (docUri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: delete: Path not found: $relativePath")
            return false
        }

        return try {
            val deleted = DocumentsContract.deleteDocument(activity.contentResolver, docUri)
            VPinballManager.log(VPinballLogLevel.INFO, "SAF: delete: Result=$deleted for $relativePath")
            deleted
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: delete: Exception: ${e.message}")
            false
        }
    }

    fun isDirectory(relativePath: String): Boolean {
        val uri = getExternalStorageUri() ?: return false
        val docUri = buildDocumentUri(uri, relativePath) ?: return false

        return try {
            activity.contentResolver.query(docUri, arrayOf(DocumentsContract.Document.COLUMN_MIME_TYPE), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val mimeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                    val mimeType = cursor.getString(mimeIndex)
                    val isDir = mimeType == DocumentsContract.Document.MIME_TYPE_DIR
                    VPinballManager.log(VPinballLogLevel.INFO, "SAF: isDirectory: $relativePath -> $isDir (mime=$mimeType)")
                    isDir
                } else {
                    VPinballManager.log(VPinballLogLevel.ERROR, "SAF: isDirectory: No results for $relativePath")
                    false
                }
            } ?: false
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: isDirectory: Exception: ${e.message}")
            false
        }
    }

    fun listDirectory(relativePath: String): String {
        VPinballManager.log(VPinballLogLevel.INFO, "SAF: listDirectory: path=$relativePath")

        val uri = getExternalStorageUri()
        if (uri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: listDirectory: No external storage URI")
            return "[]"
        }

        val docUri =
            if (relativePath.isEmpty()) {
                val treeDocumentId = DocumentsContract.getTreeDocumentId(uri)
                DocumentsContract.buildDocumentUriUsingTree(uri, treeDocumentId)
            } else {
                buildDocumentUri(uri, relativePath)
            }

        if (docUri == null) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: listDirectory: Path not found: $relativePath")
            return "[]"
        }

        val results = mutableListOf<String>()

        try {
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, DocumentsContract.getDocumentId(docUri))

            activity.contentResolver
                .query(
                    childrenUri,
                    arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE),
                    null,
                    null,
                    null,
                )
                ?.use { cursor ->
                    val nameIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME)

                    while (cursor.moveToNext()) {
                        val name = cursor.getString(nameIndex)
                        val childPath = if (relativePath.isEmpty()) name else "$relativePath/$name"
                        results.add(childPath)
                    }
                }

            VPinballManager.log(VPinballLogLevel.INFO, "SAF: listDirectory: Found ${results.size} entries")
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "SAF: listDirectory: Exception: ${e.message}")
        }

        return JSONArray(results).toString()
    }
}
