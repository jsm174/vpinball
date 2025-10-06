#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include "../include/vpinball/VPinballLib_C.h"

namespace VPinballLib {

struct Table {
   char* uuid;
   char* name;
   char* fileName;
   char* fullPath;
   char* path;
   char* image;
   int64_t createdAt;
   int64_t modifiedAt;
};

struct TablesData {
   Table* tables;
   int tableCount;
   bool success;
};

class TableManager {
public:
   TableManager();
   ~TableManager();

   VPINBALL_STATUS Start();
   VPINBALL_STATUS Load();
   VPINBALL_STATUS Reconcile();
   VPINBALL_STATUS Save();
   VPINBALL_STATUS RefreshTables();

   VPINBALL_STATUS AddTable(const string& filePath, string* outUuid = nullptr);
   VPINBALL_STATUS DeleteTable(const string& uuid);
   VPINBALL_STATUS RenameTable(const string& uuid, const string& newName);
   VPINBALL_STATUS SetTableImage(const string& uuid, const string& imagePath);
   const string& GetTablesPath();

   Table* GetTable(const string& uuid);
   string GetTableImageFullPath(const string& uuid);
   void GetAllTables(TablesData& tablesData);
   size_t GetTableCount();
   void ClearAll();

   VPINBALL_STATUS ImportTable(const string& sourceFile);
   string ExportTable(const string& uuid);

   VPINBALL_STATUS ScanDirectory(const string& path);

   VPINBALL_STATUS ReloadTablesPath();


   bool ValidateJson(const string& jsonString) const;

   const char* GetTablesJsonCopy();

   VPINBALL_STATUS CompressDirectory(const string& sourcePath, const string& destinationPath);
   VPINBALL_STATUS UncompressArchive(const string& archivePath);

private:
   vector<Table> m_tables;
   string m_tablesPath;
   string m_tablesJsonPath;

   Table* CreateTableFromFile(const string& filePath);
   void CleanupTable(Table& table);
   string GenerateUUID() const;
   Table* FindTableByPath(const string& fullPath);
   Table* FindTableByUUID(const string& uuid);
   void RemoveDuplicateUUIDs();
   VPINBALL_STATUS ExtractZipToDirectory(const string& zipFile, const string& destDir);
   string SanitizeTableName(const string& name);
   string GetUniqueTableFolder(const string& baseName, const string& tablesPath);
};

}