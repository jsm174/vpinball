package org.vpinball.app

import org.vpinball.app.jni.Table

data class VPinballUiState(
    val splash: Boolean = true,
    val loading: Boolean = false,
    var table: Table? = null,
    var title: String? = null,
    var progress: Int = 0,
    var status: String? = null,
    var error: String? = null,
    val playing: Boolean = false,
)
