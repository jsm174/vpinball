package org.vpinball.app

import android.content.Context
import android.net.Uri
import android.util.Log
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import org.vpinball.app.jni.Table
import org.vpinball.app.jni.VPinballLogLevel

class TableManager(private val context: Context) {
    companion object {
        private const val TAG = "TableManager"
        private var instance: TableManager? = null

        private fun log(message: String) {
            Log.i(TAG, message)
        }

        fun initialize(context: Context) {
            if (instance == null) {
                instance = TableManager(context.applicationContext)
            }
        }

        fun getInstance(): TableManager {
            return instance ?: throw IllegalStateException("TableManager not initialized")
        }

        suspend fun loadTables(onProgress: ((Int, String) -> Unit)? = null): List<Table> {
            val instance = getInstance()
            instance.refresh(onProgress)
            return instance.tables.value
        }

        suspend fun importTable(
            context: Context,
            uri: Uri,
            onUpdate: (Int, String) -> Unit,
            onComplete: (String, String) -> Unit,
            onError: () -> Unit,
        ) {
            val instance = getInstance()
            val result = instance.importTable(uri, onUpdate)
            if (result) {
                onComplete("imported", "")
            } else {
                onError()
            }
        }

        suspend fun shareTable(table: Table, onComplete: (File) -> Unit, onError: () -> Unit) {
            val instance = getInstance()
            val file = instance.exportTable(table.uuid)
            if (file != null) {
                onComplete(file)
            } else {
                onError()
            }
        }

        suspend fun extractTableScript(table: Table, onProgress: ((Int, String) -> Unit)? = null, onComplete: () -> Unit, onError: () -> Unit) {
            val instance = getInstance()
            if (instance.extractTableScript(table.uuid, onProgress)) {
                onComplete()
            } else {
                onError()
            }
        }
    }

    private val _tables = MutableStateFlow<List<Table>>(emptyList())
    val tables: StateFlow<List<Table>> = _tables.asStateFlow()

    private var tablesPath: String = ""
    private var tablesJSONPath: String = ""
    private var requiresStaging: Boolean = false
    private var loadedTableUuid: String = ""
    private var loadedTableWorkingDir: String = ""
    private val fileOps = FileOps()

    init {
        loadTablesPath()
    }

    private fun log(message: String) {
        Log.i(TAG, message)
    }

    suspend fun refresh(onProgress: ((Int, String) -> Unit)? = null) {
        if (onProgress != null) {
            withContext(Dispatchers.IO) {
                loadTablesPath()
                loadTables(onProgress)
            }
        } else {
            loadTablesPath()
            loadTables(onProgress)
        }
    }

    fun getTable(uuid: String): Table? {
        return _tables.value.firstOrNull { it.uuid == uuid }
    }

    suspend fun importTable(uri: Uri, onProgress: (Int, String) -> Unit): Boolean {
        return withContext(Dispatchers.IO) {
            val fileName = fileOps.getFileName(context, uri)
            if (fileName == null) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to get filename from URI")
                return@withContext false
            }

            val tempFile = File(context.cacheDir, fileName)

            try {
                withContext(Dispatchers.Main) { onProgress(20, "Copying file") }

                val copyResult = fileOps.copy(context, uri, tempFile)
                if (!copyResult) {
                    VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy file")
                    return@withContext false
                }

                withContext(Dispatchers.Main) { onProgress(60, "Importing table") }

                val result = importTableSync(tempFile.absolutePath)
                tempFile.delete()

                if (result) {
                    withContext(Dispatchers.Main) {
                        onProgress(100, "Import complete")
                        loadTables()
                    }
                }

                result
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Import error: ${e.message}")
                e.printStackTrace()
                tempFile.delete()
                false
            }
        }
    }

    suspend fun deleteTable(uuid: String): Boolean {
        return withContext(Dispatchers.IO) {
            val result = deleteTableSync(uuid)
            if (result) {
                withContext(Dispatchers.Main) { loadTables() }
            }
            result
        }
    }

    suspend fun renameTable(uuid: String, newName: String): Boolean {
        return withContext(Dispatchers.IO) {
            val result = renameTableSync(uuid, newName)
            if (result) {
                withContext(Dispatchers.Main) { loadTables() }
            }
            result
        }
    }

    suspend fun setTableImage(uuid: String, imagePath: String): Boolean {
        return withContext(Dispatchers.IO) {
            val result = setTableImageSync(uuid, imagePath)
            if (result) {
                withContext(Dispatchers.Main) { loadTables() }
            }
            result
        }
    }

    suspend fun exportTable(uuid: String): File? {
        return withContext(Dispatchers.IO) {
            val path = exportTableSync(uuid)
            path?.let { File(it) }
        }
    }

    suspend fun stageTable(uuid: String, onProgress: ((Int, String) -> Unit)? = null): String? {
        return withContext(Dispatchers.IO) { stageTableSync(uuid, onProgress) }
    }

    suspend fun cleanupLoadedTable(uuid: String, onProgress: ((Int, String) -> Unit)? = null) {
        withContext(Dispatchers.IO) {
            cleanupLoadedTableSync(uuid, onProgress)
        }
    }

    suspend fun extractTableScript(uuid: String, onProgress: ((Int, String) -> Unit)? = null): Boolean {
        return withContext(Dispatchers.IO) { extractTableScriptSync(uuid, onProgress) }
    }

    private fun loadTablesPath() {
        val customPath = VPinballManager.loadValue(org.vpinball.app.jni.VPinballSettingsSection.STANDALONE, "TablesPath", "")

        tablesPath =
            if (customPath.isNotEmpty()) {
                customPath
            } else {
                File(context.filesDir, "tables").absolutePath
            }

        if (!tablesPath.endsWith("/")) {
            tablesPath += "/"
        }

        requiresStaging = tablesPath.startsWith("content://")

        tablesJSONPath =
            if (requiresStaging) {
                "${tablesPath}tables.json"
            } else {
                File(tablesPath, "tables.json").absolutePath
            }

        if (!requiresStaging && !fileOps.exists(tablesPath)) {
            fileOps.createDirectory(tablesPath)
        }
    }

    private fun loadTables(onProgress: ((Int, String) -> Unit)? = null) {
        var loadedTables = mutableListOf<Table>()

        onProgress?.invoke(10, "Loading tables...")

        val jsonExists = fileOps.exists(tablesJSONPath)

        if (jsonExists) {
            val content = fileOps.read(tablesJSONPath)
            if (content != null) {
                try {
                    @Serializable data class TablesResponse(val tableCount: Int, val tables: List<Table>)
                    val response = Json.decodeFromString<TablesResponse>(content)
                    loadedTables = response.tables.toMutableList()
                } catch (e: Exception) {
                    log("Failed to parse tables.json: ${e.message}")
                    e.printStackTrace()
                }
            }
        }

        onProgress?.invoke(30, "Validating tables...")

        val seen = mutableSetOf<String>()
        loadedTables.removeAll { table ->
            if (table.uuid.isEmpty() || seen.contains(table.uuid)) {
                return@removeAll true
            }
            seen.add(table.uuid)

            val fullPath = buildPath(table.path)
            if (!fileOps.exists(fullPath)) {
                return@removeAll true
            }

            false
        }

        onProgress?.invoke(50, "Scanning for images...")

        loadedTables.forEachIndexed { index, table ->
            if (table.image.isEmpty()) {
                val tableDir = File(table.path).parent ?: ""
                val stem = File(table.path).nameWithoutExtension

                val imagePath =
                    if (tableDir.isNotEmpty()) {
                        "$tableDir/$stem"
                    } else {
                        stem
                    }

                val pngPath = buildPath("$imagePath.png")
                val jpgPath = buildPath("$imagePath.jpg")

                val updatedImage =
                    when {
                        fileOps.exists(pngPath) -> relativePath(pngPath, tablesPath)
                        fileOps.exists(jpgPath) -> relativePath(jpgPath, tablesPath)
                        else -> table.image
                    }

                if (updatedImage != table.image) {
                    loadedTables[index] = table.copy(image = updatedImage)
                }
            }
        }

        onProgress?.invoke(70, "Scanning for tables...")
        val vpxFiles = fileOps.listFiles(tablesPath, ".vpx")

        onProgress?.invoke(90, "Finalizing...")
        for (filePath in vpxFiles) {
            if (!loadedTables.any { buildPath(it.path) == filePath }) {
                createTable(filePath)?.let {
                    loadedTables.add(it)
                }
            }
        }

        _tables.value = loadedTables
        saveTables()
        onProgress?.invoke(100, "Complete")
    }

    private fun saveTables() {
        val sorted = _tables.value.sortedBy { it.name.lowercase() }

        val jsonObject = buildString {
            append("{\"tableCount\":${sorted.size},\"tables\":[")
            sorted.forEachIndexed { index, table ->
                if (index > 0) append(",")
                append("{\"uuid\":\"${table.uuid}\",")
                append("\"name\":\"${table.name.replace("\"", "\\\"")}\",")
                append("\"path\":\"${table.path.replace("\"", "\\\"")}\",")
                append("\"image\":\"${table.image.replace("\"", "\\\"")}\",")
                append("\"createdAt\":${table.createdAt},")
                append("\"modifiedAt\":${table.modifiedAt}}")
            }
            append("]}")
        }

        try {
            fileOps.write(tablesJSONPath, jsonObject)
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to save tables.json: ${e.message}")
        }
    }

    private fun createTable(path: String): Table? {
        val uuid = generateUUID()
        val relativePath = relativePath(path, tablesPath)

        var name = File(relativePath).nameWithoutExtension.replace("_", " ")

        val now = System.currentTimeMillis() / 1000

        val parentPath = File(relativePath).parent ?: ""
        val stem = File(relativePath).nameWithoutExtension

        val imageBasePath =
            if (parentPath.isNotEmpty()) {
                "$parentPath/$stem"
            } else {
                stem
            }

        val pngPath = buildPath("$imageBasePath.png")
        val jpgPath = buildPath("$imageBasePath.jpg")

        val image =
            when {
                fileOps.exists(pngPath) -> relativePath(pngPath, tablesPath)
                fileOps.exists(jpgPath) -> relativePath(jpgPath, tablesPath)
                else -> ""
            }

        return Table(uuid = uuid, name = name, path = relativePath, image = image, createdAt = now, modifiedAt = now)
    }

    private fun generateUUID(): String {
        var uuid = UUID.randomUUID().toString().lowercase()
        while (_tables.value.any { it.uuid == uuid }) {
            uuid = UUID.randomUUID().toString().lowercase()
            VPinballManager.log(VPinballLogLevel.WARN, "UUID collision detected: $uuid, regenerating")
        }
        return uuid
    }

    private fun sanitizeName(name: String): String {
        val invalidChars = setOf(' ', '_', '/', '\\', ':', '*', '?', '"', '<', '>', '|', '.', '&', '\'', '(', ')')
        var sanitized = name.map { if (it in invalidChars) '-' else it }.joinToString("")

        var result = ""
        var lastWasHyphen = false
        for (char in sanitized) {
            if (char == '-') {
                if (!lastWasHyphen) {
                    result += char
                    lastWasHyphen = true
                }
            } else {
                result += char
                lastWasHyphen = false
            }
        }

        while (result.startsWith("-")) {
            result = result.drop(1)
        }
        while (result.endsWith("-")) {
            result = result.dropLast(1)
        }

        return result.ifEmpty { "table" }
    }

    private fun getUniqueFolder(baseName: String): String {
        val sanitized = sanitizeName(baseName)
        var candidate = sanitized
        var counter = 2

        while (fileOps.exists(buildPath(candidate))) {
            candidate = "$sanitized-$counter"
            counter++
        }

        return candidate
    }

    private fun buildPath(relativePath: String): String {
        return if (requiresStaging) {
            "$tablesPath$relativePath"
        } else {
            File(tablesPath, relativePath).absolutePath
        }
    }

    private fun relativePath(fullPath: String, basePath: String): String {
        if (fullPath.isEmpty() || basePath.isEmpty()) {
            return fullPath
        }

        if (!fullPath.startsWith(basePath)) {
            return fullPath
        }

        var result = fullPath.drop(basePath.length)
        if (result.startsWith("/")) {
            result = result.drop(1)
        }

        return result
    }

    private fun importTableSync(path: String): Boolean {
        if (!fileOps.exists(path)) {
            VPinballManager.log(VPinballLogLevel.ERROR, "File does not exist")
            return false
        }

        val ext = File(path).extension.lowercase()

        return when (ext) {
            "vpxz" -> importVPXZ(path)
            "vpx" -> importVPX(path)
            else -> {
                VPinballManager.log(VPinballLogLevel.ERROR, "Unsupported file extension: $ext")
                false
            }
        }
    }

    private fun importVPX(path: String): Boolean {
        val stem = File(path).nameWithoutExtension
        val name = stem.replace("_", " ")

        val folderName = getUniqueFolder(name)
        val destFolder = buildPath(folderName)

        if (!fileOps.createDirectory(destFolder)) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to create directory")
            return false
        }

        val fileName = File(path).name
        val destFile = buildPath("$folderName/$fileName")

        if (!fileOps.copy(path, destFile)) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy file")
            return false
        }

        return true
    }

    private fun importVPXZ(path: String): Boolean {
        val tempDir = File(context.cacheDir, "vpinball_import_${System.currentTimeMillis()}").absolutePath

        if (!fileOps.createDirectory(tempDir)) {
            return false
        }

        try {
            ZipInputStream(FileInputStream(path)).use { zip ->
                var entry = zip.nextEntry
                while (entry != null) {
                    val entryPath = File(tempDir, entry.name).absolutePath

                    if (entry.isDirectory) {
                        fileOps.createDirectory(entryPath)
                    } else {
                        val parentDir = File(entryPath).parent
                        if (parentDir != null) {
                            fileOps.createDirectory(parentDir)
                        }

                        FileOutputStream(entryPath).use { output -> zip.copyTo(output) }
                    }

                    entry = zip.nextEntry
                }
            }

            val vpxFiles = fileOps.listFiles(tempDir, ".vpx")

            if (vpxFiles.isEmpty()) {
                fileOps.deleteDirectory(tempDir)
                return false
            }

            var result = true
            for (vpxFile in vpxFiles) {
                val vpxName = File(vpxFile).nameWithoutExtension
                val sourceDir = File(vpxFile).parent ?: ""

                val folderName = getUniqueFolder(vpxName)
                val destFolder = buildPath(folderName)

                if (!fileOps.createDirectory(destFolder)) {
                    result = false
                    continue
                }

                if (!fileOps.copyDirectory(sourceDir, destFolder)) {
                    result = false
                    continue
                }
            }

            fileOps.deleteDirectory(tempDir)
            return result
        } catch (e: Exception) {
            fileOps.deleteDirectory(tempDir)
            return false
        }
    }

    private fun deleteTableSync(uuid: String): Boolean {
        val table = getTable(uuid) ?: return false

        val tablePath = buildPath(table.path)
        val tableDir = File(table.path).parent?.let { buildPath(it) } ?: return false

        if (tableDir.isEmpty() || tableDir == tablesPath || "$tableDir/" == tablesPath) {
            return false
        }

        if (fileOps.exists(tableDir)) {
            val vpxFiles = fileOps.listFiles(tableDir, ".vpx")

            if (vpxFiles.size <= 1) {
                if (!fileOps.deleteDirectory(tableDir)) {
                    return false
                }
            } else {
                if (!fileOps.delete(tablePath)) {
                    return false
                }
            }
        }

        _tables.value = _tables.value.filter { it.uuid != uuid }
        saveTables()

        return true
    }

    private fun renameTableSync(uuid: String, newName: String): Boolean {
        val table = getTable(uuid) ?: return false

        val now = System.currentTimeMillis() / 1000
        val updatedTable = table.copy(name = newName, modifiedAt = now)

        _tables.value = _tables.value.map { if (it.uuid == uuid) updatedTable else it }
        saveTables()

        return true
    }

    private fun setTableImageSync(uuid: String, imagePath: String): Boolean {
        val table = getTable(uuid) ?: return false

        if (imagePath.isEmpty()) {
            if (table.image.isNotEmpty()) {
                val currentImagePath = buildPath(table.image)
                if (fileOps.exists(currentImagePath)) {
                    fileOps.delete(currentImagePath)
                }
            }

            val now = System.currentTimeMillis() / 1000
            val updatedTable = table.copy(image = "", modifiedAt = now)
            _tables.value = _tables.value.map { if (it.uuid == uuid) updatedTable else it }
            saveTables()

            return true
        }

        if (imagePath.startsWith("/")) {
            val baseName = File(table.path).nameWithoutExtension

            val workingDir =
                if (uuid == loadedTableUuid && loadedTableWorkingDir.isNotEmpty()) {
                    loadedTableWorkingDir
                } else {
                    File(table.path).parent?.let { buildPath(it) } ?: ""
                }

            val destPath =
                if (requiresStaging && uuid == loadedTableUuid) {
                    File(workingDir, "$baseName.jpg").absolutePath
                } else {
                    buildPath("${File(table.path).parent}/$baseName.jpg")
                }

            if (!fileOps.copy(imagePath, destPath)) {
                return false
            }

            val relPath = relativePath(destPath, tablesPath)
            val now = System.currentTimeMillis() / 1000
            val updatedTable = table.copy(image = relPath, modifiedAt = now)

            _tables.value = _tables.value.map { if (it.uuid == uuid) updatedTable else it }
            saveTables()

            return true
        }

        val now = System.currentTimeMillis() / 1000
        val updatedTable = table.copy(image = imagePath, modifiedAt = now)
        _tables.value = _tables.value.map { if (it.uuid == uuid) updatedTable else it }
        saveTables()

        return true
    }

    private fun exportTableSync(uuid: String): String? {
        val table = getTable(uuid) ?: return null

        val sanitizedName = sanitizeName(table.name)
        val tempFile = File(context.cacheDir, "$sanitizedName.vpxz")

        if (tempFile.exists()) {
            tempFile.delete()
        }

        val tableDir = File(table.path).parent ?: return null

        try {
            val tableDirToCompressFinal =
                if (requiresStaging) {
                    val stagingBaseDir = File(context.cacheDir, "staged_export")
                    val stagingTableDir = File(stagingBaseDir, tableDir)

                    if (stagingTableDir.exists()) {
                        fileOps.deleteDirectory(stagingTableDir.absolutePath)
                    }

                    if (!fileOps.createDirectory(stagingTableDir.absolutePath)) {
                        return null
                    }

                    val sourceTableDir = buildPath(tableDir)

                    if (!fileOps.copyDirectory(sourceTableDir, stagingTableDir.absolutePath)) {
                        return null
                    }

                    stagingTableDir.absolutePath
                } else {
                    File(buildPath(tableDir)).absolutePath
                }

            ZipOutputStream(FileOutputStream(tempFile)).use { zip ->
                fileOps.addDirectoryToZip(zip, tableDirToCompressFinal, tableDirToCompressFinal)
            }

            return tempFile.absolutePath
        } catch (e: Exception) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to export table: ${e.message}")
            return null
        }
    }

    private fun stageTableSync(uuid: String, onProgress: ((Int, String) -> Unit)? = null): String? {
        val table = getTable(uuid) ?: return null

        if (table.path.isEmpty()) {
            return null
        }

        val tableDir = File(table.path).parent ?: ""
        val fileName = File(table.path).name

        if (requiresStaging) {
            onProgress?.invoke(10, "Staging table...")
            val cachePath = File(context.filesDir, "staging_cache/$tableDir").absolutePath

            if (fileOps.exists(cachePath)) {
                fileOps.deleteDirectory(cachePath)
            }

            fileOps.createDirectory(cachePath)

            onProgress?.invoke(30, "Copying table files...")
            val sourceTableDir = buildPath(tableDir)
            if (!fileOps.copyDirectory(sourceTableDir, cachePath)) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy to staging cache")
                return null
            }

            onProgress?.invoke(100, "Ready")
            loadedTableUuid = uuid
            loadedTableWorkingDir = cachePath

            return File(cachePath, fileName).absolutePath
        }

        val fullPath = buildPath(table.path)
        loadedTableUuid = uuid
        loadedTableWorkingDir = File(fullPath).parent ?: "."

        if (loadedTableWorkingDir.isEmpty()) {
            loadedTableWorkingDir = "."
        }

        return fullPath
    }

    private fun cleanupLoadedTableSync(uuid: String, onProgress: ((Int, String) -> Unit)? = null) {
        if (loadedTableUuid != uuid) {
            return
        }

        if (requiresStaging && loadedTableWorkingDir.isNotEmpty()) {
            val table = getTable(uuid)
            if (table != null) {
                onProgress?.invoke(10, "Saving changes...")
                val tableDir = File(table.path).parent ?: ""
                val destTableDir = buildPath(tableDir)

                val changedFileExtensions = listOf("txt", "ini", "cfg", "xml", "nv")
                val workingDirFile = File(loadedTableWorkingDir)
                val filesToCopy = workingDirFile.listFiles()?.filter { file ->
                    file.isFile && changedFileExtensions.contains(file.extension.lowercase())
                } ?: emptyList()

                if (filesToCopy.isNotEmpty()) {
                    onProgress?.invoke(30, "Copying ${filesToCopy.size} file(s)...")
                    var copiedCount = 0
                    var failedCount = 0

                    filesToCopy.forEach { file ->
                        val relativePath = file.name
                        val destPath = buildPath("$tableDir/$relativePath")

                        if (fileOps.copy(file.absolutePath, destPath)) {
                            copiedCount++
                            val progress = 30 + ((copiedCount * 60) / filesToCopy.size)
                            onProgress?.invoke(progress, "Copied $copiedCount/${filesToCopy.size}")
                        } else {
                            failedCount++
                            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy back: ${file.name}")
                        }
                    }

                    onProgress?.invoke(100, "Complete")

                    if (failedCount > 0) {
                        VPinballManager.log(VPinballLogLevel.WARN, "Sync complete: $copiedCount succeeded, $failedCount failed")
                    }
                } else {
                    onProgress?.invoke(100, "No changes to save")
                }
            }
        }

        loadedTableUuid = ""
        loadedTableWorkingDir = ""
    }

    private fun extractTableScriptSync(uuid: String, onProgress: ((Int, String) -> Unit)? = null): Boolean {
        val table = getTable(uuid) ?: return false

        val tableDir = File(table.path).parent ?: return false
        val baseName = File(table.path).nameWithoutExtension
        val scriptPath = buildPath("$tableDir/$baseName.vbs")

        if (fileOps.exists(scriptPath)) {
            return true
        }

        val fullPath: String
        val cachePath: String?

        if (requiresStaging) {
            onProgress?.invoke(10, "Staging table...")
            cachePath = File(context.filesDir, "staging_cache/$tableDir").absolutePath

            if (fileOps.exists(cachePath)) {
                fileOps.deleteDirectory(cachePath)
            }

            fileOps.createDirectory(cachePath)

            onProgress?.invoke(30, "Copying table files...")
            val sourceTableDir = buildPath(tableDir)
            if (!fileOps.copyDirectory(sourceTableDir, cachePath)) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy to staging cache")
                return false
            }

            onProgress?.invoke(70, "Extracting script...")
            fullPath = File(cachePath, File(table.path).name).absolutePath
        } else {
            onProgress?.invoke(50, "Extracting script...")
            fullPath = buildPath(table.path)
            cachePath = null
        }

        if (VPinballManager.vpinballJNI.VPinballLoadTable(fullPath) != 0) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to load table for script extraction: $fullPath")
            cachePath?.let { fileOps.deleteDirectory(it) }
            return false
        }

        if (VPinballManager.vpinballJNI.VPinballExtractTableScript() != 0) {
            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to extract script from table")
            cachePath?.let { fileOps.deleteDirectory(it) }
            return false
        }

        if (requiresStaging && cachePath != null) {
            onProgress?.invoke(90, "Copying script back...")
            val stagedScriptPath = File(cachePath, "$baseName.vbs").absolutePath

            val destScriptPath = buildPath("$tableDir/$baseName.vbs")
            if (!fileOps.copy(stagedScriptPath, destScriptPath)) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy script back to SAF")
                fileOps.deleteDirectory(cachePath)
                return false
            }

            fileOps.deleteDirectory(cachePath)
        }

        onProgress?.invoke(100, "Complete")

        return if (fileOps.exists(scriptPath)) {
            true
        } else {
            VPinballManager.log(VPinballLogLevel.WARN, "Script file not found after extraction")
            false
        }
    }

    fun filteredTables(searchText: String, sortAscending: Boolean = true): List<Table> {
        var filtered = _tables.value

        if (searchText.isNotEmpty()) {
            filtered = filtered.filter { it.name.contains(searchText, ignoreCase = true) }
        }

        filtered = filtered.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it.name })
        if (!sortAscending) {
            filtered = filtered.reversed()
        }

        return filtered
    }

    fun hasScriptFile(table: Table): Boolean {
        val scriptRelativePath = table.path.substringBeforeLast('.') + ".vbs"
        return fileOps.exists(buildPath(scriptRelativePath))
    }

    fun hasIniFile(table: Table): Boolean {
        val iniRelativePath = table.path.substringBeforeLast('.') + ".ini"
        return fileOps.exists(buildPath(iniRelativePath))
    }

    private inner class FileOps {
        fun exists(path: String): Boolean {
            if (path.startsWith("content://")) {
                val relativePath =
                    if (path.startsWith(tablesPath)) {
                        path.drop(tablesPath.length)
                    } else {
                        path
                    }
                return VPinballManager.safFileSystem.exists(relativePath)
            }
            return File(path).exists()
        }

        fun read(path: String): String? {
            if (path.startsWith("content://")) {
                val relativePath =
                    if (path.startsWith(tablesPath)) {
                        path.drop(tablesPath.length)
                    } else {
                        path
                    }
                return VPinballManager.safFileSystem.readFile(relativePath)
            }
            return try {
                File(path).readText()
            } catch (e: Exception) {
                null
            }
        }

        fun write(path: String, content: String): Boolean {
            if (path.startsWith("content://")) {
                val relativePath =
                    if (path.startsWith(tablesPath)) {
                        path.drop(tablesPath.length)
                    } else {
                        path
                    }
                return VPinballManager.safFileSystem.writeFile(relativePath, content)
            }
            return try {
                File(path).writeText(content)
                true
            } catch (e: Exception) {
                false
            }
        }

        fun copy(from: String, to: String): Boolean {
            if (to.startsWith("content://")) {
                // Copy TO SAF
                val relativePath =
                    if (to.startsWith(tablesPath)) {
                        to.drop(tablesPath.length)
                    } else {
                        to
                    }
                return try {
                    FileInputStream(from).use { input ->
                        val outputStream = VPinballManager.safFileSystem.openOutputStream(relativePath)
                        if (outputStream != null) {
                            outputStream.use { output -> input.copyTo(output) }
                            true
                        } else {
                            VPinballManager.log(VPinballLogLevel.ERROR, "Failed to open output stream for SAF: $relativePath")
                            false
                        }
                    }
                } catch (e: Exception) {
                    VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy to SAF: ${e.message}")
                    false
                }
            }

            return try {
                File(from).copyTo(File(to), overwrite = true)
                true
            } catch (e: Exception) {
                false
            }
        }

        fun copy(context: Context, from: Uri, to: File): Boolean {
            return try {
                when (from.scheme) {
                    "file" -> {
                        val fromFile = File(from.path ?: return false)
                        if (!fromFile.exists()) {
                            VPinballManager.log(VPinballLogLevel.ERROR, "Source file does not exist: ${fromFile.absolutePath}")
                            return false
                        }

                        if (fromFile.canonicalPath == to.canonicalPath) {
                            return true
                        }

                        fromFile.copyTo(to, overwrite = true)
                        true
                    }
                    "content" -> {
                        context.contentResolver.openInputStream(from)?.use { input -> FileOutputStream(to).use { output -> input.copyTo(output) } }
                        true
                    }
                    else -> {
                        VPinballManager.log(VPinballLogLevel.ERROR, "Unsupported URI scheme: ${from.scheme}")
                        false
                    }
                }
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to copy file: ${e.message}")
                false
            }
        }

        fun delete(path: String): Boolean {
            if (path.startsWith("content://")) {
                val relativePath =
                    if (path.startsWith(tablesPath)) {
                        path.drop(tablesPath.length)
                    } else {
                        path
                    }
                return VPinballManager.safFileSystem.delete(relativePath)
            }
            return try {
                File(path).delete()
            } catch (e: Exception) {
                false
            }
        }

        fun deleteDirectory(path: String): Boolean {
            if (path.startsWith("content://")) {
                val relativePath =
                    if (path.startsWith(tablesPath)) {
                        path.drop(tablesPath.length)
                    } else {
                        path
                    }
                return VPinballManager.safFileSystem.delete(relativePath)
            }
            return try {
                File(path).deleteRecursively()
            } catch (e: Exception) {
                false
            }
        }

        fun createDirectory(path: String): Boolean {
            if (path.startsWith("content://")) {
                // SAF directories are created automatically when writing files
                return true
            }
            return try {
                File(path).mkdirs()
            } catch (e: Exception) {
                false
            }
        }

        fun listFiles(path: String, ext: String): List<String> {
            val files = mutableListOf<String>()

            if (path.startsWith("content://")) {
                // SAF path - use SAF file system
                val result = VPinballManager.safFileSystem.listFilesRecursive(ext.removePrefix("."))
                if (result.isNotEmpty()) {
                    // Parse JSON array of file paths
                    try {
                        val jsonArray = org.json.JSONArray(result)
                        for (i in 0 until jsonArray.length()) {
                            val relativePath = jsonArray.getString(i)
                            files.add("$path$relativePath")
                        }
                    } catch (e: Exception) {
                        VPinballManager.log(VPinballLogLevel.ERROR, "Error parsing SAF file list: ${e.message}")
                    }
                }
                return files
            }

            try {
                File(path).walkTopDown().forEach { file ->
                    if (file.isFile) {
                        if (ext.isEmpty() || file.extension.lowercase() == ext.lowercase().removePrefix(".")) {
                            files.add(file.absolutePath)
                        }
                    }
                }
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Error listing files in $path: ${e.message}")
            }

            return files
        }

        fun copyDirectory(from: String, to: String): Boolean {
            // Copying FROM SAF TO regular filesystem
            if (from.startsWith("content://") && !to.startsWith("content://")) {
                val relativePath =
                    if (from.startsWith(tablesPath)) {
                        from.drop(tablesPath.length)
                    } else {
                        from
                    }
                return VPinballManager.safFileSystem.copySAFToFilesystem(relativePath, to)
            }

            // Copying FROM regular filesystem TO SAF
            if (!from.startsWith("content://") && to.startsWith("content://")) {
                val relativePath =
                    if (to.startsWith(tablesPath)) {
                        to.drop(tablesPath.length)
                    } else {
                        to
                    }
                return VPinballManager.safFileSystem.copyDirectory(from, relativePath)
            }

            // Regular filesystem copy
            return try {
                val fromDir = File(from)
                val toDir = File(to)

                if (toDir.exists()) {
                    toDir.deleteRecursively()
                }

                toDir.mkdirs()

                fromDir.listFiles()?.forEach { file ->
                    val destPath = File(toDir, file.name)

                    if (file.isDirectory) {
                        if (!copyDirectory(file.absolutePath, destPath.absolutePath)) {
                            return false
                        }
                    } else {
                        file.copyTo(destPath, overwrite = true)
                    }
                }

                true
            } catch (e: Exception) {
                false
            }
        }

        fun addDirectoryToZip(zip: ZipOutputStream, directoryPath: String, basePath: String) {
            File(directoryPath).walkTopDown().forEach { file ->
                val relativePath = file.absolutePath.drop(basePath.length + 1)

                if (file.isDirectory) {
                    zip.putNextEntry(ZipEntry("$relativePath/"))
                    zip.closeEntry()
                } else {
                    zip.putNextEntry(ZipEntry(relativePath))
                    FileInputStream(file).use { input -> input.copyTo(zip) }
                    zip.closeEntry()
                }
            }
        }

        fun getFileName(context: Context, uri: Uri): String? {
            return try {
                when (uri.scheme) {
                    "file" -> {
                        File(uri.path ?: return null).name
                    }
                    "content" -> {
                        val cursor = context.contentResolver.query(uri, null, null, null, null)
                        cursor?.use {
                            val nameIndex = it.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                            it.moveToFirst()
                            it.getString(nameIndex)
                        }
                    }
                    else -> null
                }
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to get filename from URI: ${e.message}")
                null
            }
        }
    }
}
