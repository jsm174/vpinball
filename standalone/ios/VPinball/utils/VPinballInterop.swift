import SwiftUI

// VPinball Enums

enum VPinballLogLevel: CInt {
    case debug
    case info
    case warn
    case error
}

enum VPinballStatus: CInt {
    case success
    case failure
}

enum VPinballSettingsSection: String {
    case standalone = "Standalone"
    case player = "Player"
    case alpha = "Alpha"
    case backglass = "Backglass"
    case scoreView = "ScoreView"
    case topper = "Topper"
    case tableOverride = "TableOverride"
    case tableOption = "TableOption"
    case pluginAlphaDMD = "Plugin.AlphaDMD"
    case pluginB2S = "Plugin.B2S"
    case pluginB2SLegacy = "Plugin.B2SLegacy"
    case pluginDMDUtil = "Plugin.DMDUtil"
    case pluginDOF = "Plugin.DOF"
    case pluginFlexDMD = "Plugin.FlexDMD"
    case pluginPinMAME = "Plugin.PinMAME"
    case pluginPUP = "Plugin.PUP"
    case pluginRemoteControl = "Plugin.RemoteControl"
    case pluginScoreView = "Plugin.ScoreView"
    case pluginSerum = "Plugin.Serum"
    case pluginWMP = "Plugin.WMP"
}

enum VPinballViewMode: CInt {
    case desktopFSS
    case cabinet
    case desktopNoFSS

    static let all: [VPinballViewMode] = [.desktopFSS,
                                          .cabinet,
                                          .desktopNoFSS]

    var name: String {
        switch self {
        case .desktopFSS:
            return "Desktop & FSS"
        case .cabinet:
            return "Cabinet"
        case .desktopNoFSS:
            return "Desktop (no FSS)"
        }
    }
}

enum VPinballAO: CInt {
    case aoDisable
    case aoStatic
    case aoDynamic

    static let all: [VPinballAO] = [.aoDisable,
                                    .aoStatic,
                                    .aoDynamic]

    var name: String {
        switch self {
        case .aoDisable:
            return "Disable AO"
        case .aoStatic:
            return "Static AO"
        case .aoDynamic:
            return "Dynamic AO"
        }
    }
}

enum VPinballReflectionMode: CInt {
    case reflNone
    case reflBalls
    case reflStatic
    case reflStaticNBalls
    case reflStaticNDynamic
    case reflDynamic

    static let all: [VPinballReflectionMode] = [.reflNone,
                                                .reflBalls,
                                                .reflStatic,
                                                .reflStaticNBalls,
                                                .reflStaticNDynamic,
                                                .reflDynamic]

    var name: String {
        switch self {
        case .reflNone:
            return "Disable Reflections"
        case .reflBalls:
            return "Balls Only"
        case .reflStatic:
            return "Static Only"
        case .reflStaticNBalls:
            return "Static & Balls"
        case .reflStaticNDynamic:
            return "Static & Unsynced Dynamic"
        case .reflDynamic:
            return "Dynamic"
        }
    }
}

enum VPinballMaxTexDimension: CInt {
    case unlimited = 0
    case max256 = 256
    case max384 = 384
    case max512 = 512
    case max768 = 768
    case max1024 = 1024
    case max1280 = 1280
    case max1536 = 1536
    case max1792 = 1792
    case max2048 = 2048
    case max3172 = 3172
    case max4096 = 4096

    static let all: [VPinballMaxTexDimension] = [.max256,
                                                 .max384,
                                                 .max512,
                                                 .max768,
                                                 .max1024,
                                                 .max1280,
                                                 .max1536,
                                                 .max1792,
                                                 .max2048,
                                                 .max3172,
                                                 .max4096,
                                                 .unlimited]

    var name: String {
        return self == .unlimited ? "Unlimited" : String(rawValue)
    }
}

enum VPinballMSAASamples: CInt {
    case disabled = 1
    case samples4 = 4
    case samples6 = 6
    case samples8 = 8

    static let all: [VPinballMSAASamples] = [.disabled,
                                             .samples4,
                                             .samples6,
                                             .samples8]

    var name: String {
        return self == .disabled ? "Disabled" : "\(rawValue) Samples"
    }
}

enum VPinballAAFactor: CInt {
    case pct50 = 50
    case pct75 = 75
    case disabled = 100
    case pct125 = 125
    case pct133 = 133
    case pct150 = 150
    case pct175 = 175
    case pct200 = 200

    static let all: [VPinballAAFactor] = [.pct50,
                                          .pct75,
                                          .disabled,
                                          .pct125,
                                          .pct133,
                                          .pct150,
                                          .pct175,
                                          .pct200]

    var name: String {
        return self == .disabled ? "Disabled" : "\(rawValue)%"
    }

    var floatValue: Float {
        return Float(rawValue) / 100.0
    }

    static func fromFloat(_ value: Float) -> VPinballAAFactor? {
        return VPinballAAFactor(rawValue: CInt(value * 100))
    }
}

enum VPinballFXAA: CInt {
    case disabled
    case fastFXAA
    case standardFXAA
    case qualityFXAA
    case fastNFAA
    case standardDLAA
    case qualitySMAA

    static let all: [VPinballFXAA] = [.disabled,
                                      .fastFXAA,
                                      .standardFXAA,
                                      .qualityFXAA,
                                      .fastNFAA,
                                      .standardDLAA,
                                      .qualitySMAA]

    var name: String {
        switch self {
        case .disabled:
            return "Disabled"
        case .fastFXAA:
            return "Fast FXAA"
        case .standardFXAA:
            return "Standard FXAA"
        case .qualityFXAA:
            return "Quality FXAA"
        case .fastNFAA:
            return "Fast NFAA"
        case .standardDLAA:
            return "Standard DLAA"
        case .qualitySMAA:
            return "Quality SMAA"
        }
    }
}

enum VPinballSharpen: CInt {
    case disabled
    case cas
    case bilateralCAS

    static let all: [VPinballSharpen] = [.disabled,
                                         .cas,
                                         .bilateralCAS]

    var name: String {
        switch self {
        case .disabled:
            return "Disabled"
        case .cas:
            return "CAS"
        case .bilateralCAS:
            return "Bilateral CAS"
        }
    }
}

enum VPinballToneMapper: CInt {
    case reinhard
    case agx
    case filmic
    case neutral
    case agxPunchy

    static let all: [VPinballToneMapper] = [.reinhard,
                                            .agx,
                                            .filmic,
                                            .neutral,
                                            .agxPunchy]

    var name: String {
        switch self {
        case .reinhard:
            return "Reinhard"
        case .agx:
            return "AgX"
        case .filmic:
            return "Filmic"
        case .neutral:
            return "Neutral"
        case .agxPunchy:
            return "AgX Punchy"
        }
    }
}

enum VPinballViewLayoutMode: CInt {
    case legacy
    case camera
    case window

    static let all: [VPinballViewLayoutMode] = [.legacy,
                                                .camera,
                                                .window]

    var name: String {
        switch self {
        case .legacy:
            return "Legacy"
        case .camera:
            return "Camera"
        case .window:
            return "Window"
        }
    }
}

enum VPinballExternalDMD: CInt {
    case none
    case dmdServer
    case zedmdWiFi

    static let all: [VPinballExternalDMD] = [.none,
                                             .dmdServer,
                                             .zedmdWiFi]

    var name: String {
        switch self {
        case .none:
            return "None"
        case .dmdServer:
            return "DMDServer"
        case .zedmdWiFi:
            return "ZeDMD WiFi"
        }
    }
}

// VPinball Event Enums

enum VPinballEvent: CInt {
    case archiveUncompressing
    case archiveCompressing
    case loadingItems
    case loadingSounds
    case loadingImages
    case loadingFonts
    case loadingCollections
    case play
    case creatingPlayer
    case windowCreated
    case prerendering
    case playerStarted
    case rumble
    case scriptError
    case liveUIToggle
    case liveUIUpdate
    case playerClosing
    case playerClosed
    case stopped
    case webServer
    case captureScreenshot
    case tableList
    case tableImport
    case tableRename
    case tableDelete
    case tableScan
    case tableScanComplete
    case refreshingTableList
    case tableListRefreshComplete
    case tableAdded
    case tableUpdated
    case tableRemoved
    case tablesJsonGenerated

    var name: String? {
        switch self {
        case .archiveUncompressing:
            return "Uncompressing"
        case .archiveCompressing:
            return "Compressing"
        case .loadingItems:
            return "Loading Items"
        case .loadingSounds:
            return "Loading Sounds"
        case .loadingImages:
            return "Loading Images"
        case .loadingFonts:
            return "Loading Fonts"
        case .loadingCollections:
            return "Loading Collections"
        case .prerendering:
            return "Prerendering Static Parts"
        case .refreshingTableList:
            return "Refreshing Tables"
        default:
            return nil
        }
    }
}

enum VPinballScriptErrorType: CInt {
    case compile
    case runtime

    var name: String {
        switch self {
        case .compile:
            return "Compile error"
        case .runtime:
            return "Runtime error"
        }
    }
}

enum VPinballOptionUnit: CInt {
    case noUnit
    case percent

    func formatValue(_ value: Float) -> String {
        switch self {
        case .noUnit:
            return String(format: "%.1f", value)
        case .percent:
            return String(format: "%.1f %%", value * 100.0)
        }
    }
}

// VPinball Touch Areas

struct VPinballTouchArea {
    let left: CGFloat
    let top: CGFloat
    let right: CGFloat
    let bottom: CGFloat
    let label: String
}

let VPinballTouchAreas: [[VPinballTouchArea]] = [
    [VPinballTouchArea(left: 50,
                       top: 0,
                       right: 100,
                       bottom: 10,
                       label: "Menu")],
    [VPinballTouchArea(left: 0,
                       top: 0,
                       right: 50,
                       bottom: 10,
                       label: "Coin")],
    [VPinballTouchArea(left: 0,
                       top: 10,
                       right: 50,
                       bottom: 30,
                       label: "Left\nMagna-Save"),
     VPinballTouchArea(left: 50,
                       top: 10,
                       right: 100,
                       bottom: 30,
                       label: "Right\nMagna-Save")],
    [VPinballTouchArea(left: 0,
                       top: 30,
                       right: 50,
                       bottom: 60,
                       label: "Left\nNudge"),
     VPinballTouchArea(left: 50,
                       top: 30,
                       right: 100,
                       bottom: 60,
                       label: "Right\nNudge"),
     VPinballTouchArea(left: 30,
                       top: 60,
                       right: 70,
                       bottom: 100,
                       label: "Center\nNudge")],
    [VPinballTouchArea(left: 0,
                       top: 60,
                       right: 30,
                       bottom: 90,
                       label: "Left\nFlipper"),
     VPinballTouchArea(left: 70,
                       top: 60,
                       right: 100,
                       bottom: 90,
                       label: "Right\nFlipper")],
    [VPinballTouchArea(left: 70,
                       top: 90,
                       right: 100,
                       bottom: 100,
                       label: "Plunger")],
    [VPinballTouchArea(left: 0,
                       top: 90,
                       right: 30,
                       bottom: 100,
                       label: "Start")],
]

// VPinball Unit Converter

enum VPinballUnitConverter {
    static func cmToVPU(_ cm: Float) -> Float {
        return cm * (50.0 / (2.54 * 1.0625))
    }

    static func vpuToCM(_ vpu: Float) -> Float {
        return vpu * (2.54 * 1.0625 / 50.0)
    }
}

// Native Swift Codable Data Structures (replaces C structs)
// Note: VPXTable is defined in VPXTable.swift

struct TableOptions: Codable {
    let globalEmissionScale: Float
    let globalDifficulty: Float
    let exposure: Float
    let toneMapper: Int
    let musicVolume: Int
    let soundVolume: Int
}

struct CustomTableOption: Codable {
    let sectionName: String
    let id: String
    let name: String
    let showMask: Int
    let minValue: Float
    let maxValue: Float
    let step: Float
    let defaultValue: Float
    let unit: Int
    let literals: String
    let value: Float
}

struct ViewSetup: Codable {
    let viewMode: Int
    let sceneScaleX: Float
    let sceneScaleY: Float
    let sceneScaleZ: Float
    let viewX: Float
    let viewY: Float
    let viewZ: Float
    let lookAt: Float
    let viewportRotation: Float
    let fov: Float
    let layback: Float
    let viewHOfs: Float
    let viewVOfs: Float
    let windowTopZOfs: Float
    let windowBottomZOfs: Float
}

// Event Data Structures
struct ProgressEventData: Codable {
    let progress: Int
}

struct RumbleData: Codable {
    let lowFrequencyRumble: UInt16
    let highFrequencyRumble: UInt16
    let durationMs: UInt32
}

struct ScreenshotEventData: Codable {
    let success: Bool
}

struct WindowCreatedData: Codable {
    let pWindow: String?
    let pTitle: String?
}

struct ScriptErrorData: Codable {
    let error: Int
    let line: Int
    let position: Int
    let description: String
}

struct WebServerData: Codable {
    let url: String
}

struct CaptureScreenshotData: Codable {
    let success: Bool
}

struct TableEventData: Codable {
    let success: Bool
    let table: VPXTable?
}

struct VPinballTableEventData: Codable {
    let tableId: String?
    let newName: String?
    let path: String?
    var success: Bool
}

// Legacy struct needed for window creation (raw data access)
struct VPinballWindowCreatedData {
    let window: Unmanaged<UIWindow>?
    let title: UnsafePointer<CChar>?
}

struct TableScanEventData: Codable {
    let tablesFound: Int?
    let tablesProcessed: Int?
    let scanComplete: Bool?
    let currentTable: String?
}

// VPinball Callbacks (hybrid approach: JSON + void pointer)

typealias VPinballEventCallback = @convention(c) (CInt, UnsafePointer<CChar>?, UnsafeRawPointer?) -> UnsafeRawPointer?

// VPinball C Definitions

@_silgen_name("VPinballInit")
func VPinballInit(_ callback: VPinballEventCallback)

@_silgen_name("VPinballLog")
func VPinballLog(_ level: CInt, _ pMessage: UnsafePointer<CChar>)

@_silgen_name("VPinballResetLog")
func VPinballResetLog()

@_silgen_name("VPinballSetWebServerUpdated")
func VPinballSetWebServerUpdated()

@_silgen_name("VPinballLoadValueInt")
func VPinballLoadValueInt(_ section: UnsafePointer<CChar>, _ pKey: UnsafePointer<CChar>, _ defaultValue: CInt) -> CInt

@_silgen_name("VPinballLoadValueFloat")
func VPinballLoadValueFloat(_ section: UnsafePointer<CChar>, _ pKey: UnsafePointer<CChar>, _ defaultValue: Float) -> Float

@_silgen_name("VPinballLoadValueString")
func VPinballLoadValueString(_ section: UnsafePointer<CChar>, _ pKey: UnsafePointer<CChar>, _ defaultValue: UnsafePointer<CChar>) -> UnsafePointer<CChar>

@_silgen_name("VPinballSaveValueInt")
func VPinballSaveValueInt(_ section: UnsafePointer<CChar>, _ pKey: UnsafePointer<CChar>, _ value: CInt)

@_silgen_name("VPinballSaveValueFloat")
func VPinballSaveValueFloat(_ section: UnsafePointer<CChar>, _ pKey: UnsafePointer<CChar>, _ value: Float)

@_silgen_name("VPinballSaveValueString")
func VPinballSaveValueString(_ section: UnsafePointer<CChar>, _ pKey: UnsafePointer<CChar>, _ value: UnsafePointer<CChar>)

@_silgen_name("VPinballGetVersionStringFull")
func VPinballGetVersionStringFull() -> UnsafePointer<CChar>

@_silgen_name("VPinballExportTable")
func VPinballExportTable(_ pUuid: UnsafePointer<CChar>) -> UnsafePointer<CChar>?

@_silgen_name("VPinballUpdateWebServer")
func VPinballUpdateWebServer()

@_silgen_name("VPinballResetIni")
func VPinballResetIni() -> CInt

@_silgen_name("VPinballLoad")
func VPinballLoad(_ pSource: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballExtractScript")
func VPinballExtractScript(_ pSource: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballPlay")
func VPinballPlay() -> CInt

@_silgen_name("VPinballStop")
func VPinballStop()

@_silgen_name("VPinballSetPlayState")
func VPinballSetPlayState(_ enable: CInt) -> CInt

@_silgen_name("VPinballGetTableOptions")
func VPinballGetTableOptions() -> UnsafePointer<CChar>?

@_silgen_name("VPinballSetTableOptions")
func VPinballSetTableOptions(_ jsonOptions: UnsafePointer<CChar>)

@_silgen_name("VPinballSetDefaultTableOptions")
func VPinballSetDefaultTableOptions()

@_silgen_name("VPinballResetTableOptions")
func VPinballResetTableOptions()

@_silgen_name("VPinballSaveTableOptions")
func VPinballSaveTableOptions()

@_silgen_name("VPinballGetCustomTableOptionsCount")
func VPinballGetCustomTableOptionsCount() -> CInt

@_silgen_name("VPinballGetCustomTableOptions")
func VPinballGetCustomTableOptions() -> UnsafePointer<CChar>?

@_silgen_name("VPinballSetCustomTableOption")
func VPinballSetCustomTableOption(_ jsonOption: UnsafePointer<CChar>)

@_silgen_name("VPinballSetDefaultCustomTableOptions")
func VPinballSetDefaultCustomTableOptions()

@_silgen_name("VPinballResetCustomTableOptions")
func VPinballResetCustomTableOptions()

@_silgen_name("VPinballSaveCustomTableOptions")
func VPinballSaveCustomTableOptions()

@_silgen_name("VPinballGetViewSetup")
func VPinballGetViewSetup() -> UnsafePointer<CChar>?

@_silgen_name("VPinballSetViewSetup")
func VPinballSetViewSetup(_ jsonSetup: UnsafePointer<CChar>)

@_silgen_name("VPinballSetDefaultViewSetup")
func VPinballSetDefaultViewSetup()

@_silgen_name("VPinballResetViewSetup")
func VPinballResetViewSetup()

@_silgen_name("VPinballSaveViewSetup")
func VPinballSaveViewSetup()

@_silgen_name("VPinballToggleFPS")
func VPinballToggleFPS()

@_silgen_name("VPinballCaptureScreenshot")
func VPinballCaptureScreenshot(_ filename: UnsafePointer<CChar>)

// VPXTable Management Functions
@_silgen_name("VPinballRefreshTables")
func VPinballRefreshTables() -> CInt

@_silgen_name("VPinballGetVPXTables")
func VPinballGetVPXTables() -> UnsafePointer<CChar>

@_silgen_name("VPinballGetVPXTable")
func VPinballGetVPXTable(_ uuid: UnsafePointer<CChar>) -> UnsafePointer<CChar>

@_silgen_name("VPinballAddVPXTable")
func VPinballAddVPXTable(_ filePath: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballRemoveVPXTable")
func VPinballRemoveVPXTable(_ uuid: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballRenameVPXTable")
func VPinballRenameVPXTable(_ uuid: UnsafePointer<CChar>, _ newName: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballSetTableArtwork")
func VPinballSetTableArtwork(_ uuid: UnsafePointer<CChar>, _ artworkPath: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballGetTablesPath")
func VPinballGetTablesPath() -> UnsafePointer<CChar>

@_silgen_name("VPinballImportTableFile")
func VPinballImportTableFile(_ sourceFile: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballFreeString")
func VPinballFreeString(_ jsonString: UnsafeMutablePointer<CChar>)
