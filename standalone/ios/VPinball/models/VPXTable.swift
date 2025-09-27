import UIKit

// VPXTable struct that matches the C++ VPXTable structure
struct VPXTable: Codable, Identifiable, Hashable {
    let uuid: String
    let name: String
    let path: String
    let artwork: String
    let createdAt: Int64
    let modifiedAt: Int64

    // Computed properties for iOS UI compatibility
    var id: String { uuid }

    var tableId: UUID {
        UUID(uuidString: uuid) ?? UUID()
    }

    var fileName: String {
        (path as NSString).lastPathComponent
    }

    var uiImage: UIImage? {
        guard !artwork.isEmpty else { return nil }
        return UIImage(contentsOfFile: artworkPath)
    }

    var artworkPath: String {
        let tablesPath = String(cString: VPinballGetTablesPath())
        return (tablesPath as NSString).appendingPathComponent(artwork)
    }

    var fullPath: String {
        let tablesPath = String(cString: VPinballGetTablesPath())
        return (tablesPath as NSString).appendingPathComponent(path)
    }

    var baseURL: URL {
        URL(fileURLWithPath: fullPath).deletingLastPathComponent()
    }

    var basePath: String {
        return baseURL.path
    }

    var fullURL: URL {
        URL(fileURLWithPath: fullPath)
    }

    var scriptURL: URL {
        return fullURL.deletingPathExtension().appendingPathExtension("vbs")
    }

    var scriptPath: String {
        return scriptURL.path
    }

    var iniURL: URL {
        return fullURL.deletingPathExtension().appendingPathExtension("ini")
    }

    var iniPath: String {
        return iniURL.path
    }

    var imagePath: String {
        return artworkPath
    }

    func exists() -> Bool {
        return FileManager.default.fileExists(atPath: fullPath)
    }

    func hasScriptFile() -> Bool {
        return FileManager.default.fileExists(atPath: scriptPath)
    }

    func hasIniFile() -> Bool {
        return FileManager.default.fileExists(atPath: iniPath)
    }

    func hasImageFile() -> Bool {
        return FileManager.default.fileExists(atPath: artworkPath)
    }
}

// VPXTablesResponse struct that matches C++ response
struct VPXTablesResponse: Codable {
    let success: Bool
    let tableCount: Int
    let tables: [VPXTable]
}

// Table management using C++ library calls
@MainActor
class VPXTableManager: ObservableObject {
    static let shared = VPXTableManager()

    @Published var tables: [VPXTable] = []
    @Published var isLoading = false

    private let vpinballManager = VPinballManager.shared

    func refreshTables() async {
        isLoading = true

        // Use the efficient refresh method for pull-to-refresh
        let success = await Task.detached(priority: .userInitiated) {
            VPinballRefreshTables() == 0 // 0 = SUCCESS
        }.value

        if success {
            vpinballManager.log(.info, "Table refresh completed successfully")
        } else {
            vpinballManager.log(.error, "Table refresh failed")
        }

        // Load the updated table list
        await loadTables()

        isLoading = false
    }

    // Internal refresh method without loading indicator (for background use)
    private func refreshTablesInternal() async {
        await scanTables()
        await loadTables()
    }

    private func scanTables() async {
        let success = await Task.detached(priority: .userInitiated) {
            VPinballRefreshTables() == 0 // 0 = SUCCESS
        }.value

        if success {
            vpinballManager.log(.info, "Table scan completed successfully")
        } else {
            vpinballManager.log(.error, "Table scan failed")
        }
    }

    func loadTables() async {
        do {
            let jsonStringPtr = VPinballGetTables()
            let jsonString = String(cString: jsonStringPtr)

            guard let jsonData = jsonString.data(using: String.Encoding.utf8) else {
                vpinballManager.log(.error, "Failed to convert JSON string to data")
                return
            }

            let response = try JSONDecoder().decode(VPXTablesResponse.self, from: jsonData)

            tables = response.tables.filter { $0.exists() }
            vpinballManager.log(.info, "Loaded \(tables.count) tables from C++ library")
        } catch {
            vpinballManager.log(.error, "Failed to decode VPX tables: \(error)")
            tables = []
        }
    }

    func addTable(from url: URL) async -> VPXTable? {
        // Use the new unified import system - it handles everything internally
        let originalTableCount = tables.count
        let fileName = url.lastPathComponent

        // Import the table (this handles VPX and VPXZ files, creates folders, copies files, updates registry)
        await vpinballManager.import(url: url)

        // Refresh the table list to pick up the newly imported table
        await loadTables()

        // Try to find the newly added table by comparing table count and name
        if tables.count > originalTableCount {
            // Look for a table with a similar name to what we just imported
            let baseName = String(fileName.dropLast(4)) // Remove extension
            let candidateTable = tables.first { table in
                // Check if the table name contains the base name (case insensitive)
                table.name.lowercased().contains(baseName.lowercased()) ||
                    table.fileName.lowercased().contains(baseName.lowercased())
            }

            if let newTable = candidateTable {
                vpinballManager.log(.info, "Successfully imported and found new table: \(newTable.name)")
                return newTable
            } else {
                // If we can't find by name, just return the newest table (last in the list)
                vpinballManager.log(.info, "Imported table, returning newest table in list")
                return tables.last
            }
        }

        vpinballManager.log(.error, "Import may have failed - table count unchanged")
        return nil
    }

    func deleteTable(_ table: VPXTable) async {
        vpinballManager.log(.info, "Deleting table: \(table.name)")

        // First, immediately remove from the UI (optimistic update)
        tables.removeAll { $0.uuid == table.uuid }

        // Then perform the actual deletion in the background
        // The C++ DeleteTable will send a TableRemoved event which handles web server updates
        Task.detached(priority: .userInitiated) {
            let status = VPinballRemoveVPXTable(table.uuid.cstring)

            if status != 0 {
                // If deletion failed, refresh the table list to restore correct state
                await MainActor.run {
                    self.vpinballManager.log(.error, "Failed to delete table: \(table.name)")
                }
                await self.refreshTablesInternal()
            }
        }
    }

    func renameTable(_ table: VPXTable, newName: String) async {
        let status = VPinballRenameTable(table.uuid.cstring, newName.cstring)

        if status == 0 { // SUCCESS
            // Refresh the table list
            await loadTables()
            VPinballSetWebServerUpdated()
        }
    }

    func updateTableImage(_ table: VPXTable, image: UIImage) async {
        guard let jpegData = image.jpegData(compressionQuality: 0.8) else {
            vpinballManager.log(.error, "Failed to convert image to JPEG for table: \(table.name)")
            return
        }

        let tableBaseName = (table.fileName as NSString).deletingPathExtension
        let artworkFileName = "\(tableBaseName).jpg"

        let tableDirectory = table.baseURL
        let artworkPath = tableDirectory.appendingPathComponent(artworkFileName).path

        let relativeArtworkPath = table.path.isEmpty ? artworkFileName :
            "\((table.path as NSString).deletingLastPathComponent)/\(artworkFileName)"

        do {
            try jpegData.write(to: URL(fileURLWithPath: artworkPath))

            let status = await Task.detached(priority: .userInitiated) {
                VPinballSetTableArtwork(table.uuid.cstring, relativeArtworkPath.cstring)
            }.value

            if status == 0 {
                vpinballManager.log(.info, "Successfully updated artwork for table: \(table.name)")
                await loadTables()
                VPinballSetWebServerUpdated()
            } else {
                vpinballManager.log(.error, "Failed to update artwork in database for table: \(table.name)")
            }
        } catch {
            vpinballManager.log(.error, "Failed to write artwork file for table: \(table.name) - \(error)")
        }
    }

    func resetTableImage(_ table: VPXTable) async {
        let status = await Task.detached(priority: .userInitiated) {
            VPinballSetTableArtwork(table.uuid.cstring, "".cstring)
        }.value

        if status == 0 {
            vpinballManager.log(.info, "Successfully reset artwork for table: \(table.name)")
            await loadTables()
            VPinballSetWebServerUpdated()
        } else {
            vpinballManager.log(.error, "Failed to reset artwork in database for table: \(table.name)")
        }
    }

    func updateTable(_: VPXTable) {
        VPinballSetWebServerUpdated()
    }

    func filteredTables(searchText: String, sortOrder: SortOrder) -> [VPXTable] {
        var filtered = tables

        // Apply search filter
        if !searchText.isEmpty {
            filtered = filtered.filter { table in
                table.name.localizedCaseInsensitiveContains(searchText)
            }
        }

        // Apply sort order
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
