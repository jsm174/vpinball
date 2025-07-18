
import Combine
import SwiftData
import SwiftUI

class VPinballManager {
    enum ScreenshotMode {
        case artwork
        case quit
    }

    static let shared = VPinballManager()

    var modelContext: ModelContext?
    var sdlUIWindow: UIWindow?
    var sdlViewController: UIViewController?
    var haptics = false
    var activeTable: PinTable?
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

        VPinballInit { value, data in
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
                let progressData = data!.bindMemory(to: VPinballProgressData.self,
                                                    capacity: 1).pointee

                DispatchQueue.main.async {
                    if let name = event?.name {
                        vpinballViewModel.updateProgressHUD(progress: Int(progressData.progress),
                                                            status: name)
                    } else {
                        vpinballViewModel.updateProgressHUD(progress: Int(progressData.progress))
                    }
                }
            case .windowCreated:
                let windowCreatedData = data!.bindMemory(to: VPinballWindowCreatedData.self,
                                                         capacity: 1).pointee

                vpinballViewModel.liveUIOverride = false
                vpinballViewModel.showTouchInstructions = false
                vpinballViewModel.showTouchOverlay = false

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
                if vpinballManager.haptics {
                    vpinballManager.rumble(data!.bindMemory(to: VPinballRumbleData.self,
                                                            capacity: 1).pointee)
                }
            case .scriptError:
                if vpinballViewModel.scriptError == nil {
                    let scriptErrorData = data!.bindMemory(to: VPinballScriptErrorData.self,
                                                           capacity: 1).pointee

                    let type = VPinballScriptErrorType(rawValue: scriptErrorData.error)!
                    let description = scriptErrorData.description.map { String(cString: $0) } ?? "Description unavailable"

                    vpinballViewModel.scriptError = "\(type.name) on line \(scriptErrorData.line), position \(scriptErrorData.position):\n\n\(description)"
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
                if let data = data {
                    let webServerData = data.bindMemory(to: VPinballWebServerData.self,
                                                        capacity: 1).pointee

                    let url = webServerData.url.map { String(cString: $0) }

                    DispatchQueue.main.async {
                        vpinballViewModel.webServerURL = url
                    }
                } else {
                    DispatchQueue.main.async {
                        vpinballViewModel.webServerURL = nil
                    }
                }
            case .captureScreenshot:
                if let data = data {
                    let captureScreenshotData = data.bindMemory(to: VPinballCaptureScreenshotData.self,
                                                                capacity: 1).pointee

                    if let table = vpinballManager.activeTable {
                        if captureScreenshotData.success {
                            DispatchQueue.main.async {
                                PinTable.update(table: table)
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
            case .tableList:
                if let data = data {
                    return vpinballManager.handleTableList(data: data)
                }
            case .tableImport:
                if let data = data {
                    vpinballManager.handleTableImport(data: data)
                }
            case .tableRename:
                if let data = data {
                    vpinballManager.handleTableRename(data: data)
                }
            case .tableDelete:
                if let data = data {
                    vpinballManager.handleTableDelete(data: data)
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

    func `import`(url: URL) async -> PinTable? {
        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: url.lastPathComponent.removingPercentEncoding!,
                                              status: url.pathExtension.lowercased() == "vpx" ? "Copying" : "Uncompressing")
        }

        do {
            let fileManager = FileManager.default

            if url.pathExtension.lowercased() == "vpx" {
                // Create documents/uuid
                // Copy vpx file to documents/uuid

                let table = PinTable(url: url)
                try fileManager.createDirectory(atPath: table.basePath,
                                                withIntermediateDirectories: true,
                                                attributes: nil)

                let destURL = table.baseURL.appendingPathComponent(url.lastPathComponent)

                try fileManager.copyItem(atPath: url.path,
                                         toPath: destURL.path)

                await MainActor.run { vpinballViewModel.hideHUD() }
                return table
            } else {
                // Create tmp/stage
                // Copy file to tmp/stage
                // Uncompress file.vpxz (or file.zip) in tmp/stage
                // Delete file.vpxz (or file.zip)
                // Find first .vpx file
                // Copy .vpx file's parent folder to documents/uuid

                let stageBaseURL = fileManager.temporaryDirectory.appendingPathComponent("stage")

                if fileManager.fileExists(atPath: stageBaseURL.path) {
                    try fileManager.removeItem(atPath: stageBaseURL.path)
                }

                try fileManager.createDirectory(atPath: stageBaseURL.path,
                                                withIntermediateDirectories: true,
                                                attributes: nil)

                let stageURL = stageBaseURL.appendingPathComponent(url.lastPathComponent)

                try fileManager.copyItem(atPath: url.path,
                                         toPath: stageURL.path)

                let success = await Task.detached(
                    priority: .userInitiated,
                    operation: { [stageURL] in
                        VPinballStatus(rawValue: VPinballUncompress(stageURL.path.cstring))
                    }
                ).value == .success

                try fileManager.removeItem(atPath: stageURL.path)

                if success {
                    if let enumerator = fileManager.enumerator(at: stageBaseURL,
                                                               includingPropertiesForKeys: nil,
                                                               options: [.skipsHiddenFiles],
                                                               errorHandler: nil)
                    {
                        for case let fileURL as URL in enumerator {
                            if fileURL.pathExtension.lowercased() == "vpx" {
                                let table = PinTable(url: fileURL)

                                try fileManager.copyItem(atPath: fileURL.deletingLastPathComponent().path,
                                                         toPath: table.basePath)

                                await MainActor.run { vpinballViewModel.hideHUD() }
                                return table
                            }
                        }
                    }
                }
            }
        }

        catch {
            log(.error, "unable to import: error=\(error)")
        }

        await MainActor.run { vpinballViewModel.hideHUD() }
        return nil
    }

    func share(table: PinTable) async -> URL? {
        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: table.name,
                                              status: "Compressing")
        }

        do {
            let fileManager = FileManager.default
            let name = table.name.replacingOccurrences(of: "[ ]", with: "_", options: .regularExpression)
            let url = fileManager.temporaryDirectory.appendingPathComponent("\(name).vpxz")

            if fileManager.fileExists(atPath: url.path) {
                try fileManager.removeItem(atPath: url.path)
            }

            if await Task.detached(
                priority: .userInitiated,
                operation: { [table, url] in
                    VPinballStatus(rawValue: VPinballCompress(table.basePath.cstring,
                                                              url.path.cstring))
                }
            ).value == .success {
                await MainActor.run { vpinballViewModel.hideHUD() }
                return url
            }
        } catch {
            log(.error, "unable to share: error=\(error)")
        }

        await MainActor.run { vpinballViewModel.hideHUD() }
        return nil
    }

    func extractScript(table: PinTable) async -> Bool {
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

    func play(table: PinTable) async -> Bool {
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

    func rumble(_ data: VPinballRumbleData) {
        if data.low_frequency_rumble > 0 || data.high_frequency_rumble > 0 {
            let style: UIImpactFeedbackGenerator.FeedbackStyle

            if data.low_frequency_rumble == data.high_frequency_rumble {
                style = .rigid
            } else if data.low_frequency_rumble > 20000 || data.high_frequency_rumble > 20000 {
                style = .heavy
            } else if data.low_frequency_rumble > 10000 || data.high_frequency_rumble > 10000 {
                style = .medium
            } else if data.low_frequency_rumble > 1000 || data.high_frequency_rumble > 1000 {
                style = .light
            } else {
                style = .soft
            }

            impactGenerators[style]!.impactOccurred()
        }
    }

    func getTableOptions() -> VPinballTableOptions {
        var tableOptions = VPinballTableOptions()
        withUnsafePointer(to: &tableOptions) { ptr in
            VPinballGetTableOptions(ptr)
        }
        return tableOptions
    }

    func setTableOptions(_ tableOptions: VPinballTableOptions) {
        withUnsafePointer(to: tableOptions) { ptr in
            VPinballSetTableOptions(ptr)
        }
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

    func getCustomTableOptions() -> [VPinballCustomTableOption] {
        var customOptions: [VPinballCustomTableOption] = []

        let count = VPinballGetCustomTableOptionsCount()
        for index in 0 ..< count {
            var customTableOption = VPinballCustomTableOption()
            withUnsafePointer(to: &customTableOption) { ptr in
                VPinballGetCustomTableOption(index, ptr)
            }
            customOptions.append(customTableOption)
        }

        return customOptions
    }

    func setCustomTableOption(_ customTableOption: VPinballCustomTableOption) {
        withUnsafePointer(to: customTableOption) { ptr in
            VPinballSetCustomTableOption(ptr)
        }
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

    func getViewSetup() -> VPinballViewSetup {
        var viewSetup = VPinballViewSetup()
        withUnsafePointer(to: &viewSetup) { ptr in
            VPinballGetViewSetup(ptr)
        }
        return viewSetup
    }

    func setViewSetup(_ viewSetup: VPinballViewSetup) {
        withUnsafePointer(to: viewSetup) { ptr in
            VPinballSetViewSetup(ptr)
        }
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
        return activeTable?.hasImage() ?? false
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

    func handleTableList(data: UnsafeRawPointer) -> UnsafeRawPointer? {
        let tableListDataPtr = UnsafeMutableRawPointer(mutating: data).assumingMemoryBound(to: VPinballTablesData.self)

        if modelContext == nil {
            tableListDataPtr.pointee.success = false
            return nil
        }

        DispatchQueue.main.sync {
            do {
                let descriptor = FetchDescriptor<PinTable>()
                let tables = try modelContext!.fetch(descriptor)

                if tables.isEmpty {
                    tableListDataPtr.pointee.tables = nil
                    tableListDataPtr.pointee.tableCount = 0
                    tableListDataPtr.pointee.success = true
                    return
                }

                let tablesArray = UnsafeMutablePointer<VPinballTableInfo>.allocate(capacity: tables.count)
                var allocatedCount = 0

                for (index, table) in tables.enumerated() {
                    let tableIdString = table.tableId.uuidString
                    let tableIdCString = tableIdString.cString(using: .utf8)!
                    let tableIdPtr = UnsafeMutablePointer<CChar>.allocate(capacity: tableIdCString.count)
                    tableIdPtr.initialize(from: tableIdCString, count: tableIdCString.count)

                    let tableNameCString = table.name.cString(using: .utf8)!
                    let tableNamePtr = UnsafeMutablePointer<CChar>.allocate(capacity: tableNameCString.count)
                    tableNamePtr.initialize(from: tableNameCString, count: tableNameCString.count)

                    tablesArray[index] = VPinballTableInfo(
                        tableId: tableIdPtr,
                        name: tableNamePtr
                    )
                    allocatedCount += 1
                }

                tableListDataPtr.pointee.tables = tablesArray
                tableListDataPtr.pointee.tableCount = CInt(tables.count)
                tableListDataPtr.pointee.success = true

            } catch {
                tableListDataPtr.pointee.tables = nil
                tableListDataPtr.pointee.tableCount = 0
                tableListDataPtr.pointee.success = false
            }
        }

        return nil
    }

    func handleTableImport(data: UnsafeRawPointer) {
        let eventDataPtr = UnsafeMutableRawPointer(mutating: data).assumingMemoryBound(to: VPinballTableEventData.self)
        let eventData = eventDataPtr.pointee

        if eventData.path == nil {
            log(.error, "Failed to import table: invalid file path")
            eventDataPtr.pointee.success = false
            return
        }
        let pathPtr = eventData.path!

        let filePath = String(cString: pathPtr)

        DispatchQueue.main.async { [weak self] in
            if self == nil {
                eventDataPtr.pointee.success = false
                return
            }

            Task {
                let fileUrl = URL(fileURLWithPath: filePath)

                if let table = await self!.import(url: fileUrl) {
                    PinTable.create(table: table)
                    eventDataPtr.pointee.success = true

                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                        VPinballSetWebServerUpdated()
                    }
                } else {
                    self!.log(.error, "Failed to import table from web server")
                    eventDataPtr.pointee.success = false
                }
            }
        }
    }

    func handleTableRename(data: UnsafeRawPointer) {
        let eventDataPtr = UnsafeMutableRawPointer(mutating: data).assumingMemoryBound(to: VPinballTableEventData.self)

        if modelContext == nil {
            eventDataPtr.pointee.success = false
            return
        }

        if eventDataPtr.pointee.tableId == nil || eventDataPtr.pointee.newName == nil {
            log(.error, "Failed to rename table: invalid parameters")
            eventDataPtr.pointee.success = false
            return
        }
        let tableIdPtr = eventDataPtr.pointee.tableId!
        let newNamePtr = eventDataPtr.pointee.newName!

        let tableIdString = String(cString: tableIdPtr)
        let newName = String(cString: newNamePtr)

        if UUID(uuidString: tableIdString) == nil {
            log(.error, "Failed to rename table: invalid table ID format")
            eventDataPtr.pointee.success = false
            return
        }
        let tableId = UUID(uuidString: tableIdString)!

        DispatchQueue.main.sync {
            do {
                let descriptor = FetchDescriptor<PinTable>(predicate: #Predicate<PinTable> { table in
                    table.tableId == tableId
                })
                let tables = try modelContext!.fetch(descriptor)

                if tables.first == nil {
                    log(.error, "Failed to rename table: table not found")
                    eventDataPtr.pointee.success = false
                    return
                }
                let table = tables.first!

                PinTable.updateName(table: table, name: newName)
                eventDataPtr.pointee.success = true

            } catch {
                log(.error, "Failed to rename table: \(error.localizedDescription)")
                eventDataPtr.pointee.success = false
            }
        }
    }

    func handleTableDelete(data: UnsafeRawPointer) {
        let eventDataPtr = UnsafeMutableRawPointer(mutating: data).assumingMemoryBound(to: VPinballTableEventData.self)

        if modelContext == nil {
            eventDataPtr.pointee.success = false
            return
        }

        if eventDataPtr.pointee.tableId == nil {
            log(.error, "Failed to delete table: invalid table ID")
            eventDataPtr.pointee.success = false
            return
        }
        let tableIdPtr = eventDataPtr.pointee.tableId!

        let tableIdString = String(cString: tableIdPtr)

        if UUID(uuidString: tableIdString) == nil {
            log(.error, "Failed to delete table: invalid table ID format")
            eventDataPtr.pointee.success = false
            return
        }
        let tableId = UUID(uuidString: tableIdString)!

        DispatchQueue.main.sync {
            do {
                let descriptor = FetchDescriptor<PinTable>(predicate: #Predicate<PinTable> { table in
                    table.tableId == tableId
                })
                let tables = try modelContext!.fetch(descriptor)

                if tables.first == nil {
                    log(.error, "Failed to delete table: table not found")
                    eventDataPtr.pointee.success = false
                    return
                }
                let table = tables.first!

                try? FileManager.default.removeItem(at: table.baseURL)

                modelContext!.delete(table)
                do {
                    try modelContext!.save()
                    eventDataPtr.pointee.success = true
                    VPinballSetWebServerUpdated()
                } catch {
                    log(.error, "Failed to delete table: \(error.localizedDescription)")
                    eventDataPtr.pointee.success = false
                }

            } catch {
                log(.error, "Failed to delete table: \(error.localizedDescription)")
                eventDataPtr.pointee.success = false
            }
        }
    }
}
