#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace VPinballLib {

enum class VPinballStatus;
struct VPXTable;
struct VPXTablesData;
enum class TableEvent;

class VPinballTableManager {
public:
   VPinballTableManager();
   VPinballTableManager(const std::string& prefPath);
   ~VPinballTableManager();

   VPinballStatus LoadFromJson();
   VPinballStatus SaveToJson(const std::string& jsonPath);
   VPinballStatus RefreshTables();
   


   void SetEventCallback(std::function<void(TableEvent, const VPXTable*)> callback);
   std::function<void(TableEvent, const VPXTable*)> GetEventCallback() const { return m_eventCallback; }
   
   VPinballStatus AddTable(const std::string& filePath);
   VPinballStatus DeleteTable(const std::string& uuid);
   VPinballStatus RenameTable(const std::string& uuid, const std::string& newName);
   VPinballStatus SetTableArtwork(const std::string& uuid, const std::string& artworkPath);
   const std::string& GetTablesPath();
   
   VPXTable* GetTable(const std::string& uuid);
   void GetAllTables(VPXTablesData& tablesData);
   size_t GetTableCount();
   void ClearAll();
   
   VPinballStatus ImportTable(const std::string& sourceFile);
   std::string ExportTable(const std::string& uuid);
   
   VPinballStatus ScanDirectory(const std::string& path);
   VPinballStatus ReconcileWithFilesystem();
   

   bool ValidateJson(const std::string& jsonString) const;
   
   char* GetTablesJsonCopy();
   char* GetTableJsonCopy(const std::string& uuid);
   
   std::string GetTablesJsonPath();
   
   VPinballStatus CompressDirectory(const std::string& sourcePath, const std::string& destinationPath);
   VPinballStatus UncompressArchive(const std::string& archivePath);
   
private:
   std::vector<VPXTable> m_tables;
   std::function<void(TableEvent, const VPXTable*)> m_eventCallback;
   std::string m_tablesPath;
   
   VPXTable* CreateTableFromFile(const std::string& filePath);
   void CleanupTable(VPXTable& table);
   std::string GenerateUUID() const;
   void NotifyEvent(TableEvent event, const VPXTable* table = nullptr);
   VPXTable* FindTableByPath(const std::string& fullPath);
   VPXTable* FindTableByUUID(const std::string& uuid);
   VPinballStatus ExtractZipToDirectory(const std::string& zipFile, const std::string& destDir);
   std::string SanitizeTableName(const std::string& name);
   std::string GetUniqueTableFolder(const std::string& baseName, const std::string& tablesPath);
};

}