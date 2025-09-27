
import Combine
import SwiftData
import SwiftUI

class VPinballManager {
    enum ScreenshotMode {
        case artwork
        case quit
    }

    static let shared = VPinballManager()
    var haptics = false
    var activeTable: VPXTable?
    var memoryWarningSubscription: AnyCancellable?
    var screenshotMode: ScreenshotMode?

    let impactGenerators: [UIImpactFeedbackGenerator.FeedbackStyle: UIImpactFeedbackGenerator] = [
        .heavy: UIImpactFeedbackGenerator(style: .heavy),
        .light: UIImpactFeedbackGenerator(style: .light),
        .medium: UIImpactFeedbackGenerator(style: .medium),
        .rigid: UIImpactFeedbackGenerator(style: .rigid),
        .soft: UIImpactFeedbackGenerator(style: .soft),
    ]

    let vpinballViewModel = VPinballViewModel.shared

    private init() {
        impactGenerators.forEach { $0.value.prepare() }

        VPinballSetEventCallback { value, jsonData, _ in
            let vpinballManager = VPinballManager.shared
            let vpinballViewModel = VPinballViewModel.shared
            let event = VPinballEvent(rawValue: value)
            switch event {
            case .archiveCompressing,
                 .archiveUncompressing,
                 .loadingItems,
                 .loadingSounds,
                 .loadingImages,
                 .loadingFonts,
                 .loadingCollections,
                 .prerendering:
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    if let jsonDataBytes = jsonString.data(using: .utf8),
                       let progressData = try? JSONDecoder().decode(ProgressEventData.self, from: jsonDataBytes)
                    {
                        let apply = {
                            if let name = event?.name {
                                vpinballViewModel.updateProgressHUD(progress: progressData.progress,
                                                                    status: name)
                            } else {
                                vpinballViewModel.updateProgressHUD(progress: progressData.progress)
                            }
                        }

                        if Thread.isMainThread {
                            apply()
                            CATransaction.flush()
                            RunLoop.main.run(mode: .default,
                                             before: Date().addingTimeInterval(0))
                        } else {
                            DispatchQueue.main.async {
                                apply()
                            }
                        }
                    }
                }
            case .playerStarted:
                vpinballViewModel.isPlaying = true

                vpinballViewModel.showTouchInstructions = vpinballManager.loadValue(.standalone,
                                                                                    "TouchInstructions",
                                                                                    true)

                vpinballViewModel.showTouchOverlay = vpinballManager.loadValue(.standalone,
                                                                               "TouchOverlay",
                                                                               false)
            case .rumble:
                if vpinballManager.haptics,
                   let jsonData = jsonData
                {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    if let jsonDataBytes = jsonString.data(using: .utf8),
                       let rumbleData = try? JSONDecoder().decode(RumbleData.self, from: jsonDataBytes)
                    {
                        vpinballManager.rumble(rumbleData)
                    }
                }
            case .scriptError:
                if vpinballViewModel.scriptError == nil,
                   let jsonData = jsonData
                {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    if let jsonDataBytes = jsonString.data(using: .utf8),
                       let scriptErrorData = try? JSONDecoder().decode(ScriptErrorData.self, from: jsonDataBytes)
                    {
                        let type = VPinballScriptErrorType(rawValue: CInt(scriptErrorData.error))!
                        vpinballViewModel.scriptError = "\(type.name) on line \(scriptErrorData.line), position \(scriptErrorData.position):\n\n\(scriptErrorData.description)"
                    }
                }
            case .playerClosing:
                break
            case .playerClosed:
                vpinballViewModel.isPlaying = false
            case .stopped:
                vpinballManager.activeTable = nil
                vpinballViewModel.isPlaying = false
                vpinballViewModel.setAction(action: .stopped)
            case .webServer:
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    if let jsonDataBytes = jsonString.data(using: .utf8),
                       let webServerData = try? JSONDecoder().decode(WebServerData.self, from: jsonDataBytes)
                    {
                        DispatchQueue.main.async {
                            vpinballViewModel.webServerURL = webServerData.url
                        }
                    }
                } else {
                    DispatchQueue.main.async {
                        vpinballViewModel.webServerURL = nil
                    }
                }
            case .captureScreenshot:
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    if let jsonDataBytes = jsonString.data(using: .utf8),
                       let captureScreenshotData = try? JSONDecoder().decode(CaptureScreenshotData.self, from: jsonDataBytes)
                    {
                        if let table = vpinballManager.activeTable {
                            if captureScreenshotData.success {
                                DispatchQueue.main.async {
                                    // Table updated - refresh web server
                                    VPinballSetWebServerUpdated()
                                }
                            }

                            switch vpinballManager.screenshotMode {
                            case .artwork:
                                DispatchQueue.main.async {
                                    vpinballViewModel.artworkImage = table.uiImage
                                }
                            case .quit:
                                vpinballManager.stop()
                            default:
                                break
                            }
                        }
                    }
                }
            case .tableListUpdated:
                vpinballManager.log(.info, "Received tableListUpdated event - refreshing iOS table list")

                // Check if there's a focusUuid in the JSON
                var focusUuid: UUID?
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    if let jsonDataBytes = jsonString.data(using: .utf8) {
                        struct TableListUpdatedData: Codable {
                            let focusUuid: String?
                        }
                        if let data = try? JSONDecoder().decode(TableListUpdatedData.self, from: jsonDataBytes),
                           let uuidString = data.focusUuid
                        {
                            focusUuid = UUID(uuidString: uuidString)
                        }
                    }
                }

                DispatchQueue.main.async {
                    vpinballViewModel.hideHUD()
                    Task {
                        await VPXTableManager.shared.loadTables()

                        // If there's a focusUuid, scroll to it
                        if let uuid = focusUuid {
                            vpinballViewModel.scrollToTableId = uuid
                        }
                    }
                }
            default:
                break
            }

            return nil
        }

        memoryWarningSubscription = NotificationCenter.default
            .publisher(for: UIApplication.didReceiveMemoryWarningNotification)
            .sink { [weak self] _ in
                guard let self = self else { return }

                if !self.vpinballViewModel.didReceiveMemoryWarning {
                    self.vpinballViewModel.didReceiveMemoryWarning = true

                    self.log(.warn, "Low memory warning notification received")

                    if self.loadValue(.standalone, "LowMemoryNotice", -1) == -1 {
                        self.saveValue(.standalone, "LowMemoryNotice", 1)
                    }
                }
            }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            self.updateWebServer()

            if self.loadValue(.standalone, "LowMemoryNotice", -1) == 1 {
                self.vpinballViewModel.showLowMemoryNotice = true
            }
        }
    }

    func log(_ level: VPinballLogLevel, _ message: String) {
        VPinballLog(level.rawValue, message.cstring)
    }

    func loadValue(_ section: VPinballSettingsSection, _ key: String, _ defaultValue: CInt) -> CInt {
        return VPinballLoadValueInt(section.rawValue.cstring, key.cstring, defaultValue)
    }

    func loadValue(_ section: VPinballSettingsSection, _ key: String, _ defaultValue: Int) -> Int {
        return Int(loadValue(section, key, CInt(defaultValue)))
    }

    func loadValue(_ section: VPinballSettingsSection, _ key: String, _ defaultValue: Float) -> Float {
        return VPinballLoadValueFloat(section.rawValue.cstring, key.cstring, defaultValue)
    }

    func loadValue(_ section: VPinballSettingsSection, _ key: String, _ defaultValue: Bool) -> Bool {
        return loadValue(section, key, defaultValue ? 1 : 0) == 1
    }

    func loadValue(_ section: VPinballSettingsSection, _ key: String, _ defaultValue: String) -> String {
        return String(cString: VPinballLoadValueString(section.rawValue.cstring, key.cstring, defaultValue.cstring))
    }

    func saveValue(_ section: VPinballSettingsSection, _ key: String, _ value: CInt) {
        VPinballSaveValueInt(section.rawValue.cstring, key.cstring, value)
    }

    func saveValue(_ section: VPinballSettingsSection, _ key: String, _ value: Int) {
        saveValue(section, key, CInt(value))
    }

    func saveValue(_ section: VPinballSettingsSection, _ key: String, _ value: Float) {
        VPinballSaveValueFloat(section.rawValue.cstring, key.cstring, value)
    }

    func saveValue(_ section: VPinballSettingsSection, _ key: String, _ value: Bool) {
        saveValue(section, key, value ? 1 : 0)
    }

    func saveValue(_ section: VPinballSettingsSection, _ key: String, _ value: String) {
        VPinballSaveValueString(section.rawValue.cstring, key.cstring, value.cstring)
    }

    func `import`(url: URL) async {
        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: url.lastPathComponent.removingPercentEncoding!,
                                              status: url.pathExtension.lowercased() == "vpx" ? "Importing" : "Importing Archive")
        }

        do {
            let fileManager = FileManager.default

            let isSecurityScoped = url.startAccessingSecurityScopedResource()
            defer {
                if isSecurityScoped {
                    url.stopAccessingSecurityScopedResource()
                }
            }

            let tempURL = fileManager.temporaryDirectory.appendingPathComponent(url.lastPathComponent)

            if fileManager.fileExists(atPath: tempURL.path) {
                try fileManager.removeItem(at: tempURL)
            }

            try fileManager.copyItem(at: url, to: tempURL)

            let importResult = await Task.detached(
                priority: .userInitiated,
                operation: { [tempURL] in
                    return VPinballImportTable(tempURL.path.cstring)
                }
            ).value

            let success = (importResult == VPinballStatus.success.rawValue)

            try? fileManager.removeItem(at: tempURL)

            if success {
                log(.info, "Successfully imported table: \(url.lastPathComponent)")

                await Task.detached {
                    _ = VPinballRefreshTables()
                }.value

                await MainActor.run { vpinballViewModel.hideHUD() }
                return
            } else {
                log(.error, "Failed to import table: \(url.lastPathComponent)")
            }
        } catch {
            log(.error, "Unable to import: error=\(error)")
        }

        await MainActor.run { vpinballViewModel.hideHUD() }
    }

    func share(table: VPXTable) async -> URL? {
        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: table.name,
                                              status: "Compressing")
        }

        let exportPath = await Task<String?, Never>.detached(
            priority: .userInitiated,
            operation: { [table] in
                guard let pathPtr = VPinballExportTable(table.uuid.cstring) else {
                    return nil
                }
                return String(cString: pathPtr)
            }
        ).value

        await MainActor.run { vpinballViewModel.hideHUD() }

        if let exportPath = exportPath {
            return URL(fileURLWithPath: exportPath)
        } else {
            log(.error, "Failed to export table: \(table.name)")
            return nil
        }
    }

    func extractScript(table: VPXTable) async -> Bool {
        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: table.name,
                                              status: "Extracting")
        }

        var success = true

        // First load the table, then extract script from the loaded table
        if await Task.detached(
            priority: .userInitiated,
            operation: { [table] in
                // Load the table first
                guard VPinballStatus(rawValue: VPinballLoad(table.uuid.uuidString.cstring)) == .success else {
                    return VPinballStatus.failure
                }
                // Then extract script from the loaded table
                return VPinballStatus(rawValue: VPinballExtractScript())
            }
        ).value != .success {
            success = false

            log(.error, "unable to extract script from table")
        }

        await MainActor.run {
            vpinballViewModel.hideHUD()
        }

        return success
    }

    func play(table: VPXTable) async -> Bool {
        if activeTable != nil {
            return false
        }

        if loadValue(.standalone, "ResetLogOnPlay", true) {
            VPinballResetLog()
        }

        activeTable = table
        vpinballViewModel.scriptError = nil

        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: table.name,
                                              status: "Launching")
        }

        haptics = loadValue(.standalone, "Haptics", true)

        var success = true

        if await Task.detached(
            priority: .userInitiated,
            operation: { [table] in
                VPinballStatus(rawValue: VPinballLoad(table.fullPath.cstring))
            }
        ).value == .success {
            if await MainActor.run(body: { VPinballStatus(rawValue: VPinballPlay()) }) != .success {
                try? await Task.sleep(nanoseconds: 500_000_000)

                activeTable = nil

                success = false

                log(.error, "unable to start table")
            }
        } else {
            try? await Task.sleep(nanoseconds: 500_000_000)

            activeTable = nil

            success = false

            log(.error, "unable to load table")
        }

        await MainActor.run {
            vpinballViewModel.hideHUD()
        }

        return success
    }

    func rumble(_ data: RumbleData) {
        if data.lowFrequencyRumble > 0 || data.highFrequencyRumble > 0 {
            let style: UIImpactFeedbackGenerator.FeedbackStyle

            if data.lowFrequencyRumble == data.highFrequencyRumble {
                style = .rigid
            } else if data.lowFrequencyRumble > 20000 || data.highFrequencyRumble > 20000 {
                style = .heavy
            } else if data.lowFrequencyRumble > 10000 || data.highFrequencyRumble > 10000 {
                style = .medium
            } else if data.lowFrequencyRumble > 1000 || data.highFrequencyRumble > 1000 {
                style = .light
            } else {
                style = .soft
            }

            impactGenerators[style]!.impactOccurred()
        }
    }

    func hasScreenshot() -> Bool {
        return activeTable?.hasImageFile() ?? false
    }

    func captureScreenshot(mode: ScreenshotMode) {
        if let table = activeTable {
            screenshotMode = mode
            VPinballCaptureScreenshot(table.imagePath)
        }
    }

    func toggleFPS() {
        VPinballToggleFPS()
    }

    func stop() {
        VPinballStop()
    }

    func resetIni() {
        _ = VPinballResetIni()
    }

    func updateWebServer() {
        VPinballUpdateWebServer()
    }
}
