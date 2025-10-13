import SwiftUI
import UniformTypeIdentifiers
import ZIPFoundation

@MainActor
class TableManager: ObservableObject {
    static let shared = TableManager()

    @Published private(set) var tables: [Table] = []

    private var tablesPath: String = ""
    private var tablesJSONPath: String = ""
    private var requiresStaging: Bool = false
    private var loadedTableUuid: String = ""
    private var loadedTableWorkingDir: String = ""

    private let vpinballViewModel = VPinballViewModel.shared

    init() {
        loadTablesPath()
        loadTables()
    }

    func refresh() {
        loadTables()
    }

    func getTable(uuid: String) -> Table? {
        return tables.first { $0.uuid == uuid }
    }

    func importTable(from url: URL) async -> Bool {
        vpinballViewModel.showProgressHUD(
            title: url.lastPathComponent.removingPercentEncoding!,
            status: url.pathExtension.lowercased() == "vpx" ? "Importing" : "Importing Archive"
        )

        let isSecurityScoped = url.startAccessingSecurityScopedResource()
        defer {
            if isSecurityScoped {
                url.stopAccessingSecurityScopedResource()
            }
        }

        let result = importTableSync(path: url.path)

        vpinballViewModel.hideHUD()
        if result {
            loadTables()
        }

        return result
    }

    func deleteTable(uuid: String) async -> Bool {
        let result = deleteTableSync(uuid: uuid)

        if result {
            loadTables()
        }

        return result
    }

    func renameTable(uuid: String, newName: String) async -> Bool {
        let result = renameTableSync(uuid: uuid, newName: newName)

        if result {
            loadTables()
        }

        return result
    }

    func setTableImage(uuid: String, imagePath: String) async -> Bool {
        let result = setTableImageSync(uuid: uuid, imagePath: imagePath)

        if result {
            loadTables()
        }

        return result
    }

    func exportTable(uuid: String) async -> URL? {
        if let table = getTable(uuid: uuid) {
            vpinballViewModel.showProgressHUD(title: table.name, status: "Compressing")
        }

        let exportPath = exportTableSync(uuid: uuid)

        vpinballViewModel.hideHUD()

        if let exportPath = exportPath {
            return URL(fileURLWithPath: exportPath)
        }

        return nil
    }

    func stageTable(uuid: String) -> String? {
        return stageTableSync(uuid: uuid)
    }

    func cleanupLoadedTable(uuid: String) {
        cleanupLoadedTableSync(uuid: uuid)
    }

    func extractTableScript(uuid: String) async -> Bool {
        guard let table = getTable(uuid: uuid) else {
            return false
        }

        vpinballViewModel.showProgressHUD(title: table.name, status: "Extracting Script")

        let result = extractTableScriptSync(uuid: uuid)

        vpinballViewModel.hideHUD()

        return result
    }

    private func loadTablesPath() {
        tablesPath = String(cString: VPinballGetTablesPath())

        if !tablesPath.hasSuffix("/") {
            tablesPath += "/"
        }

        tablesJSONPath = (tablesPath as NSString).appendingPathComponent("tables.json")
        requiresStaging = false

        if !FileOps.exists(tablesPath) {
            _ = FileOps.createDirectory(tablesPath)
        }
    }

    func loadTables() {
        var loadedTables: [Table] = []

        if FileOps.exists(tablesJSONPath) {
            if let content = FileOps.read(tablesJSONPath),
               let jsonData = content.data(using: .utf8)
            {
                if let response = try? JSONDecoder().decode(TablesResponse.self, from: jsonData) {
                    loadedTables = response.tables
                }
            }
        }

        var seen = Set<String>()
        loadedTables.removeAll { table in
            if table.uuid.isEmpty || seen.contains(table.uuid) {
                return true
            }
            seen.insert(table.uuid)

            let fullPath = (tablesPath as NSString).appendingPathComponent(table.path)
            if !FileOps.exists(fullPath) {
                VPinballManager.log(.info, "Removing table with missing file: \(fullPath)")
                return true
            }

            return false
        }

        for i in 0..<loadedTables.count {
            if loadedTables[i].image.isEmpty {
                let fullPath = (tablesPath as NSString).appendingPathComponent(loadedTables[i].path)
                let parentPath = (fullPath as NSString).deletingLastPathComponent
                let stem = (fullPath as NSString).deletingPathExtension.components(separatedBy: "/").last ?? ""
                let pngPath = (parentPath as NSString).appendingPathComponent(stem + ".png")
                let jpgPath = (parentPath as NSString).appendingPathComponent(stem + ".jpg")

                if FileOps.exists(pngPath) {
                    loadedTables[i] = Table(
                        uuid: loadedTables[i].uuid,
                        name: loadedTables[i].name,
                        path: loadedTables[i].path,
                        image: relativePath(fullPath: pngPath, basePath: tablesPath),
                        createdAt: loadedTables[i].createdAt,
                        modifiedAt: loadedTables[i].modifiedAt
                    )
                } else if FileOps.exists(jpgPath) {
                    loadedTables[i] = Table(
                        uuid: loadedTables[i].uuid,
                        name: loadedTables[i].name,
                        path: loadedTables[i].path,
                        image: relativePath(fullPath: jpgPath, basePath: tablesPath),
                        createdAt: loadedTables[i].createdAt,
                        modifiedAt: loadedTables[i].modifiedAt
                    )
                }
            }
        }

        let vpxFiles = FileOps.listFiles(tablesPath, ext: ".vpx")
        for filePath in vpxFiles {
            if !loadedTables.contains(where: { (tablesPath as NSString).appendingPathComponent($0.path) == filePath }) {
                if let newTable = createTable(path: filePath) {
                    loadedTables.append(newTable)
                }
            }
        }

        tables = loadedTables
        saveTables()
    }

    private func saveTables() {
        let sorted = tables.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        let response = TablesResponse(tableCount: sorted.count, tables: sorted)

        if let jsonData = try? JSONEncoder().encode(response),
           let jsonString = String(data: jsonData, encoding: .utf8)
        {
            _ = FileOps.write(tablesJSONPath, content: jsonString)
        }
    }

    private func createTable(path: String) -> Table? {
        let uuid = generateUUID()
        let relativePath = self.relativePath(fullPath: path, basePath: tablesPath)

        var name = (path as NSString).deletingPathExtension.components(separatedBy: "/").last ?? ""
        name = name.replacingOccurrences(of: "_", with: " ")

        let now = Int64(Date().timeIntervalSince1970)

        let parentPath = (path as NSString).deletingLastPathComponent
        let stem = (path as NSString).deletingPathExtension.components(separatedBy: "/").last ?? ""
        let pngPath = (parentPath as NSString).appendingPathComponent(stem + ".png")
        let jpgPath = (parentPath as NSString).appendingPathComponent(stem + ".jpg")

        var image = ""
        if FileOps.exists(pngPath) {
            image = self.relativePath(fullPath: pngPath, basePath: tablesPath)
        } else if FileOps.exists(jpgPath) {
            image = self.relativePath(fullPath: jpgPath, basePath: tablesPath)
        }

        return Table(uuid: uuid, name: name, path: relativePath, image: image, createdAt: now, modifiedAt: now)
    }

    private func findTable(path: String) -> Table? {
        return tables.first { (tablesPath as NSString).appendingPathComponent($0.path) == path }
    }

    private func generateUUID() -> String {
        var uuid = UUID().uuidString.lowercased()
        while tables.contains(where: { $0.uuid == uuid }) {
            uuid = UUID().uuidString.lowercased()
            VPinballManager.log(.warn, "UUID collision detected: \(uuid), regenerating")
        }
        return uuid
    }

    private func sanitizeName(_ name: String) -> String {
        let invalidChars = CharacterSet(charactersIn: " _/\\:*?\"<>|.&'()")
        var sanitized = name.components(separatedBy: invalidChars).joined(separator: "-")

        var result = ""
        var lastWasHyphen = false
        for char in sanitized {
            if char == "-" {
                if !lastWasHyphen {
                    result.append(char)
                    lastWasHyphen = true
                }
            } else {
                result.append(char)
                lastWasHyphen = false
            }
        }

        while result.hasPrefix("-") {
            result.removeFirst()
        }
        while result.hasSuffix("-") {
            result.removeLast()
        }

        return result.isEmpty ? "table" : result
    }

    private func getUniqueFolder(baseName: String) -> String {
        let sanitized = sanitizeName(baseName)
        var candidate = sanitized
        var counter = 2

        while FileOps.exists((tablesPath as NSString).appendingPathComponent(candidate)) {
            candidate = "\(sanitized)-\(counter)"
            counter += 1
        }

        return candidate
    }

    private func relativePath(fullPath: String, basePath: String) -> String {
        if fullPath.isEmpty || basePath.isEmpty {
            return fullPath
        }

        if !fullPath.hasPrefix(basePath) {
            return fullPath
        }

        var result = String(fullPath.dropFirst(basePath.count))
        if result.hasPrefix("/") {
            result = String(result.dropFirst())
        }

        return result
    }

    private func importTableSync(path: String) -> Bool {
        if !FileOps.exists(path) {
            return false
        }

        let ext = (path as NSString).pathExtension.lowercased()

        if ext == "vpxz" {
            return importVPXZ(path: path)
        } else if ext == "vpx" {
            return importVPX(path: path)
        }

        return false
    }

    private func importVPX(path: String) -> Bool {
        let stem = (path as NSString).deletingPathExtension.components(separatedBy: "/").last ?? ""
        var name = stem.replacingOccurrences(of: "_", with: " ")

        let folderName = getUniqueFolder(baseName: name)
        let destFolder = (tablesPath as NSString).appendingPathComponent(folderName)

        if !FileOps.createDirectory(destFolder) {
            return false
        }

        let fileName = (path as NSString).lastPathComponent
        let destFile = (destFolder as NSString).appendingPathComponent(fileName)

        if !FileOps.copy(from: path, to: destFile) {
            return false
        }

        VPinballManager.log(.info, "Successfully imported table: \(name)")
        return true
    }

    private func importVPXZ(path: String) -> Bool {
        let tempDir = (NSTemporaryDirectory() as NSString).appendingPathComponent("vpinball_import_\(Int(Date().timeIntervalSince1970))")

        if !FileOps.createDirectory(tempDir) {
            return false
        }

        defer {
            _ = FileOps.deleteDirectory(tempDir)
        }

        guard let archive = Archive(url: URL(fileURLWithPath: path), accessMode: .read) else {
            return false
        }

        do {
            for entry in archive {
                let entryPath = (tempDir as NSString).appendingPathComponent(entry.path)

                if entry.type == .directory {
                    try FileManager.default.createDirectory(atPath: entryPath, withIntermediateDirectories: true)
                } else {
                    let parentDir = (entryPath as NSString).deletingLastPathComponent
                    try FileManager.default.createDirectory(atPath: parentDir, withIntermediateDirectories: true)
                    _ = try archive.extract(entry, to: URL(fileURLWithPath: entryPath))
                }
            }
        } catch {
            return false
        }

        let vpxFiles = FileOps.listFiles(tempDir, ext: ".vpx")

        if vpxFiles.isEmpty {
            return false
        }

        var result = true
        for vpxFile in vpxFiles {
            let vpxName = (vpxFile as NSString).deletingPathExtension.components(separatedBy: "/").last ?? ""
            let sourceDir = (vpxFile as NSString).deletingLastPathComponent
            let fileName = (vpxFile as NSString).lastPathComponent

            let folderName = getUniqueFolder(baseName: vpxName)
            let destFolder = (tablesPath as NSString).appendingPathComponent(folderName)

            if !FileOps.createDirectory(destFolder) {
                result = false
                continue
            }

            if !FileOps.copyDirectory(from: sourceDir, to: destFolder) {
                result = false
                continue
            }

            VPinballManager.log(.info, "Successfully imported VPX from archive: \(vpxName)")
        }

        return result
    }

    private func deleteTableSync(uuid: String) -> Bool {
        guard let table = getTable(uuid: uuid) else {
            return false
        }

        let tablePath = (tablesPath as NSString).appendingPathComponent(table.path)
        let tableDir = (tablePath as NSString).deletingLastPathComponent

        if tableDir.isEmpty || tableDir == tablesPath || tableDir + "/" == tablesPath {
            return false
        }

        if FileOps.exists(tableDir) {
            let vpxFiles = FileOps.listFiles(tableDir, ext: ".vpx")

            if vpxFiles.count <= 1 {
                if !FileOps.deleteDirectory(tableDir) {
                    return false
                }
            } else {
                if !FileOps.delete(tablePath) {
                    return false
                }
            }
        }

        tables.removeAll { $0.uuid == uuid }
        saveTables()

        return true
    }

    private func renameTableSync(uuid: String, newName: String) -> Bool {
        guard let index = tables.firstIndex(where: { $0.uuid == uuid }) else {
            return false
        }

        let now = Int64(Date().timeIntervalSince1970)
        tables[index] = Table(
            uuid: tables[index].uuid,
            name: newName,
            path: tables[index].path,
            image: tables[index].image,
            createdAt: tables[index].createdAt,
            modifiedAt: now
        )

        saveTables()
        return true
    }

    private func setTableImageSync(uuid: String, imagePath: String) -> Bool {
        guard let index = tables.firstIndex(where: { $0.uuid == uuid }) else {
            return false
        }

        let table = tables[index]

        if imagePath.isEmpty {
            if !table.image.isEmpty {
                let currentImagePath = (tablesPath as NSString).appendingPathComponent(table.image)
                if FileOps.exists(currentImagePath) {
                    _ = FileOps.delete(currentImagePath)
                }
            }

            let now = Int64(Date().timeIntervalSince1970)
            tables[index] = Table(
                uuid: table.uuid,
                name: table.name,
                path: table.path,
                image: "",
                createdAt: table.createdAt,
                modifiedAt: now
            )

            saveTables()
            return true
        }

        if imagePath.hasPrefix("/") {
            let fullPath = (tablesPath as NSString).appendingPathComponent(table.path)
            let baseName = (fullPath as NSString).deletingPathExtension.components(separatedBy: "/").last ?? ""

            let workingDir: String
            if uuid == loadedTableUuid && !loadedTableWorkingDir.isEmpty {
                workingDir = loadedTableWorkingDir
            } else {
                workingDir = (fullPath as NSString).deletingLastPathComponent
            }

            let destPath = (workingDir as NSString).appendingPathComponent(baseName + ".jpg")

            if !FileOps.copy(from: imagePath, to: destPath) {
                return false
            }

            let relPath = relativePath(fullPath: destPath, basePath: tablesPath)
            let now = Int64(Date().timeIntervalSince1970)

            tables[index] = Table(
                uuid: table.uuid,
                name: table.name,
                path: table.path,
                image: relPath,
                createdAt: table.createdAt,
                modifiedAt: now
            )

            saveTables()
            return true
        }

        let now = Int64(Date().timeIntervalSince1970)
        tables[index] = Table(
            uuid: table.uuid,
            name: table.name,
            path: table.path,
            image: imagePath,
            createdAt: table.createdAt,
            modifiedAt: now
        )

        saveTables()
        return true
    }

    private func exportTableSync(uuid: String) -> String? {
        guard let table = getTable(uuid: uuid) else {
            return nil
        }

        let sanitizedName = sanitizeName(table.name)
        let tempFile = (NSTemporaryDirectory() as NSString).appendingPathComponent(sanitizedName + ".vpxz")

        if FileOps.exists(tempFile) {
            _ = FileOps.delete(tempFile)
        }

        let fullPath = (tablesPath as NSString).appendingPathComponent(table.path)
        let tableDirToCompress = (fullPath as NSString).deletingLastPathComponent

        guard let archive = Archive(url: URL(fileURLWithPath: tempFile), accessMode: .create) else {
            return nil
        }

        do {
            try FileOps.addDirectoryToArchive(archive: archive, directoryPath: tableDirToCompress, basePath: tableDirToCompress)
        } catch {
            return nil
        }

        return tempFile
    }

    private func stageTableSync(uuid: String) -> String? {
        guard let table = getTable(uuid: uuid) else {
            return nil
        }

        if table.path.isEmpty {
            return nil
        }

        let fullPath = (tablesPath as NSString).appendingPathComponent(table.path)
        loadedTableUuid = uuid
        loadedTableWorkingDir = (fullPath as NSString).deletingLastPathComponent

        if loadedTableWorkingDir.isEmpty {
            loadedTableWorkingDir = "."
        }

        return fullPath
    }

    private func cleanupLoadedTableSync(uuid: String) {
        if loadedTableUuid != uuid {
            return
        }

        loadedTableUuid = ""
        loadedTableWorkingDir = ""
    }

    private func extractTableScriptSync(uuid: String) -> Bool {
        guard let table = getTable(uuid: uuid) else {
            return false
        }

        let fullPath = (tablesPath as NSString).appendingPathComponent(table.path)
        let tableDir = (fullPath as NSString).deletingLastPathComponent
        let baseName = (fullPath as NSString).deletingPathExtension.components(separatedBy: "/").last ?? ""
        let scriptPath = (tableDir as NSString).appendingPathComponent(baseName + ".vbs")

        if FileOps.exists(scriptPath) {
            return true
        }

        if VPinballStatus(rawValue: VPinballLoadTable(fullPath.cstring)) != .success {
            VPinballManager.log(.error, "Failed to load table for script extraction: \(fullPath)")
            return false
        }

        if VPinballStatus(rawValue: VPinballExtractTableScript()) != .success {
            VPinballManager.log(.error, "Failed to extract script from table")
            return false
        }

        if FileOps.exists(scriptPath) {
            VPinballManager.log(.info, "Successfully extracted script: \(scriptPath)")
            return true
        }

        VPinballManager.log(.warn, "Script file not found after extraction")
        return false
    }

    func filteredTables(searchText: String, sortOrder: SortOrder) -> [Table] {
        var filtered = tables

        if !searchText.isEmpty {
            filtered = filtered.filter { table in
                table.name.localizedCaseInsensitiveContains(searchText)
            }
        }

        filtered = filtered.sorted { table1, table2 in
            if sortOrder == .forward {
                return table1.name.localizedCaseInsensitiveCompare(table2.name) == .orderedAscending
            } else {
                return table1.name.localizedCaseInsensitiveCompare(table2.name) == .orderedDescending
            }
        }

        return filtered
    }
}

private enum FileOps {
    static func exists(_ path: String) -> Bool {
        return FileManager.default.fileExists(atPath: path)
    }

    static func read(_ path: String) -> String? {
        return try? String(contentsOfFile: path, encoding: .utf8)
    }

    static func write(_ path: String, content: String) -> Bool {
        do {
            try content.write(toFile: path, atomically: true, encoding: .utf8)
            return true
        } catch {
            return false
        }
    }

    static func copy(from: String, to: String) -> Bool {
        do {
            if exists(to) {
                try FileManager.default.removeItem(atPath: to)
            }
            try FileManager.default.copyItem(atPath: from, toPath: to)
            return true
        } catch {
            return false
        }
    }

    static func delete(_ path: String) -> Bool {
        do {
            try FileManager.default.removeItem(atPath: path)
            return true
        } catch {
            return false
        }
    }

    static func deleteDirectory(_ path: String) -> Bool {
        do {
            try FileManager.default.removeItem(atPath: path)
            return true
        } catch {
            return false
        }
    }

    static func createDirectory(_ path: String) -> Bool {
        do {
            try FileManager.default.createDirectory(atPath: path, withIntermediateDirectories: true)
            return true
        } catch {
            return false
        }
    }

    static func listFiles(_ path: String, ext: String) -> [String] {
        var files: [String] = []

        guard let enumerator = FileManager.default.enumerator(atPath: path) else {
            return files
        }

        while let file = enumerator.nextObject() as? String {
            let fullPath = (path as NSString).appendingPathComponent(file)
            var isDirectory: ObjCBool = false

            if FileManager.default.fileExists(atPath: fullPath, isDirectory: &isDirectory) {
                if !isDirectory.boolValue {
                    if ext.isEmpty || (fullPath as NSString).pathExtension.lowercased() == ext.lowercased().replacingOccurrences(of: ".", with: "") {
                        files.append(fullPath)
                    }
                }
            }
        }

        return files
    }

    static func copyDirectory(from: String, to: String) -> Bool {
        do {
            if exists(to) {
                try FileManager.default.removeItem(atPath: to)
            }

            try FileManager.default.createDirectory(atPath: to, withIntermediateDirectories: true)

            let contents = try FileManager.default.contentsOfDirectory(atPath: from)

            for item in contents {
                let sourcePath = (from as NSString).appendingPathComponent(item)
                let destPath = (to as NSString).appendingPathComponent(item)

                var isDirectory: ObjCBool = false
                if FileManager.default.fileExists(atPath: sourcePath, isDirectory: &isDirectory) {
                    if isDirectory.boolValue {
                        if !copyDirectory(from: sourcePath, to: destPath) {
                            return false
                        }
                    } else {
                        try FileManager.default.copyItem(atPath: sourcePath, toPath: destPath)
                    }
                }
            }

            return true
        } catch {
            return false
        }
    }

    static func addDirectoryToArchive(archive: Archive, directoryPath: String, basePath: String) throws {
        let enumerator = FileManager.default.enumerator(atPath: directoryPath)

        while let file = enumerator?.nextObject() as? String {
            let fullPath = (directoryPath as NSString).appendingPathComponent(file)
            var isDirectory: ObjCBool = false

            if FileManager.default.fileExists(atPath: fullPath, isDirectory: &isDirectory) {
                let relativePath = String(fullPath.dropFirst(basePath.count + 1))

                if isDirectory.boolValue {
                    try archive.addEntry(with: relativePath + "/", type: .directory, uncompressedSize: 0, provider: { (position: Int64, size: Int) -> Data in
                        return Data()
                    })
                } else {
                    try archive.addEntry(with: relativePath, relativeTo: URL(fileURLWithPath: basePath))
                }
            }
        }
    }
}
