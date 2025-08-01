package org.vpinball.app.ui.screens.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import org.vpinball.app.VPinballManager
import org.vpinball.app.jni.VPinballAAFactor
import org.vpinball.app.jni.VPinballAO
import org.vpinball.app.jni.VPinballExternalDMD
import org.vpinball.app.jni.VPinballFXAA
import org.vpinball.app.jni.VPinballGfxBackend
import org.vpinball.app.jni.VPinballMSAASamples
import org.vpinball.app.jni.VPinballMaxTexDimension
import org.vpinball.app.jni.VPinballReflectionMode
import org.vpinball.app.jni.VPinballSettingsSection.PLAYER
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_ALPHA_DMD
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_B2S
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_B2S_LEGACY
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_DMD_UTIL
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_DOF
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_FLEX_DMD
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_PINMAME
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_PUP
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_REMOTE_CONTROL
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_SCORE_VIEW
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_SERUM
import org.vpinball.app.jni.VPinballSettingsSection.PLUGIN_WMP
import org.vpinball.app.jni.VPinballSettingsSection.STANDALONE
import org.vpinball.app.jni.VPinballSettingsSection.TABLE_OVERRIDE
import org.vpinball.app.jni.VPinballSharpen
import org.vpinball.app.jni.VPinballViewMode

class SettingsViewModel : ViewModel() {
    // General

    var haptics by mutableStateOf(false)
        private set

    var altColor by mutableStateOf(false)
        private set

    var altSound by mutableStateOf(false)
        private set

    var renderingModeOverride by mutableStateOf(false)
        private set

    var liveUIOverride by mutableStateOf(false)
        private set

    var gfxBackend by mutableStateOf(VPinballGfxBackend.OPENGLES)
        private set

    // External DMD

    var externalDMD by mutableStateOf(VPinballExternalDMD.NONE)
        private set

    var dmdServerAddr by mutableStateOf("0.0.0.0")
        private set

    var dmdServerPort by mutableIntStateOf(6789)
        private set

    var zedmdWiFiAddr by mutableStateOf("zedmd-wifi.local")
        private set

    // Display

    var bgSet by mutableStateOf(VPinballViewMode.DESKTOP_FSS)
        private set

    // Environment Lighting

    var overrideEmissionScale by mutableStateOf(false)
        private set

    var emissionScale by mutableIntStateOf(0)
        private set

    // Ball Rendering

    var ballTrail by mutableStateOf(false)
        private set

    var ballTrailStrength by mutableFloatStateOf(0.0f)
        private set

    var ballAntiStretch by mutableStateOf(false)
        private set

    var disableLightingForBalls by mutableStateOf(false)
        private set

    // Performance

    var maxAO by mutableStateOf(VPinballAO.AO_DISABLE)
        private set

    var pfReflection by mutableStateOf(VPinballReflectionMode.REFL_STATIC_N_DYNAMIC)
        private set

    var forceAnisotropicFiltering by mutableStateOf(false)
        private set

    var maxTexDimension by mutableStateOf(VPinballMaxTexDimension.MAX_768)
        private set

    var forceBloomOff by mutableStateOf(false)
        private set

    var forceMotionBlurOff by mutableStateOf(false)
        private set

    var alphaRampAccuracy by mutableIntStateOf(0)
        private set

    // Anti-Aliasing

    var msaaSamples by mutableStateOf(VPinballMSAASamples.DISABLED)
        private set

    var aaFactor by mutableStateOf(VPinballAAFactor.DISABLED)
        private set

    var fxaa by mutableStateOf(VPinballFXAA.STANDARD_FXAA)
        private set

    var sharpen by mutableStateOf(VPinballSharpen.DISABLED)
        private set

    var scaleFXDMD by mutableStateOf(false)
        private set

    // Web Server

    var webServer by mutableStateOf(false)
        private set

    var webServerPort by mutableIntStateOf(0)
        private set

    // Miscellaneous Features

    var ssRefl by mutableStateOf(false)
        private set

    // Advanced

    var resetLogOnPlay by mutableStateOf(false)
        private set

    // Plugins

    var pluginAlphaDMD by mutableStateOf(false)
        private set

    var pluginB2S by mutableStateOf(false)
        private set

    var pluginB2SLegacy by mutableStateOf(false)
        private set

    var pluginDMDUtil by mutableStateOf(false)
        private set

    var pluginDOF by mutableStateOf(false)
        private set

    var pluginFlexDMD by mutableStateOf(false)
        private set

    var pluginPinMAME by mutableStateOf(false)
        private set

    var pluginPUP by mutableStateOf(false)
        private set

    var pluginRemoteControl by mutableStateOf(false)
        private set

    var pluginScoreView by mutableStateOf(false)
        private set

    var pluginSerum by mutableStateOf(false)
        private set

    var pluginWMP by mutableStateOf(false)
        private set

    fun loadSettings() {
        // General

        haptics = VPinballManager.loadValue(STANDALONE, "Haptics", true)
        altColor = VPinballManager.loadValue(STANDALONE, "AltColor", true)
        altSound = VPinballManager.loadValue(STANDALONE, "AltSound", true)
        renderingModeOverride = (VPinballManager.loadValue(STANDALONE, "RenderingModeOverride", 2) == 2)
        liveUIOverride = VPinballManager.loadValue(STANDALONE, "LiveUIOverride", true)
        gfxBackend = VPinballGfxBackend.fromString(VPinballManager.loadValue(PLAYER, "GfxBackend", VPinballGfxBackend.OPENGLES.value))

        // External DMD

        externalDMD =
            when {
                VPinballManager.loadValue(STANDALONE, "DMDServer", false) -> VPinballExternalDMD.DMD_SERVER

                VPinballManager.loadValue(STANDALONE, "ZeDMDWiFi", false) -> VPinballExternalDMD.ZEDMD_WIFI

                else -> VPinballExternalDMD.NONE
            }

        dmdServerAddr = VPinballManager.loadValue(STANDALONE, "DMDServerAddr", "0.0.0.0")
        dmdServerPort = VPinballManager.loadValue(STANDALONE, "DMDServerPort", 6789)
        zedmdWiFiAddr = VPinballManager.loadValue(STANDALONE, "ZeDMDWiFiAddr", "zedmd-wifi.local")

        // Display

        bgSet = VPinballViewMode.fromInt(VPinballManager.loadValue(PLAYER, "BGSet", VPinballViewMode.DESKTOP_FSS.value))

        // Environment Lighting

        overrideEmissionScale = VPinballManager.loadValue(TABLE_OVERRIDE, "OverrideEmissionScale", false)
        emissionScale = (VPinballManager.loadValue(PLAYER, "EmissionScale", 0.5f) * 100).toInt()

        // Ball Rendering

        ballTrail = VPinballManager.loadValue(PLAYER, "BallTrail", true)
        ballTrailStrength = VPinballManager.loadValue(PLAYER, "BallTrailStrength", 0.5f)
        ballAntiStretch = VPinballManager.loadValue(PLAYER, "BallAntiStretch", false)
        disableLightingForBalls = VPinballManager.loadValue(PLAYER, "DisableLightingForBalls", false)

        // Performance

        maxAO =
            when {
                VPinballManager.loadValue(PLAYER, "DisableAO", false) -> VPinballAO.AO_DISABLE
                VPinballManager.loadValue(PLAYER, "DynamicAO", true) -> VPinballAO.AO_DYNAMIC
                else -> VPinballAO.AO_STATIC
            }
        pfReflection =
            VPinballReflectionMode.fromInt(VPinballManager.loadValue(PLAYER, "PFReflection", VPinballReflectionMode.REFL_STATIC_N_DYNAMIC.value))
        maxTexDimension =
            VPinballMaxTexDimension.fromInt(VPinballManager.loadValue(PLAYER, "MaxTexDimension", VPinballMaxTexDimension.MAX_1024.value))
        forceAnisotropicFiltering = VPinballManager.loadValue(PLAYER, "ForceAnisotropicFiltering", true)
        forceBloomOff = VPinballManager.loadValue(PLAYER, "ForceBloomOff", false)
        forceMotionBlurOff = VPinballManager.loadValue(PLAYER, "ForceMotionBlurOff", false)
        alphaRampAccuracy = VPinballManager.loadValue(PLAYER, "AlphaRampAccuracy", 10)

        // Anti-Aliasing

        msaaSamples = VPinballMSAASamples.fromInt(VPinballManager.loadValue(PLAYER, "MSAASamples", VPinballMSAASamples.DISABLED.value))
        aaFactor =
            VPinballAAFactor.fromFloat(
                VPinballManager.loadValue(PLAYER, "AAFactor", if (VPinballManager.loadValue(PLAYER, "USEAA", false)) 1.5f else 1.0f)
            )
        fxaa = VPinballFXAA.fromInt(VPinballManager.loadValue(PLAYER, "FXAA", VPinballFXAA.STANDARD_FXAA.value))
        sharpen = VPinballSharpen.fromInt(VPinballManager.loadValue(PLAYER, "Sharpen", VPinballSharpen.DISABLED.value))
        scaleFXDMD = VPinballManager.loadValue(PLAYER, "ScaleFXDMD", false)

        // Miscellaneous Features

        ssRefl = VPinballManager.loadValue(PLAYER, "SSRefl", false)

        // Web Server

        webServer = VPinballManager.loadValue(STANDALONE, "WebServer", false)
        webServerPort = VPinballManager.loadValue(STANDALONE, "WebServerPort", 2112)

        // Advanced

        resetLogOnPlay = VPinballManager.loadValue(STANDALONE, "ResetLogOnPlay", true)

        // Plugins

        pluginAlphaDMD = VPinballManager.loadValue(PLUGIN_ALPHA_DMD, "Enable", false)
        pluginB2S = VPinballManager.loadValue(PLUGIN_B2S, "Enable", false)
        pluginB2SLegacy = VPinballManager.loadValue(PLUGIN_B2S_LEGACY, "Enable", false)
        pluginDMDUtil = VPinballManager.loadValue(PLUGIN_DMD_UTIL, "Enable", false)
        pluginDOF = VPinballManager.loadValue(PLUGIN_DOF, "Enable", false)
        pluginFlexDMD = VPinballManager.loadValue(PLUGIN_FLEX_DMD, "Enable", false)
        pluginPinMAME = VPinballManager.loadValue(PLUGIN_PINMAME, "Enable", false)
        pluginPUP = VPinballManager.loadValue(PLUGIN_PUP, "Enable", false)
        pluginRemoteControl = VPinballManager.loadValue(PLUGIN_REMOTE_CONTROL, "Enable", false)
        pluginScoreView = VPinballManager.loadValue(PLUGIN_SCORE_VIEW, "Enable", true)
        pluginSerum = VPinballManager.loadValue(PLUGIN_SERUM, "Enable", false)
        pluginWMP = VPinballManager.loadValue(PLUGIN_WMP, "Enable", false)
    }

    // General

    fun handleHaptics(value: Boolean) {
        haptics = value
        VPinballManager.saveValue(STANDALONE, "Haptics", haptics)
    }

    fun handleAltColor(value: Boolean) {
        altColor = value
        VPinballManager.saveValue(STANDALONE, "AltColor", altColor)
    }

    fun handleAltSound(value: Boolean) {
        altSound = value
        VPinballManager.saveValue(STANDALONE, "AltSound", altSound)
    }

    fun handleRenderingModeOverride(value: Boolean) {
        renderingModeOverride = value
        VPinballManager.saveValue(STANDALONE, "RenderingModeOverride", if (renderingModeOverride) 2 else -1)
    }

    fun handleLiveUIOverride(value: Boolean) {
        liveUIOverride = value
        VPinballManager.saveValue(STANDALONE, "LiveUIOverride", liveUIOverride)
    }

    fun handleGfxBackend(value: VPinballGfxBackend) {
        gfxBackend = value
        VPinballManager.saveValue(PLAYER, "GfxBackend", value.value)
    }

    // External DMD

    fun handleExternalDMD(value: VPinballExternalDMD) {
        externalDMD = value
        VPinballManager.saveValue(STANDALONE, "DMDServer", externalDMD == VPinballExternalDMD.DMD_SERVER)
        VPinballManager.saveValue(STANDALONE, "ZeDMDWiFi", externalDMD == VPinballExternalDMD.ZEDMD_WIFI)
    }

    fun handleDMDServerAddr(value: String) {
        dmdServerAddr = value
        VPinballManager.saveValue(STANDALONE, "DMDServerAddr", dmdServerAddr)
    }

    fun handleDMDServerPort(value: Int) {
        dmdServerPort = value
        VPinballManager.saveValue(STANDALONE, "DMDServerPort", dmdServerPort)
    }

    fun handleZeDMDWiFiAddr(value: String) {
        zedmdWiFiAddr = value
        VPinballManager.saveValue(STANDALONE, "ZeDMDWiFiAddr", zedmdWiFiAddr)
    }

    // Display

    fun handleBGSet(value: VPinballViewMode) {
        bgSet = value
        VPinballManager.saveValue(PLAYER, "BGSet", bgSet.value)
    }

    // Environment Lighting

    fun handleOverrideEmissionScale(value: Boolean) {
        overrideEmissionScale = value
        VPinballManager.saveValue(TABLE_OVERRIDE, "OverrideEmissionScale", overrideEmissionScale)
    }

    fun handleEmissionScale(value: Int) {
        emissionScale = value
        VPinballManager.saveValue(PLAYER, "EmissionScale", value.toFloat() / 100f)
    }

    // Ball Rendering

    fun handleBallTrail(value: Boolean) {
        ballTrail = value
        VPinballManager.saveValue(PLAYER, "BallTrail", ballTrail)
    }

    fun handleBallTrailStrength(value: Float) {
        ballTrailStrength = value
        VPinballManager.saveValue(PLAYER, "BallTrailStrength", ballTrailStrength)
    }

    fun handleBallAntiStretch(value: Boolean) {
        ballAntiStretch = value
        VPinballManager.saveValue(PLAYER, "BallAntiStretch", ballAntiStretch)
    }

    fun handleDisableLightingForBalls(value: Boolean) {
        disableLightingForBalls = value
        VPinballManager.saveValue(PLAYER, "DisableLightingForBalls", disableLightingForBalls)
    }

    // Performance

    fun handleMaxAO(value: VPinballAO) {
        maxAO = value
        VPinballManager.saveValue(PLAYER, "DisableAO", maxAO == VPinballAO.AO_DISABLE)
        VPinballManager.saveValue(PLAYER, "DynamicAO", maxAO == VPinballAO.AO_DYNAMIC)
    }

    fun handlePFReflection(value: VPinballReflectionMode) {
        pfReflection = value
        VPinballManager.saveValue(PLAYER, "PFReflection", value.value)
    }

    fun handleMaxTexDimension(value: VPinballMaxTexDimension) {
        maxTexDimension = value
        VPinballManager.saveValue(PLAYER, "MaxTexDimension", value.value)
    }

    fun handleForceAnisotropicFiltering(value: Boolean) {
        forceAnisotropicFiltering = value
        VPinballManager.saveValue(PLAYER, "ForceAnisotropicFiltering", forceAnisotropicFiltering)
    }

    fun handleForceBloomOff(value: Boolean) {
        forceBloomOff = value
        VPinballManager.saveValue(PLAYER, "ForceBloomOff", forceBloomOff)
    }

    fun handleForceMotionBlurOff(value: Boolean) {
        forceMotionBlurOff = value
        VPinballManager.saveValue(PLAYER, "ForceMotionBlurOff", forceMotionBlurOff)
    }

    fun handleAlphaRampAccuracy(value: Int) {
        alphaRampAccuracy = value
        VPinballManager.saveValue(PLAYER, "AlphaRampAccuracy", alphaRampAccuracy)
    }

    // Anti-Aliasing

    fun handleMSAASamples(value: VPinballMSAASamples) {
        msaaSamples = value
        VPinballManager.saveValue(PLAYER, "MSAASamples", value.value)
    }

    fun handleAAFactor(value: VPinballAAFactor) {
        aaFactor = value
        VPinballManager.saveValue(PLAYER, "USEAA", aaFactor.floatValue > 1.0)
        VPinballManager.saveValue(PLAYER, "AAFactor", aaFactor.floatValue)
    }

    fun handleFXAA(value: VPinballFXAA) {
        fxaa = value
        VPinballManager.saveValue(PLAYER, "FXAA", fxaa.value)
    }

    fun handleSharpen(value: VPinballSharpen) {
        sharpen = value
        VPinballManager.saveValue(PLAYER, "Sharpen", sharpen.value)
    }

    fun handleScaleFXDMD(value: Boolean) {
        scaleFXDMD = value
        VPinballManager.saveValue(PLAYER, "ScaleFXDMD", value)
    }

    // Miscellaneous Features

    fun handleSSRefl(value: Boolean) {
        ssRefl = value
        VPinballManager.saveValue(PLAYER, "SSRefl", ssRefl)
    }

    // Web Server

    fun handleWebServer(value: Boolean) {
        webServer = value
        VPinballManager.saveValue(STANDALONE, "WebServer", webServer)
        VPinballManager.updateWebServer()
    }

    fun handleWebServerPort(value: Int) {
        webServerPort = value
        VPinballManager.saveValue(STANDALONE, "WebServerPort", webServerPort)
        VPinballManager.updateWebServer()
    }

    // Advanced

    fun handleResetLogOnPlay(value: Boolean) {
        resetLogOnPlay = value
        VPinballManager.saveValue(STANDALONE, "ResetLogOnPlay", resetLogOnPlay)
    }

    // Plugins

    fun handlePluginAlphaDMD(value: Boolean) {
        pluginAlphaDMD = value
        VPinballManager.saveValue(PLUGIN_ALPHA_DMD, "Enable", pluginAlphaDMD)
    }

    fun handlePluginB2S(value: Boolean) {
        pluginB2S = value
        VPinballManager.saveValue(PLUGIN_B2S, "Enable", pluginB2S)
    }

    fun handlePluginB2SLegacy(value: Boolean) {
        pluginB2SLegacy = value
        VPinballManager.saveValue(PLUGIN_B2S_LEGACY, "Enable", pluginB2SLegacy)
    }

    fun handlePluginDMDUtil(value: Boolean) {
        pluginDMDUtil = value
        VPinballManager.saveValue(PLUGIN_DMD_UTIL, "Enable", pluginDMDUtil)
    }

    fun handlePluginDOF(value: Boolean) {
        pluginDOF = value
        VPinballManager.saveValue(PLUGIN_DOF, "Enable", pluginDOF)
    }

    fun handlePluginFlexDMD(value: Boolean) {
        pluginFlexDMD = value
        VPinballManager.saveValue(PLUGIN_FLEX_DMD, "Enable", pluginFlexDMD)
    }

    fun handlePluginPinMAME(value: Boolean) {
        pluginPinMAME = value
        VPinballManager.saveValue(PLUGIN_PINMAME, "Enable", pluginPinMAME)
    }

    fun handlePluginPUP(value: Boolean) {
        pluginPUP = value
        VPinballManager.saveValue(PLUGIN_PUP, "Enable", pluginPUP)
    }

    fun handlePluginRemoteControl(value: Boolean) {
        pluginRemoteControl = value
        VPinballManager.saveValue(PLUGIN_REMOTE_CONTROL, "Enable", pluginRemoteControl)
    }

    fun handlePluginScoreView(value: Boolean) {
        pluginScoreView = value
        VPinballManager.saveValue(PLUGIN_SCORE_VIEW, "Enable", pluginScoreView)
    }

    fun handlePluginSerum(value: Boolean) {
        pluginSerum = value
        VPinballManager.saveValue(PLUGIN_SERUM, "Enable", pluginSerum)
    }

    fun handlePluginWMP(value: Boolean) {
        pluginWMP = value
        VPinballManager.saveValue(PLUGIN_WMP, "Enable", pluginWMP)
    }

    // Reset

    fun handleResetTouchInstructions() {
        VPinballManager.saveValue(STANDALONE, "TouchInstructions", true)
    }

    fun handleResetAllSettings() {
        VPinballManager.resetIni()
    }
}
