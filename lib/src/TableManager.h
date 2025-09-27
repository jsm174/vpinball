#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include "../include/vpinball/VPinballLib_C.h"

namespace VPinballLib {

struct VPXTable {
   char* uuid;
   char* name;
   char* fileName;
   char* fullPath;
   char* path;
   char* artwork;
   int64_t createdAt;
   int64_t modifiedAt;
};

struct TableInfo {
   char* tableId;
   char* name;
};

struct TablesData {
   TableInfo* tables;
   int tableCount;
   bool success;
};

struct VPXTablesData {
   VPXTable* tables;
   int tableCount;
   bool success;
};

class TableManager {
public:
   TableManager();
   ~TableManager();

   VPINBALL_STATUS LoadFromJson();
   VPINBALL_STATUS SaveToJson(const std::string& jsonPath);
   VPINBALL_STATUS RefreshTables();

   void SetSystemEventCallback(std::function<void*(VPINBALL_EVENT, void*)> callback);
   void SendSystemEvent(VPINBALL_EVENT event, void* data);

   VPINBALL_STATUS AddTable(const std::string& filePath, std::string* outUuid = nullptr);
   VPINBALL_STATUS DeleteTable(const std::string& uuid);
   VPINBALL_STATUS RenameTable(const std::string& uuid, const std::string& newName);
   VPINBALL_STATUS SetTableArtwork(const std::string& uuid, const std::string& artworkPath);
   const std::string& GetTablesPath();

   VPXTable* GetTable(const std::string& uuid);
   void GetAllTables(VPXTablesData& tablesData);
   size_t GetTableCount();
   void ClearAll();

   VPINBALL_STATUS ImportTable(const std::string& sourceFile);
   std::string ExportTable(const std::string& uuid);

   VPINBALL_STATUS ScanDirectory(const std::string& path);
   VPINBALL_STATUS ReconcileWithFilesystem();
   VPINBALL_STATUS ReloadTablesPath();
   void CompleteDeferredInitialization();


   bool ValidateJson(const std::string& jsonString) const;

   const char* GetTablesJsonCopy();
   char* GetTableJsonCopy(const std::string& uuid);

   std::string GetTablesJsonPath();

   VPINBALL_STATUS CompressDirectory(const std::string& sourcePath, const std::string& destinationPath);
   VPINBALL_STATUS UncompressArchive(const std::string& archivePath);

private:
   std::vector<VPXTable> m_tables;
   std::function<void*(VPINBALL_EVENT, void*)> m_systemEventCallback;
   std::string m_tablesPath;

   VPXTable* CreateTableFromFile(const std::string& filePath);
   void CleanupTable(VPXTable& table);
   std::string GenerateUUID() const;
   VPXTable* FindTableByPath(const std::string& fullPath);
   VPXTable* FindTableByUUID(const std::string& uuid);
   VPINBALL_STATUS ExtractZipToDirectory(const std::string& zipFile, const std::string& destDir);
   std::string SanitizeTableName(const std::string& name);
   std::string GetUniqueTableFolder(const std::string& baseName, const std::string& tablesPath);
};

}