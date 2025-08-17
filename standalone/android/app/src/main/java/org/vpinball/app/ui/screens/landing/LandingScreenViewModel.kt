package org.vpinball.app.ui.screens.landing

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.vpinball.app.TableListMode
import org.vpinball.app.TableListSortOrder
import org.vpinball.app.VPinballManager
import org.vpinball.app.jni.VPinballVPXTable
import org.vpinball.app.jni.VPinballJNI
import org.vpinball.app.jni.VPinballSettingsSection.STANDALONE
import kotlinx.serialization.json.Json

class LandingScreenViewModel : ViewModel() {
    companion object {
        private var refreshCallback: (() -> Unit)? = null
        
        fun setRefreshCallback(callback: () -> Unit) {
            refreshCallback = callback
        }
        
        fun triggerRefresh() {
            refreshCallback?.invoke()
        }
    }
    private var tableJob: Job? = null
    private val vpinballJNI = VPinballJNI()

    private val _unfilteredTables = MutableStateFlow(emptyList<VPinballVPXTable>())
    val unfilteredTables: StateFlow<List<VPinballVPXTable>> = _unfilteredTables

    private val _filteredTables = MutableStateFlow(emptyList<VPinballVPXTable>())
    val filteredTables: StateFlow<List<VPinballVPXTable>> = _filteredTables

    private val _tableListMode = MutableStateFlow(TableListMode.TWO_COLUMN)
    val tableListMode: StateFlow<TableListMode> = _tableListMode

    private val _tableListSortOrder = MutableStateFlow(TableListSortOrder.A_Z)
    val tableListSortOrder: StateFlow<TableListSortOrder> = _tableListSortOrder

    private val _search = MutableStateFlow("")
    val search: StateFlow<String> = _search

    init {
        loadSettings()
        fetchTables()
        // Register this instance for refresh callbacks
        setRefreshCallback { refreshTables() }
    }

    fun setTableListMode(mode: TableListMode) {
        _tableListMode.update { mode }
        VPinballManager.saveValue(STANDALONE, "TableListMode", mode.value)
    }

    fun setTableSortOrder(order: TableListSortOrder) {
        _tableListSortOrder.update { order }
        VPinballManager.saveValue(STANDALONE, "TableListSort", order.value)
        fetchTables()
    }

    fun search(query: String) {
        _search.update { query }
        val unfilteredTables = _unfilteredTables.value
        if (query.isEmpty() || query.isBlank()) {
            _filteredTables.update { unfilteredTables.toList() }
        } else {
            _filteredTables.update { unfilteredTables.filter { it.name.lowercase().contains(query.trim().lowercase()) } }
        }
    }
    
    fun refreshTables() {
        fetchTables()
    }

    private fun fetchTables() {
        tableJob?.cancel()
        tableJob =
            viewModelScope.launch {
                try {
                    // Use C++ library to get tables
                    val jsonString = vpinballJNI.VPinballGetVPXTables()
                    VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.DEBUG, "fetchTables: Received JSON (length=${jsonString.length}): $jsonString")
                    
                    val tablesResponse = Json.decodeFromString<org.vpinball.app.jni.VPXTablesResponse>(jsonString)
                    VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.DEBUG, "fetchTables: Parsed ${tablesResponse.tableCount} tables, success=${tablesResponse.success}")
                    vpinballJNI.VPinballFreeString(jsonString)
                    
                    // Sort tables based on current sort order
                    val sortedTables = when (_tableListSortOrder.value) {
                        TableListSortOrder.A_Z -> tablesResponse.tables.sortedBy { it.name }
                        TableListSortOrder.Z_A -> tablesResponse.tables.sortedByDescending { it.name }
                    }
                    
                    _unfilteredTables.update { sortedTables }
                    _filteredTables.update { sortedTables }
                    search(search.value)
                } catch (e: Exception) {
                    VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.ERROR, "Failed to fetch tables: ${e.message}")
                    _unfilteredTables.update { emptyList() }
                    _filteredTables.update { emptyList() }
                }
            }
    }

    private fun loadSettings() {
        _tableListMode.value = TableListMode.fromInt(VPinballManager.loadValue(STANDALONE, "TableListMode", TableListMode.TWO_COLUMN.value))
        _tableListSortOrder.value = TableListSortOrder.fromInt(VPinballManager.loadValue(STANDALONE, "TableListSort", TableListSortOrder.A_Z.value))
    }
}
