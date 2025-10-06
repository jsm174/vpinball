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
    case prerendering
    case playerStarted
    case rumble
    case scriptError
    case playerClosed
    case webServer
    case tableListUpdated

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

// Event Data Structures

struct ProgressEventData: Codable {
    let progress: Int
}

struct RumbleData: Codable {
    let lowFrequencyRumble: UInt16
    let highFrequencyRumble: UInt16
    let durationMs: UInt32
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

// VPinball Callbacks (hybrid approach: JSON)

typealias VPinballEventCallback = @convention(c) (CInt, UnsafePointer<CChar>?) -> Void

// VPinball C Definitions

@_silgen_name("VPinballGetVersionStringFull")
func VPinballGetVersionStringFull() -> UnsafePointer<CChar>

@_silgen_name("VPinballStartup")
func VPinballStartup(_ callback: VPinballEventCallback)

@_silgen_name("VPinballLog")
func VPinballLog(_ level: CInt, _ pMessage: UnsafePointer<CChar>)

@_silgen_name("VPinballResetLog")
func VPinballResetLog()

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

@_silgen_name("VPinballResetIni")
func VPinballResetIni() -> CInt

@_silgen_name("VPinballUpdateWebServer")
func VPinballUpdateWebServer()

@_silgen_name("VPinballLoadTable")
func VPinballLoadTable(_ pSource: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballExtractTableScript")
func VPinballExtractTableScript() -> CInt

@_silgen_name("VPinballPlay")
func VPinballPlay() -> CInt

@_silgen_name("VPinballStop")
func VPinballStop()

@_silgen_name("VPinballGetTables")
func VPinballGetTables() -> UnsafePointer<CChar>

@_silgen_name("VPinballGetTablesPath")
func VPinballGetTablesPath() -> UnsafePointer<CChar>

@_silgen_name("VPinballFileExists")
func VPinballFileExists(_ pPath: UnsafePointer<CChar>) -> Bool

@_silgen_name("VPinballDeleteFile")
func VPinballDeleteFile(_ pPath: UnsafePointer<CChar>) -> Bool

@_silgen_name("VPinballCopyFile")
func VPinballCopyFile(_ pSourcePath: UnsafePointer<CChar>, _ pDestPath: UnsafePointer<CChar>) -> Bool

@_silgen_name("VPinballPrepareFileForViewing")
func VPinballPrepareFileForViewing(_ pPath: UnsafePointer<CChar>) -> UnsafePointer<CChar>?

@_silgen_name("VPinballImportTable")
func VPinballImportTable(_ pSourceFile: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballExportTable")
func VPinballExportTable(_ pUuid: UnsafePointer<CChar>) -> UnsafePointer<CChar>?

@_silgen_name("VPinballDeleteTable")
func VPinballDeleteTable(_ uuid: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballRenameTable")
func VPinballRenameTable(_ uuid: UnsafePointer<CChar>, _ newName: UnsafePointer<CChar>) -> CInt

@_silgen_name("VPinballSetTableImage")
func VPinballSetTableImage(_ uuid: UnsafePointer<CChar>, _ imagePath: UnsafePointer<CChar>) -> CInt
