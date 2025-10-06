
import SwiftUI

@MainActor
class TableManager: ObservableObject {
    static let shared = TableManager()

    @Published var tables: [Table] = []

    let vpinballViewModel = VPinballViewModel.shared

    func loadTables() async {
        let json = String(cString: VPinballGetTables())
        if let jsonData = json.data(using: .utf8),
           let response = try? JSONDecoder().decode(TablesResponse.self, from: jsonData)
        {
            let allTables = response.tables
            tables = allTables.filter { $0.exists() }
        } else {
            VPinballManager.log(.error, "Failed to load tables")
            tables = []
        }
    }

    func importTable(from url: URL) async {
        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: url.lastPathComponent.removingPercentEncoding!,
                                              status: url.pathExtension.lowercased() == "vpx" ? "Importing" : "Importing Archive")
        }

        let isSecurityScoped = url.startAccessingSecurityScopedResource()
        defer {
            if isSecurityScoped {
                url.stopAccessingSecurityScopedResource()
            }
        }

        if await Task.detached(
            priority: .userInitiated,
            operation: {
                VPinballImportTable(url.path.cstring)
            }
        ).value != VPinballStatus.success.rawValue {
            VPinballManager.log(.error, "Failed to import table: \(url.lastPathComponent)")
        }

        await MainActor.run { vpinballViewModel.hideHUD() }
    }

    func deleteTable(_ table: Table) async {
        if await Task.detached(priority: .userInitiated,
                               operation: {
                                   VPinballDeleteTable(table.uuid.cstring)
                               }).value != VPinballStatus.success.rawValue
        {
            VPinballManager.log(.error, "Failed to delete table: \(table.name)")
        }
    }

    func renameTable(_ table: Table, newName: String) async {
        if await Task.detached(priority: .userInitiated,
                               operation: {
                                   VPinballRenameTable(table.uuid.cstring, newName.cstring)
                               }).value != VPinballStatus.success.rawValue
        {
            VPinballManager.log(.error, "Failed to rename table: \(table.name)")
        }
    }

    func shareTable(_ table: Table) async -> URL? {
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
            VPinballManager.log(.error, "Failed to export table: \(table.name)")
            return nil
        }
    }

    func extractTableScript(_ table: Table) async -> Bool {
        await MainActor.run {
            vpinballViewModel.showProgressHUD(title: table.name,
                                              status: "Extracting")
        }

        var success = true

        if await Task.detached(
            priority: .userInitiated,
            operation: { [table] in
                let loadStatus = VPinballLoadTable(table.uuid.cstring)
                guard loadStatus == VPinballStatus.success.rawValue else {
                    return VPinballStatus.failure.rawValue
                }
                return VPinballExtractTableScript()
            }
        ).value != VPinballStatus.success.rawValue {
            success = false

            VPinballManager.log(.error, "unable to extract script from table")
        }

        await MainActor.run {
            vpinballViewModel.hideHUD()
        }

        return success
    }

    func updateTableImage(_ table: Table, image: UIImage) async {
        guard let jpegData = image.jpegData(compressionQuality: 0.8) else {
            VPinballManager.log(.error, "Failed to convert image to JPEG for table: \(table.name)")
            return
        }

        let tableBaseName = (table.fileName as NSString).deletingPathExtension
        let imageFileName = "\(tableBaseName).jpg"

        let tableDirectory = table.baseURL
        let imagePath = tableDirectory.appendingPathComponent(imageFileName).path

        let relativeImagePath = table.path.isEmpty ? imageFileName :
            "\((table.path as NSString).deletingLastPathComponent)/\(imageFileName)"

        do {
            try jpegData.write(to: URL(fileURLWithPath: imagePath))

            let status = await Task.detached(priority: .userInitiated) {
                VPinballSetTableImage(table.uuid.cstring, relativeImagePath.cstring)
            }.value

            if status == 0 {
                VPinballManager.log(.info, "Successfully updated table image for table: \(table.name)")
                await loadTables()
            } else {
                VPinballManager.log(.error, "Failed to update  table image in database for table: \(table.name)")
            }
        } catch {
            VPinballManager.log(.error, "Failed to write  table image file for table: \(table.name) - \(error)")
        }
    }

    func resetTableImage(_ table: Table) async {
        let status = await Task.detached(priority: .userInitiated) {
            VPinballSetTableImage(table.uuid.cstring, "".cstring)
        }.value

        if status == 0 {
            VPinballManager.log(.info, "Successfully reset table image for table: \(table.name)")
            await loadTables()
        } else {
            VPinballManager.log(.error, "Failed to reset table image in database for table: \(table.name)")
        }
    }

    func updateTable(_: Table) {
        // Reload table list to get fresh file flags
        Task {
            await loadTables()
        }
    }

    func filteredTables(searchText: String, sortOrder: SortOrder) -> [Table] {
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
