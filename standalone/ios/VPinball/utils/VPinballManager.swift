
import Combine
import SwiftData
import SwiftUI

class VPinballManager {
    enum ScreenshotMode {
        case artwork
        case quit
    }

    static let shared = VPinballManager()
    var sdlUIWindow: UIWindow?
    var sdlViewController: UIViewController?
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

        // Tables path is now set automatically by VPinballTableManager

        VPinballInit { value, jsonData, rawData in
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
                 .prerendering,
                 .refreshingTableList:
                // Parse JSON progress data
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    if let jsonDataBytes = jsonString.data(using: .utf8),
                       let progressData = try? JSONDecoder().decode(ProgressEventData.self, from: jsonDataBytes)
                    {
                        DispatchQueue.main.async {
                            if let name = event?.name {
                                vpinballViewModel.updateProgressHUD(progress: progressData.progress,
                                                                    status: name)
                            } else {
                                vpinballViewModel.updateProgressHUD(progress: progressData.progress)
                            }
                        }
                    }
                }
            case .windowCreated:
                // Use the raw data pointer to get the actual window
                if let rawData = rawData {
                    // Cast to the WindowCreatedData struct
                    let windowCreatedData = rawData.bindMemory(to: VPinballWindowCreatedData.self, capacity: 1).pointee

                    if let window = windowCreatedData.window?.takeUnretainedValue() {
                        if let viewController = window.rootViewController {
                            let overlayView = OverlayView()
                                .environmentObject(vpinballViewModel)

                            let overlayViewHostingController = UIHostingController(rootView: overlayView)
                            overlayViewHostingController.view.backgroundColor = .clear
                            overlayViewHostingController.view.translatesAutoresizingMaskIntoConstraints = false
                            overlayViewHostingController.view.frame = viewController.view.bounds
                            overlayViewHostingController.view.isMultipleTouchEnabled = true
                            viewController.view.addSubview(overlayViewHostingController.view)
                            viewController.addChild(overlayViewHostingController)

                            vpinballManager.sdlUIWindow = window
                            vpinballManager.sdlViewController = viewController
                        }

                        window.isHidden = true
                        window.windowScene = UIApplication.shared.connectedScenes.first as? UIWindowScene
                    }
                }

                vpinballViewModel.liveUIOverride = false
                vpinballViewModel.showTouchInstructions = false
                vpinballViewModel.showTouchOverlay = false
            case .playerStarted:
                vpinballManager.sdlUIWindow?.isHidden = false

                vpinballViewModel.liveUIOverride = vpinballManager.loadValue(.standalone,
                                                                             "LiveUIOverride",
                                                                             true)

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
            case .liveUIToggle:
                vpinballViewModel.showLiveUI.toggle()

                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                    vpinballManager.setPlayState(enable: !vpinballViewModel.showLiveUI)
                }
            case .liveUIUpdate:
                break
            case .playerClosing:
                break
            case .playerClosed:
                break
            case .stopped:
                vpinballManager.activeTable = nil
                vpinballManager.sdlUIWindow = nil
                vpinballManager.sdlViewController = nil

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
            case .tableListRefreshComplete:
                vpinballManager.log(.info, "Received tableListRefreshComplete event - refreshing iOS table list")
                DispatchQueue.main.async {
                    vpinballViewModel.hideHUD()
                    // Load the updated table list (refresh was already done by web server)
                    Task {
                        await VPXTableManager.shared.loadTables()
                    }
                }
            case .tableList:
                if let jsonData = jsonData {
                    return vpinballManager.handleTableList(jsonData: UnsafePointer<CChar>(jsonData))
                }
            case .tableImport:
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    vpinballManager.handleTableImport(jsonString: jsonString)
                }
            case .tableRename:
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    vpinballManager.handleTableRename(jsonString: jsonString)
                }
            case .tableDelete:
                if let jsonData = jsonData {
                    let jsonString = String(cString: UnsafePointer<CChar>(jsonData))
                    vpinballManager.handleTableDelete(jsonString: jsonString)
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
                    return VPinballImportTableFile(tempURL.path.cstring)
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

        if await Task.detached(
            priority: .userInitiated,
            operation: { [table] in
                VPinballStatus(rawValue: VPinballExtractScript(table.fullPath.cstring))
            }
        ).value != .success {
            success = false

            log(.error, "unable to extract script table")
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

    func getTableOptions() -> TableOptions? {
        guard let jsonPtr = VPinballGetTableOptions() else { return nil }
        let jsonString = String(cString: jsonPtr)
        VPinballFreeString(UnsafeMutablePointer(mutating: jsonPtr))

        guard let jsonData = jsonString.data(using: .utf8) else { return nil }
        return try? JSONDecoder().decode(TableOptions.self, from: jsonData)
    }

    func setTableOptions(_ tableOptions: TableOptions) {
        guard let jsonData = try? JSONEncoder().encode(tableOptions),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }

        VPinballSetTableOptions(jsonString)
    }

    func setDefaultTableOptions() {
        VPinballSetDefaultTableOptions()
    }

    func resetTableOptions() {
        VPinballResetTableOptions()
    }

    func saveTableOptions() {
        VPinballSaveTableOptions()
    }

    func getCustomTableOptions() -> [CustomTableOption] {
        guard let jsonPtr = VPinballGetCustomTableOptions() else { return [] }
        let jsonString = String(cString: jsonPtr)
        VPinballFreeString(UnsafeMutablePointer(mutating: jsonPtr))

        guard let jsonData = jsonString.data(using: .utf8),
              let options = try? JSONDecoder().decode([CustomTableOption].self, from: jsonData) else { return [] }

        return options
    }

    func setCustomTableOption(_ customTableOption: CustomTableOption) {
        guard let jsonData = try? JSONEncoder().encode(customTableOption),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }

        VPinballSetCustomTableOption(jsonString)
    }

    func setDefaultCustomTableOptions() {
        VPinballSetDefaultCustomTableOptions()
    }

    func resetCustomTableOptions() {
        VPinballResetCustomTableOptions()
    }

    func saveCustomTableOptions() {
        VPinballSaveCustomTableOptions()
    }

    func getViewSetup() -> ViewSetup? {
        guard let jsonPtr = VPinballGetViewSetup() else { return nil }
        let jsonString = String(cString: jsonPtr)
        VPinballFreeString(UnsafeMutablePointer(mutating: jsonPtr))

        guard let jsonData = jsonString.data(using: .utf8) else { return nil }
        return try? JSONDecoder().decode(ViewSetup.self, from: jsonData)
    }

    func setViewSetup(_ viewSetup: ViewSetup) {
        guard let jsonData = try? JSONEncoder().encode(viewSetup),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }

        VPinballSetViewSetup(jsonString)
    }

    func setDefaultViewSetup() {
        VPinballSetDefaultViewSetup()
    }

    func resetViewSetup() {
        VPinballResetViewSetup()
    }

    func saveViewSetup() {
        VPinballSaveViewSetup()
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

    func setPlayState(enable: Bool) {
        _ = VPinballSetPlayState(enable ? 1 : 0)
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

    func handleTableList(jsonData _: UnsafePointer<CChar>) -> UnsafeRawPointer? {
        // Get VPX tables as JSON from C++ library
        let jsonStringPtr = VPinballGetVPXTables()
        let jsonString = String(cString: jsonStringPtr)
        VPinballFreeString(UnsafeMutablePointer(mutating: jsonStringPtr))

        // Parse JSON response
        if let jsonData = jsonString.data(using: .utf8),
           let tablesResponse = try? JSONDecoder().decode(VPXTablesResponse.self, from: jsonData)
        {
            // Convert to legacy format for backward compatibility with web server
            let legacyJsonString = "{\"success\":\(tablesResponse.success),\"tables\":[" +
                tablesResponse.tables.map { table in
                    "{\"tableId\":\"\(table.uuid)\",\"name\":\"\(table.name)\"}"
                }.joined(separator: ",") + "]}"

            let legacyCString = legacyJsonString.cString(using: .utf8)!
            let resultPtr = UnsafeMutablePointer<CChar>.allocate(capacity: legacyCString.count)
            resultPtr.initialize(from: legacyCString, count: legacyCString.count)

            return UnsafeRawPointer(resultPtr)
        }

        // Return empty result on error
        let errorJson = "{\"success\":false,\"tables\":[]}"
        let errorCString = errorJson.cString(using: .utf8)!
        let errorPtr = UnsafeMutablePointer<CChar>.allocate(capacity: errorCString.count)
        errorPtr.initialize(from: errorCString, count: errorCString.count)

        return UnsafeRawPointer(errorPtr)
    }

    func handleTableImport(jsonString: String) {
        guard let jsonData = jsonString.data(using: .utf8),
              let eventData = try? JSONDecoder().decode(VPinballTableEventData.self, from: jsonData)
        else {
            log(.error, "Failed to decode table import event data")
            return
        }

        let fileName = eventData.path?.split(separator: "/").last ?? "unknown"
        log(.info, "Table import event received for: \(fileName)")

        // The table has already been imported by the C++ code that sent this event
        // We just need to update the web server and potentially refresh UI
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            VPinballSetWebServerUpdated()
        }
    }

    func handleTableRename(jsonString: String) {
        guard let jsonData = jsonString.data(using: .utf8),
              let eventData = try? JSONDecoder().decode(VPinballTableEventData.self, from: jsonData)
        else {
            log(.error, "Failed to decode table rename event data")
            return
        }

        guard let tableIdString = eventData.tableId,
              let newName = eventData.newName
        else {
            log(.error, "Failed to decode table rename event: invalid parameters")
            return
        }

        log(.info, "Table rename event received for: \(tableIdString) -> \(newName)")

        // The table has already been renamed by the C++ code that sent this event
        // We just need to update the web server and potentially refresh UI
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            VPinballSetWebServerUpdated()
        }
    }

    func handleTableDelete(jsonString: String) {
        guard let jsonData = jsonString.data(using: .utf8),
              let eventData = try? JSONDecoder().decode(VPinballTableEventData.self, from: jsonData)
        else {
            log(.error, "Failed to decode table delete event data")
            return
        }

        guard let tableIdString = eventData.tableId else {
            log(.error, "Failed to decode table delete event: invalid table ID")
            return
        }

        log(.info, "Table delete event received for: \(tableIdString)")

        // The table has already been deleted by the C++ code that sent this event
        // We just need to update the web server and potentially refresh UI
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            VPinballSetWebServerUpdated()
        }
    }
}
