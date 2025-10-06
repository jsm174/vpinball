#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include "../include/vpinball/VPinballLib_C.h"

namespace VPinballLib {

struct Table {
   string uuid;
   string name;
   string path;
   string image;
   int64_t createdAt;
   int64_t modifiedAt;
};

class TableManager
{
public:
   TableManager();
   ~TableManager();

   void Init();
   void Reset();
   void Refresh();

   const string& GetTablesPath();
   string GetTables();
   Table* GetTable(const string& uuid) const;
   string GetTableImagePath(const string& uuid);

   Table* AddTable(const string& path);
   bool DeleteTable(const string& uuid);
   bool RenameTable(const string& uuid, const string& newName);
   bool SetTableImage(const string& uuid, const string& path);

   bool ImportTable(const string& path);
   string ExportTable(const string& uuid);

   string StageTable(const string& uuid);
   void CleanupLoadedTable(const string& uuid);
   bool SaveTableFile(const string& uuid, const string& filename, const string& path);
   void CleanupCaches();

private:
   void LoadTables();
   void SaveTables();

   Table* CreateTableFromFile(const string& path);
   Table* FindTableByPath(const string& path);

   string GenerateUUID() const;
   string SanitizeTableName(const string& name);
   string GetUniqueTableFolder(const string& baseName, const string& path);

   vector<Table> m_tables;
   string m_tablesPath;
   string m_tablesJSONPath;
   bool m_requiresStaging;
   string m_loadedTableUuid;
   string m_loadedTableWorkingDir;
};

}