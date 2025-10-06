#include "core/stdafx.h"
#include "TableManager.h"
#include "FileSystem.h"
#include "VPinballLib.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <random>
#include <cerrno>
#include <cstring>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <stduuid/uuid.h>
#include <zip.h>

using json = nlohmann::json;

namespace VPinballLib {

TableManager::TableManager()
{
}

TableManager::~TableManager()
{
}

void TableManager::Init()
{
   FileSystem::Init();

   Reset();
}

void TableManager::Reset()
{
   m_tables.clear();

   string tablesPath = g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "TablesPath"s, ""s);

   if (tablesPath.empty())
      m_tablesPath = string(g_pvp->m_myPrefPath) + "tables" + PATH_SEPARATOR_CHAR;
   else {
      m_tablesPath = tablesPath;

      if (!m_tablesPath.ends_with(PATH_SEPARATOR_CHAR))
         m_tablesPath += PATH_SEPARATOR_CHAR;
   }

   m_requiresStaging = FileSystem::IsSAFUri(m_tablesPath);

   if (m_requiresStaging) {
      PLOGI.printf("Using staged storage: %s", m_tablesPath.c_str());
   }
   else {
      PLOGI.printf("Using tables path: %s", m_tablesPath.c_str());
   }

   if (!m_requiresStaging) {
      if (!FileSystem::Exists(m_tablesPath)) {
         if (!FileSystem::CreateDirectories(m_tablesPath)) {
            PLOGE.printf("Could not create tables path: %s", m_tablesPath.c_str());
            return;
         }
         else {
            PLOGI.printf("Created tables path: %s", m_tablesPath.c_str());
         }
      }
   }

   m_tablesJSONPath = m_tablesPath + "tables.json";

   LoadTables();

   PLOGI.printf("Total tables: %zu", m_tables.size());
   return;
}

void TableManager::Refresh()
{
   LoadTables();
}

const string& TableManager::GetTablesPath()
{
   return m_tablesPath;
}

string TableManager::GetTables()
{
   json tablesArray = json::array();
   for (const auto& table : m_tables) {
      json tableJson;
      tableJson["uuid"] = table.uuid;
      tableJson["name"] = table.name;
      tableJson["path"] = table.path;
      tableJson["image"] = table.image;
      tableJson["createdAt"] = static_cast<long long>(table.createdAt);
      tableJson["modifiedAt"] = static_cast<long long>(table.modifiedAt);

      tablesArray.push_back(tableJson);
   }

   json j;
   j["tables"] = tablesArray;
   j["tableCount"] = static_cast<int>(m_tables.size());

   return j.dump();
}

Table* TableManager::GetTable(const string& uuid) const
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(),
                         [&uuid](const Table& table) {
                            return table.uuid == uuid;
                         });
   return (it != m_tables.end()) ? const_cast<Table*>(&(*it)) : nullptr;
}

string TableManager::GetTableImagePath(const string& uuid)
{
   Table* table = GetTable(uuid);
   if (!table) {
      return "";
   }

   if (!table->image.empty()) {
      string imagePath = FileSystem::JoinPath(m_tablesPath, table->image);
      if (FileSystem::Exists(imagePath)) {
         return imagePath;
      }
   }

   return "";
}

Table* TableManager::AddTable(const string& path)
{
   Table* table = FindTableByPath(path);
   if (table)
      return table;

   table = CreateTableFromFile(path);
   if (table) {
      m_tables.push_back(*table);
      delete table;
      return &m_tables.back();
   }

   return nullptr;
}

bool TableManager::DeleteTable(const string& uuid)
{
   Table* table = GetTable(uuid);
   if (!table)
      return false;

   string tablePath = FileSystem::JoinPath(m_tablesPath, table->path);
   string tableDir = FileSystem::GetParentPath(tablePath);

   if (tableDir.empty() || tableDir == m_tablesPath || tableDir + PATH_SEPARATOR_CHAR == m_tablesPath)
      return false;

   if (!FileSystem::Exists(tableDir)) {
      PLOGI.printf("Table directory does not exist, only removing from registry");
   }
   else {
      int vpxCount = 0;
      vector<string> vpxFiles;
      vector<string> entries = FileSystem::ListDirectory(tableDir, m_tablesPath);

      for (const auto& entry : entries) {
         string fileExt = FileSystem::GetExtension(entry);
         std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);

         if (fileExt == ".vpx") {
            vpxCount++;
            vpxFiles.push_back(entry);
         }
      }

      if (vpxCount <= 1) {
         if (!FileSystem::Delete(tableDir, m_tablesPath)) {
            PLOGE.printf("Failed to delete directory: %s", tableDir.c_str());
            return false;
         }
      }
      else {
         if (!FileSystem::Delete(tablePath, m_tablesPath)) {
            PLOGE.printf("Failed to delete VPX file: %s", tablePath.c_str());
            return false;
         }
         PLOGI.printf("Successfully deleted VPX file: %s", tablePath.c_str());
      }
   }

   for (auto it = m_tables.begin(); it != m_tables.end(); ++it) {
      if (it->uuid == uuid) {
         m_tables.erase(it);
         SaveTables();
         return true;
      }
   }

   return false;
}

bool TableManager::RenameTable(const string& uuid, const string& newName)
{
   Table* table = GetTable(uuid);
   if (!table)
      return false;

   table->name = newName;
   table->modifiedAt = std::time(nullptr);

   SaveTables();

   return true;
}

bool TableManager::SetTableImage(const string& uuid, const string& path)
{
   PLOGI.printf("Setting image for UUID: %s to: %s", uuid.c_str(), path.c_str());

   Table* table = GetTable(uuid);
   if (!table) {
      PLOGE.printf("Table not found with UUID: %s", uuid.c_str());
      return false;
   }

   if (path.empty()) {
      if (!table->image.empty()) {
         string currentImagePath = FileSystem::JoinPath(m_tablesPath, table->image);
         if (FileSystem::Exists(currentImagePath)) {
            if (FileSystem::Delete(currentImagePath, m_tablesPath)) {
               PLOGI.printf("Removed image file: %s", currentImagePath.c_str());
            }
            else {
               PLOGE.printf("Failed to remove image file: %s", currentImagePath.c_str());
            }
         }
      }
      table->image.clear();
   }
   else if (path.find(PATH_SEPARATOR_CHAR) == 0 || (path.length() >= 2 && path[1] == ':')) {
      string fullPath = FileSystem::JoinPath(m_tablesPath, table->path);
      string baseName = FileSystem::GetStem(fullPath);

      string workingDir;
      if (uuid == m_loadedTableUuid && !m_loadedTableWorkingDir.empty()) {
         workingDir = m_loadedTableWorkingDir;
      }
      else {
         workingDir = FileSystem::GetParentPath(fullPath);
         if (workingDir.empty())
            workingDir = ".";
      }

      string destPath = workingDir + PATH_SEPARATOR_CHAR + baseName + ".jpg";

      if (!FileSystem::CopyFile(path, destPath, m_tablesPath)) {
         PLOGE.printf("Failed to copy image file to: %s", destPath.c_str());
         return false;
      }

      table->image = FileSystem::GetRelativePath(destPath, m_tablesPath);

      PLOGI.printf("Copied image to: %s", destPath.c_str());
   }
   else {
      table->image = path;
   }

   table->modifiedAt = std::time(nullptr);
   SaveTables();

   PLOGI.printf("Successfully set image for table: %s", table->name.c_str());
   return true;
}

// Imports a table from a file path (.vpx or .vpxz archive)
// For .vpxz: extracts to temp, finds all VPX files, copies each with directory structure to unique folder
// For .vpx: copies single file to new unique folder
// Creates table entries and adds to registry, returns true on success

bool TableManager::ImportTable(const string& path)
{
   if (!FileSystem::Exists(path))
      return false;

   string ext = FileSystem::GetExtension(path);
   std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

   string name = FileSystem::GetStem(path);
   std::replace(name.begin(), name.end(), '_', ' ');

   PLOGI.printf("Importing %s (extension: %s)", path.c_str(), ext.c_str());

   string tablesPath = m_tablesPath;

   try {
      if (ext == ".vpxz") {
         PLOGI.printf("Extracting VPXZ archive: %s", path.c_str());

         string tempDir = FileSystem::GetTempDirectoryPath() + "/vpinball_import_" + std::to_string(std::time(nullptr));
         FileSystem::CreateDirectories(tempDir);

         if (!FileSystem::ExtractZipToDirectory(path, tempDir)) {
            FileSystem::RemoveAll(tempDir);
            return false;
         }

         vector<string> vpxFiles = FileSystem::ListFilesRecursiveLocal(tempDir, ".vpx");

         if (vpxFiles.empty()) {
            PLOGE.printf("No VPX files found in archive");
            FileSystem::RemoveAll(tempDir);
            return false;
         }

         bool result = true;
         string lastUuid;
         for (const string& vpxFile : vpxFiles) {
            string vpxName = FileSystem::GetStem(vpxFile);
            string sourceDir = FileSystem::GetParentPath(vpxFile);
            string fileName = FileSystem::GetFileName(vpxFile);

            if (sourceDir.empty())
               sourceDir = ".";

            string folderName = GetUniqueTableFolder(vpxName, tablesPath);
            string destFolder = FileSystem::JoinPath(tablesPath, folderName);
            FileSystem::CreateDirectories(destFolder);

            bool success = FileSystem::CopyDirectory(sourceDir, destFolder, m_tablesPath);
            if (!success) {
               PLOGE.printf("Failed to copy directory structure from %s to %s",
                  sourceDir.c_str(), destFolder.c_str());
               result = false;
               continue;
            }

            PLOGI.printf("Copied entire directory structure from %s to %s",
               sourceDir.c_str(), destFolder.c_str());

            string destFile = FileSystem::JoinPath(destFolder, fileName);

            Table* addedTable = AddTable(destFile);
            if (!addedTable)
               result = false;
            else {
               lastUuid = addedTable->uuid;
               PLOGI.printf("Successfully imported VPX from archive: %s", vpxName.c_str());
            }
         }
         FileSystem::RemoveAll(tempDir);

         if (result)
            SaveTables();

         return result;
      }
      else if (ext == ".vpx") {
         PLOGI.printf("Importing single VPX file: %s", path.c_str());

         string folderName = GetUniqueTableFolder(name, tablesPath);
         string destFolder = FileSystem::JoinPath(tablesPath, folderName);
         FileSystem::CreateDirectories(destFolder);

         string fileName = FileSystem::GetFileName(path);
         string destFile = FileSystem::JoinPath(destFolder, fileName);

         bool success = FileSystem::CopyFile(path, destFile, m_tablesPath);
         if (!success) {
            PLOGE.printf("Failed to copy file: %s", path.c_str());
            return false;
         }

         Table* addedTable = AddTable(destFile);
         if (addedTable) {
            SaveTables();
            PLOGI.printf("Successfully imported table: %s", name.c_str());
            return true;
         }

         return false;
      }

      PLOGE.printf("Unsupported file extension: %s", ext.c_str());
      return false;
   }

   catch (const std::exception& e) {
      PLOGE.printf("Exception: %s", e.what());
      return false;
   }
}

string TableManager::ExportTable(const string& uuid)
{
   PLOGI.printf("Exporting table with UUID: %s", uuid.c_str());

   Table* table = GetTable(uuid);
   if (!table) {
      PLOGE.printf("Table not found with UUID: %s", uuid.c_str());
      return "";
   }

   string sanitizedName = SanitizeTableName(table->name);

   string tempDir = FileSystem::GetTempDirectoryPath();
   string tempFile = tempDir + PATH_SEPARATOR_CHAR + sanitizedName + ".vpxz";

   if (FileSystem::Exists(tempFile)) {
      FileSystem::Delete(tempFile, "");
   }

   string tableDirToCompress;
   string tableDir = FileSystem::GetParentPath(table->path);

   if (m_requiresStaging) {
      PLOGI.printf("Staged storage detected, copying to temp directory first");

      string stagingBaseDir = tempDir + PATH_SEPARATOR_CHAR + "staged_export";
      string stagingTableDir = stagingBaseDir + PATH_SEPARATOR_CHAR + tableDir;

      PLOGI.printf("Copying table directory to: %s", stagingTableDir.c_str());

      if (FileSystem::Exists(stagingTableDir)) {
         FileSystem::RemoveAll(stagingTableDir);
      }

      if (!FileSystem::CreateDirectories(stagingTableDir)) {
         PLOGE.printf("Failed to create temp export directory");
         return "";
      }

      string sourceTableDir = m_tablesPath + tableDir;

      if (!FileSystem::CopyDirectory(sourceTableDir, stagingTableDir, m_tablesPath)) {
         PLOGE.printf("Failed to copy table directory to temp");
         return "";
      }

      tableDirToCompress = stagingTableDir;
      PLOGI.printf("Table directory copied successfully");
   }
   else {
      string fullPath = FileSystem::JoinPath(m_tablesPath, table->path);
      tableDirToCompress = FileSystem::GetParentPath(fullPath);
   }

   if (!FileSystem::CompressDirectory(tableDirToCompress, tempFile)) {
      PLOGE.printf("Failed to compress table directory");
      return "";
   }

   PLOGI.printf("Successfully exported table to: %s", tempFile.c_str());
   return tempFile;
}

string TableManager::StageTable(const string& uuid)
{
   Table* table = GetTable(uuid);
   if (!table) {
      PLOGE.printf("TableManager::StageTable: Table not found");
      return "";
   }

   if (table->path.empty()) {
      PLOGE.printf("TableManager::StageTable: Table path is empty");
      return "";
   }

   string tableDir = FileSystem::GetParentPath(table->path);
   string fileName = FileSystem::GetFileName(table->path);

   if (m_requiresStaging) {
      PLOGI.printf("TableManager::StageTable: Staging required, copying to cache");

      string cachePath = string(g_pvp->m_myPrefPath) + "staging_cache" + PATH_SEPARATOR_CHAR + tableDir;

      if (FileSystem::Exists(cachePath)) {
         FileSystem::RemoveAll(cachePath);
      }

      FileSystem::CreateDirectories(cachePath);

      if (!FileSystem::CopyDirectory(m_tablesPath + tableDir, cachePath, m_tablesPath)) {
         PLOGE.printf("TableManager::StageTable: Failed to copy to cache");
         return "";
      }

      m_loadedTableUuid = uuid;
      m_loadedTableWorkingDir = cachePath;

      PLOGI.printf("TableManager::StageTable: Table staged at: %s", cachePath.c_str());
      return cachePath + PATH_SEPARATOR_CHAR + fileName;
   }

   string fullPath = FileSystem::JoinPath(m_tablesPath, table->path);
   m_loadedTableUuid = uuid;
   m_loadedTableWorkingDir = FileSystem::GetParentPath(fullPath);

   if (m_loadedTableWorkingDir.empty())
      m_loadedTableWorkingDir = ".";

   PLOGI.printf("TableManager::StageTable: Using table in place: %s", fullPath.c_str());
   return fullPath;
}

void TableManager::CleanupLoadedTable(const string& uuid)
{
   if (m_loadedTableUuid != uuid) {
      return;
   }

   if (m_requiresStaging && !m_loadedTableWorkingDir.empty()) {
      PLOGI.printf("TableManager::CleanupLoadedTable: Syncing staged changes back");

      Table* table = GetTable(uuid);
      if (table) {
         string tableDir = FileSystem::GetParentPath(table->path);

         if (!FileSystem::CopyDirectory(m_loadedTableWorkingDir, m_tablesPath + tableDir, m_tablesPath)) {
            PLOGE.printf("TableManager::CleanupLoadedTable: Failed to sync changes back");
         }
         else {
            PLOGI.printf("TableManager::CleanupLoadedTable: Successfully synced changes back");
         }
      }
   }

   m_loadedTableUuid.clear();
   m_loadedTableWorkingDir.clear();
}

bool TableManager::SaveTableFile(const string& uuid, const string& filename, const string& path)
{
   if (uuid == m_loadedTableUuid && !m_loadedTableWorkingDir.empty()) {
      string destPath = m_loadedTableWorkingDir + PATH_SEPARATOR_CHAR + filename;
      return FileSystem::CopyFile(path, destPath, m_tablesPath);
   }

   Table* table = GetTable(uuid);
   if (!table) {
      PLOGE.printf("TableManager::SaveTableFile: Table not found");
      return false;
   }

   string fullPath = FileSystem::JoinPath(m_tablesPath, table->path);
   string tableDir = FileSystem::GetParentPath(fullPath);

   if (tableDir.empty())
      tableDir = ".";

   string destPath = tableDir + PATH_SEPARATOR_CHAR + filename;
   return FileSystem::CopyFile(path, destPath, m_tablesPath);
}

void TableManager::CleanupCaches()
{
   string cachePath = string(g_pvp->m_myPrefPath) + "staging_cache";
   if (FileSystem::Exists(cachePath)) {
      if (FileSystem::RemoveAll(cachePath)) {
         PLOGI.printf("TableManager::CleanupCaches: Cleaned up staging cache: %s", cachePath.c_str());
      }
   }

   string tempScreenshot = string(g_pvp->m_myPrefPath) + "temp_screenshot.jpg";
   if (FileSystem::Exists(tempScreenshot)) {
      FileSystem::Delete(tempScreenshot, "");
   }
}

// 1) Load from tables.json
// 2) Remove duplicate uuids and tables that don't exist
// 3) Re-add all the tables found in the folder
// 4) Save tables.json

void TableManager::LoadTables()
{
   m_tables.clear();

   if (FileSystem::Exists(m_tablesJSONPath)) {
      try {
         string content = FileSystem::ReadFile(m_tablesJSONPath, m_tablesPath);
         if (!content.empty()) {
            json jsonData;

            try {
               jsonData = json::parse(content);
            }
            catch (const json::parse_error& e) {
               PLOGE.printf("Unable to parse tables.json: error=%s", e.what());
               return;
            }

            if (jsonData.contains("tables") && jsonData["tables"].is_array()) {
               for (const auto& tableJson : jsonData["tables"]) {
                  Table table;

                  table.uuid = tableJson.value("uuid", "");
                  table.name = tableJson.value("name", "");
                  table.path = tableJson.value("path", "");
                  table.image = tableJson.value("image", "");
                  table.createdAt = tableJson.value("createdAt", 0LL);
                  table.modifiedAt = tableJson.value("modifiedAt", 0LL);

                  m_tables.push_back(table);
               }
            }
         }
      }

      catch (...) {
      }
   }

   std::unordered_set<string> seen;
   auto it = m_tables.begin();
   while (it != m_tables.end()) {
      if (it->uuid.empty() || !seen.insert(it->uuid).second) {
         it = m_tables.erase(it);
         continue;
      }

      string fullPath = FileSystem::JoinPath(m_tablesPath, it->path);
      if (!fullPath.empty() && !FileSystem::Exists(fullPath)) {
         PLOGI.printf("Removing table with missing file: %s", fullPath.c_str());
         it = m_tables.erase(it);
         continue;
      }

      if (it->image.empty()) {
         string parentPath = FileSystem::GetParentPath(fullPath);
         string stem = FileSystem::GetStem(fullPath);
         string pngPath = parentPath + PATH_SEPARATOR_CHAR + stem + ".png";
         string jpgPath = parentPath + PATH_SEPARATOR_CHAR + stem + ".jpg";

         if (FileSystem::Exists(pngPath)) {
            it->image = FileSystem::GetRelativePath(pngPath, m_tablesPath);
         }
         else if (FileSystem::Exists(jpgPath)) {
            it->image = FileSystem::GetRelativePath(jpgPath, m_tablesPath);
         }
      }

      ++it;
   }

   vector<string> vpxFiles = FileSystem::ListFilesRecursive(m_tablesPath, ".vpx", m_tablesPath);
   for (const string& filePath : vpxFiles)
      AddTable(filePath);

   SaveTables();

   PLOGI.printf("%zu tables", m_tables.size());
}

void TableManager::SaveTables()
{
   try {
      json tablesArray = json::array();

      vector<Table> sortedTables = m_tables;
      std::sort(sortedTables.begin(), sortedTables.end(),
         [](const Table& a, const Table& b) {
            string nameA = a.name;
            string nameB = b.name;
            std::transform(nameA.begin(), nameA.end(), nameA.begin(), ::tolower);
            std::transform(nameB.begin(), nameB.end(), nameB.begin(), ::tolower);
            return nameA < nameB;
         });

      for (const auto& table : sortedTables) {
         json tableJson;
         tableJson["uuid"] = table.uuid;
         tableJson["name"] = table.name;
         tableJson["path"] = table.path;
         tableJson["image"] = table.image;
         tableJson["createdAt"] = (long long)table.createdAt;
         tableJson["modifiedAt"] = (long long)table.modifiedAt;

         tablesArray.push_back(tableJson);
      }

      json j;
      j["tableCount"] = (int)m_tables.size();
      j["tables"] = tablesArray;

      string jsonString = j.dump(2);
      if (!FileSystem::WriteFile(m_tablesJSONPath, jsonString, m_tablesPath)) {
          PLOGI.printf("Unable to save tables.json");
      }
   }

   catch (...) {
   }
}

Table* TableManager::CreateTableFromFile(const string& path)
{
   Table* table = new Table();
   table->uuid = GenerateUUID();
   table->path = FileSystem::GetRelativePath(path, m_tablesPath);

   string name = FileSystem::GetStem(path);
   std::replace(name.begin(), name.end(), '_', ' ');
   table->name = name;

   table->modifiedAt = std::time(nullptr);
   table->createdAt = table->modifiedAt;

   string parentPath = FileSystem::GetParentPath(path);
   string stem = FileSystem::GetStem(path);
   string pngPath = parentPath + PATH_SEPARATOR_CHAR + stem + ".png";
   string jpgPath = parentPath + PATH_SEPARATOR_CHAR + stem + ".jpg";

   if (FileSystem::Exists(pngPath))
      table->image = FileSystem::GetRelativePath(pngPath, m_tablesPath);
   else if (FileSystem::Exists(jpgPath))
      table->image = FileSystem::GetRelativePath(jpgPath, m_tablesPath);

   return table;
}

Table* TableManager::FindTableByPath(const string& path)
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(),
                         [this, &path](const Table& table) {
                            return FileSystem::JoinPath(m_tablesPath, table.path) == path;
                         });
   return (it != m_tables.end()) ? &(*it) : nullptr;
}

string TableManager::GenerateUUID() const
{
   static thread_local std::random_device rd;
   static thread_local std::mt19937 generator(rd());
   static thread_local uuids::uuid_random_generator uuidGen(generator);

   string uuid;
   while (true) {
      uuid = uuids::to_string(uuidGen());
      if (GetTable(uuid) == nullptr)
         return uuid;

      PLOGW.printf("UUID collision detected: %s, regenerating", uuid.c_str());
   }
}

string TableManager::SanitizeTableName(const string& name)
{
   string sanitized = name;

   for (char& c : sanitized) {
      if (c == ' ' || c == '_' || c == '/' || c == '\\' || c == ':' ||
          c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
          c == '|' || c == '.' || c == '&' || c == '\'' || c == '(' || c == ')') {
         c = '-';
      }
   }

   string result;
   bool lastWasHyphen = false;
   for (char c : sanitized) {
      if (c == '-') {
         if (!lastWasHyphen) {
            result += c;
            lastWasHyphen = true;
         }
      }
      else {
         result += c;
         lastWasHyphen = false;
      }
   }

   while (!result.empty() && result.front() == '-') {
      result.erase(0, 1);
   }
   while (!result.empty() && result.back() == '-') {
      result.pop_back();
   }

   if (result.empty()) {
      result = "table";
   }

   return result;
}

string TableManager::GetUniqueTableFolder(const string& baseName, const string& tablesPath)
{
   string sanitizedName = SanitizeTableName(baseName);
   string candidate = sanitizedName;
   int counter = 2;

   while (FileSystem::Exists(FileSystem::JoinPath(tablesPath, candidate))) {
      candidate = sanitizedName + "-" + std::to_string(counter);
      counter++;
   }

   return candidate;
}

}
