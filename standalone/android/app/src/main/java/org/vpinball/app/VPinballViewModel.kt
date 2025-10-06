package org.vpinball.app

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.vpinball.app.jni.Table

class VPinballViewModel : ViewModel() {
    private val _state = MutableStateFlow(VPinballUiState())
    val state: StateFlow<VPinballUiState> = _state.asStateFlow()

    var webServerURL by mutableStateOf("")

    var progress = mutableIntStateOf(0)
        private set

    var status = mutableStateOf("")
        private set

    private var splashTimerStarted = false

    fun startSplashTimer() {
        if (!splashTimerStarted) {
            splashTimerStarted = true
            viewModelScope.launch {
                delay(SPLASH_DELAY_DURATION)
                _state.update { it.copy(splash = false) }
            }
        }
    }

    fun title(value: String?) {
        _state.update { it.copy(title = value) }
    }

    fun progress(value: Int) {
        _state.update { it.copy(progress = value) }
        progress.intValue = value
    }

    fun status(value: String?) {
        _state.update { it.copy(status = value) }
        status.value = value ?: ""
    }

    fun loading(isLoading: Boolean, table: Table? = state.value.table) {
        _state.update { it.copy(loading = isLoading, table = table, title = table?.name ?: "") }
    }

    fun playing(isPlaying: Boolean) {
        _state.update { it.copy(playing = isPlaying) }
    }

    fun clearError() {
        _state.update { it.copy(error = null) }
    }

    fun setError(error: String) {
        _state.update { it.copy(error = error) }
    }

    fun stopped() {
        _state.update { it.copy(loading = false, playing = false, table = null) }
    }

    companion object {
        private val SPLASH_DELAY_DURATION = 2000.milliseconds
    }
}
