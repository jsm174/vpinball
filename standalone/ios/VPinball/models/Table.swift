import UIKit

struct Table: Codable, Identifiable, Hashable {
    let uuid: String
    let name: String
    let path: String
    let image: String
    let createdAt: Int64
    let modifiedAt: Int64

    var id: String { uuid }

    var tableId: UUID {
        UUID(uuidString: uuid) ?? UUID()
    }

    var fileName: String {
        (path as NSString).lastPathComponent
    }

    var uiImage: UIImage? {
        guard !image.isEmpty else { return nil }
        return UIImage(contentsOfFile: imagePath)
    }

    var imagePath: String {
        let tablesPath = String(cString: VPinballGetTablesPath())
        return (tablesPath as NSString).appendingPathComponent(image)
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

    func exists() -> Bool {
        return VPinballFileExists(fullPath.cstring)
    }

    func hasScriptFile() -> Bool {
        return VPinballFileExists(scriptPath.cstring)
    }

    func hasIniFile() -> Bool {
        return VPinballFileExists(iniPath.cstring)
    }

    func hasImageFile() -> Bool {
        return VPinballFileExists(imagePath.cstring)
    }
}

struct TablesResponse: Codable {
    let tableCount: Int
    let tables: [Table]
}
