package org.vpinball.app

import android.content.Context
import android.net.Uri
import androidx.activity.result.launch
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import org.vpinball.app.jni.Table
import org.vpinball.app.jni.VPinballJNI
import org.vpinball.app.jni.VPinballLogLevel
import org.vpinball.app.jni.VPinballStatus
import org.vpinball.app.util.FileUtils

object TableManager {
    private val vpinballJNI = VPinballJNI()

    suspend fun loadTables(): List<Table> {
        return withContext(Dispatchers.IO) {
            try {
                val json = vpinballJNI.VPinballGetTables()

                @Serializable data class TablesResponse(val tableCount: Int, val tables: List<Table>)

                val response = Json.decodeFromString<TablesResponse>(json)
                VPinballManager.log(VPinballLogLevel.INFO, "loadTables: Decoded ${response.tableCount} tables")

                val allTables = response.tables
                val existingTables = allTables.filter { it.exists() }

                if (existingTables.size != allTables.size) {
                    VPinballManager.log(VPinballLogLevel.WARN, "loadTables: Filtered out ${allTables.size - existingTables.size} non-existing tables")
                }

                VPinballManager.log(VPinballLogLevel.INFO, "loadTables: Final table count = ${existingTables.size}")
                existingTables
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to decode VPX tables: ${e.message}")
                emptyList()
            }
        }
    }

    suspend fun importTable(
        context: Context,
        uri: Uri,
        onUpdate: (Int, String) -> Unit,
        onComplete: (uuid: String, path: String) -> Unit,
        onError: () -> Unit,
    ) {
        withContext(Dispatchers.IO) {
            try {
                val cacheDir = VPinballManager.getCacheDir()

                // Clean up cache directory
                cacheDir.deleteRecursively()
                cacheDir.mkdir()

                // Get filename from URI
                val filename = FileUtils.filenameFromUri(context, uri)
                if (filename == null) {
                    VPinballManager.log(VPinballLogLevel.ERROR, "Unable to get filename: uri=$uri")
                    withContext(Dispatchers.Main) {
                        onError()
                        VPinballManager.showError("Unable to import table.")
                    }
                    return@withContext
                }

                withContext(Dispatchers.Main) { onUpdate(20, "Copying file") }

                // Copy file to temp location for processing
                val tempFile = File(cacheDir, filename)
                FileUtils.copyFile(context, uri, tempFile) { progress ->
                    CoroutineScope(Dispatchers.Main).launch { onUpdate((20 + progress * 0.3).toInt(), "Copying file") }
                }

                withContext(Dispatchers.Main) { onUpdate(60, "Importing table") }

                // Use the new unified import method
                val status = vpinballJNI.VPinballImportTable(tempFile.absolutePath)

                // Clean up temp file
                tempFile.delete()

                if (status == VPinballStatus.SUCCESS.value) {
                    VPinballManager.log(VPinballLogLevel.INFO, "Successfully imported table: $filename")

                    withContext(Dispatchers.Main) {
                        onUpdate(100, "Import complete")
                        onComplete("", filename)
                    }
                } else {
                    VPinballManager.log(VPinballLogLevel.ERROR, "Failed to import table: $filename")
                    withContext(Dispatchers.Main) {
                        onError()
                        VPinballManager.showError("Unable to import table.")
                    }
                }
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Import error: ${e.message}")
                withContext(Dispatchers.Main) {
                    onError()
                    VPinballManager.showError("Unable to import table.")
                }
            }
        }
    }

    suspend fun deleteTable(table: Table): Boolean {
        return withContext(Dispatchers.IO) {
            val status = vpinballJNI.VPinballDeleteTable(table.uuid)
            if (status != VPinballStatus.SUCCESS.value) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to delete table: ${table.name}")
                false
            } else {
                true
            }
        }
    }

    suspend fun renameTable(table: Table, newName: String): Boolean {
        return withContext(Dispatchers.IO) {
            val status = vpinballJNI.VPinballRenameTable(table.uuid, newName)
            if (status != VPinballStatus.SUCCESS.value) {
                VPinballManager.log(VPinballLogLevel.ERROR, "Failed to rename table: ${table.name}")
                false
            } else {
                true
            }
        }
    }

    suspend fun shareTable(table: Table, onComplete: (file: File) -> Unit, onError: () -> Unit) {
        withContext(Dispatchers.IO) {
            if (!table.exists()) {
                withContext(Dispatchers.Main) {
                    onError()
                    VPinballManager.showError("Unable to share table.")
                }
                return@withContext
            }

            try {
                val exportPath = vpinballJNI.VPinballExportTable(table.uuid)
                withContext(Dispatchers.Main) {
                    if (exportPath != null) {
                        val exportFile = File(exportPath)
                        onComplete(exportFile)
                    } else {
                        onError()
                        VPinballManager.showError("Failed to export table.")
                    }
                }
            } catch (e: Exception) {
                VPinballManager.log(VPinballLogLevel.ERROR, "An error occurred: ${e.message}")
                withContext(Dispatchers.Main) { VPinballManager.showError("Unable to share table.") }
            }
        }
    }

    suspend fun extractTableScript(table: Table, onComplete: () -> Unit, onError: () -> Unit) {
        withContext(Dispatchers.IO) {
            // Load the table - C++ side handles validation for both normal and SAF paths
            val loadStatus = vpinballJNI.VPinballLoadTable(table.uuid)
            if (loadStatus != VPinballStatus.SUCCESS.value) {
                withContext(Dispatchers.Main) {
                    onError()
                    VPinballManager.showError("Unable to load table.")
                }
                return@withContext
            }

            // Then extract script from the loaded table
            val extractStatus = vpinballJNI.VPinballExtractTableScript()
            withContext(Dispatchers.Main) {
                if (extractStatus == VPinballStatus.SUCCESS.value) {
                    onComplete()
                } else {
                    onError()
                    VPinballManager.showError("Unable to extract script.")
                }
            }
        }
    }

    fun filteredTables(tables: List<Table>, searchText: String, sortAscending: Boolean = true): List<Table> {
        var filtered = tables

        // Apply search filter
        if (searchText.isNotEmpty()) {
            filtered = filtered.filter { table -> table.name.contains(searchText, ignoreCase = true) }
        }

        // Apply sort order
        filtered = filtered.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it.name })
        if (!sortAscending) {
            filtered = filtered.reversed()
        }

        return filtered
    }
}
