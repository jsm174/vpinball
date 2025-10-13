package org.vpinball.app.ui.screens.landing

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.vpinball.app.TableListMode
import org.vpinball.app.TableListSortOrder
import org.vpinball.app.TableManager
import org.vpinball.app.VPinballManager
import org.vpinball.app.jni.Table
import org.vpinball.app.jni.VPinballSettingsSection.STANDALONE

class LandingScreenViewModel : ViewModel() {
    companion object {
        private var refreshCallback: (() -> Unit)? = null
        private var scrollToTableCallback: ((String) -> Unit)? = null

        fun setRefreshCallback(callback: () -> Unit) {
            refreshCallback = callback
        }

        fun triggerRefresh() {
            refreshCallback?.invoke()
        }

        fun setScrollToTableCallback(callback: (String) -> Unit) {
            scrollToTableCallback = callback
        }

        fun triggerScrollToTable(uuid: String) {
            scrollToTableCallback?.invoke(uuid)
        }
    }

    private var tableJob: Job? = null

    private val _unfilteredTables = MutableStateFlow(emptyList<Table>())
    val unfilteredTables: StateFlow<List<Table>> = _unfilteredTables

    private val _filteredTables = MutableStateFlow(emptyList<Table>())
    val filteredTables: StateFlow<List<Table>> = _filteredTables

    private val _tableListMode = MutableStateFlow(TableListMode.TWO_COLUMN)
    val tableListMode: StateFlow<TableListMode> = _tableListMode

    private val _tableListSortOrder = MutableStateFlow(TableListSortOrder.A_Z)
    val tableListSortOrder: StateFlow<TableListSortOrder> = _tableListSortOrder

    private val _search = MutableStateFlow("")
    val search: StateFlow<String> = _search

    private val _isLoading = MutableStateFlow(false)
    val isLoading: StateFlow<Boolean> = _isLoading

    private val _loadingProgress = MutableStateFlow(0)
    val loadingProgress: StateFlow<Int> = _loadingProgress

    private val _loadingStatus = MutableStateFlow("")
    val loadingStatus: StateFlow<String> = _loadingStatus

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
                    _isLoading.update { true }
                    _loadingProgress.update { 0 }
                    _loadingStatus.update { "" }

                    // Use TableManager to load tables with progress
                    val tables =
                        TableManager.loadTables { progress, status ->
                            _loadingProgress.update { progress }
                            _loadingStatus.update { status }
                        }
                    VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.INFO, "fetchTables: Loaded ${tables.size} tables")

                    // Sort tables based on current sort order and deduplicate by UUID
                    val sortedTables =
                        when (_tableListSortOrder.value) {
                            TableListSortOrder.A_Z -> tables.sortedBy { it.name }
                            TableListSortOrder.Z_A -> tables.sortedByDescending { it.name }
                        }.distinctBy { it.uuid }

                    withContext(Dispatchers.Main) {
                        _unfilteredTables.update { sortedTables }
                        _filteredTables.update { sortedTables }
                        search(search.value)
                        VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.INFO, "fetchTables: Updated UI with ${sortedTables.size} tables")
                    }
                } catch (e: Exception) {
                    VPinballManager.log(org.vpinball.app.jni.VPinballLogLevel.ERROR, "Failed to fetch tables: ${e.message}")
                    withContext(Dispatchers.Main) {
                        _unfilteredTables.update { emptyList() }
                        _filteredTables.update { emptyList() }
                    }
                } finally {
                    _isLoading.update { false }
                }
            }
    }

    private fun loadSettings() {
        _tableListMode.value = TableListMode.fromInt(VPinballManager.loadValue(STANDALONE, "TableListMode", TableListMode.TWO_COLUMN.value))
        _tableListSortOrder.value = TableListSortOrder.fromInt(VPinballManager.loadValue(STANDALONE, "TableListSort", TableListSortOrder.A_Z.value))
    }
}
