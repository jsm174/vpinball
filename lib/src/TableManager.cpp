#include "core/stdafx.h"
#include "TableManager.h"
#include "VPinballLib.h"
#include "FileSystem.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <random>
#include <cerrno>
#include <cstring>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <stduuid/uuid.h>
#include <zip.h>

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

using json = nlohmann::json;

namespace VPinballLib {

TableManager::TableManager()
{
}

TableManager::~TableManager()
{
   ClearAll();
}

VPINBALL_STATUS TableManager::Start()
{
   ClearAll();

   string tablesPath = g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "TablesPath"s, ""s);

   if (tablesPath.empty())
      m_tablesPath = string(g_pvp->m_myPrefPath) + "tables" + PATH_SEPARATOR_CHAR;
   else {
      m_tablesPath = tablesPath;

      if (!m_tablesPath.ends_with(PATH_SEPARATOR_CHAR))
         m_tablesPath += PATH_SEPARATOR_CHAR;
   }

   if (FileSystem::IsSAFUri(m_tablesPath)) {
      PLOGI.printf("Using SAF external storage: %s", m_tablesPath.c_str());
   }
   else {
      PLOGI.printf("Using tables path: %s", m_tablesPath.c_str());
   }

   FileSystem::SetTablesPath(m_tablesPath);

   if (!FileSystem::IsSAFUri(m_tablesPath)) {
      if (!FileSystem::Exists(m_tablesPath)) {
         if (!FileSystem::CreateDirectories(m_tablesPath)) {
            PLOGE.printf("Could not create tables path: %s", m_tablesPath.c_str());
            return VPINBALL_STATUS_FAILURE;
         } else {
            PLOGI.printf("Created tables path: %s", m_tablesPath.c_str());
         }
      }
   }

   m_tablesJsonPath = m_tablesPath + "tables.json";

   Load();

   VPINBALL_STATUS status = Reconcile();
   if (status != VPINBALL_STATUS_SUCCESS)
      return status;

   PLOGI.printf("Total tables: %zu", m_tables.size());
   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS TableManager::Load()
{
   if (!FileSystem::Exists(m_tablesJsonPath))
      return VPINBALL_STATUS_SUCCESS;

   try {
      string content = FileSystem::ReadFile(m_tablesJsonPath);

      if (content.empty())
         return VPINBALL_STATUS_SUCCESS;
      
      json jsonData;

      try {
         jsonData = json::parse(content);
      } 
      
      catch (const json::parse_error& e) {
         PLOGE.printf("Unable to parse tables.json: error=%s", e.what());
         return VPINBALL_STATUS_FAILURE;
      }
      
      if (!jsonData.contains("tables") || !jsonData["tables"].is_array()) {
         PLOGE.printf("Unable to parse tables.json: missing tables array");
         return VPINBALL_STATUS_FAILURE;
      }
      
      for (const auto& tableJson : jsonData["tables"]) {
         Table table;
         memset(&table, 0, sizeof(Table));

         string uuid = tableJson.value("uuid", "");
         string name = tableJson.value("name", "");
         string path = tableJson.value("path", "");
         string image = tableJson.value("image", "");

         string fullPath = FileSystem::JoinPath(m_tablesPath, path);

         string fileName = path.empty() ? "" : std::filesystem::path(path).filename().string();

         table.uuid = new char[uuid.length() + 1];
         strcpy(table.uuid, uuid.c_str());
         table.name = new char[name.length() + 1];
         strcpy(table.name, name.c_str());
         table.fileName = new char[fileName.length() + 1];
         strcpy(table.fileName, fileName.c_str());
         table.fullPath = new char[fullPath.length() + 1];
         strcpy(table.fullPath, fullPath.c_str());
         table.path = new char[path.length() + 1];
         strcpy(table.path, path.c_str());
         table.image = new char[image.length() + 1];
         strcpy(table.image, image.c_str());
         
         table.createdAt = tableJson.value("createdAt", 0LL);
         table.modifiedAt = tableJson.value("modifiedAt", 0LL);

         m_tables.push_back(table);
      }

      RemoveDuplicateUUIDs();

      return VPINBALL_STATUS_SUCCESS;

   } catch (const std::exception& e) {
      PLOGI.printf("TableManager::Load: Exception: %s", e.what());
      return VPINBALL_STATUS_FAILURE;
   }
}

VPINBALL_STATUS TableManager::Reconcile()
{
   PLOGI.printf("Starting reconcile: %s", m_tablesPath.c_str());

   VPINBALL_STATUS scanStatus = ScanDirectory(m_tablesPath);
   if (scanStatus != VPINBALL_STATUS_SUCCESS)
      return scanStatus;
   
   auto it = m_tables.begin();
   while (it != m_tables.end()) {
      if (it->fullPath && !FileSystem::Exists(it->fullPath)) {
         CleanupTable(*it);
         it = m_tables.erase(it);
      } else {
         if (!it->image || it->image[0] == '\0') {
            std::filesystem::path tablePath(it->fullPath);
            std::filesystem::path basePath = tablePath.parent_path() / tablePath.stem();

            string imageFile;
            if (FileSystem::Exists(basePath.string() + ".png")) {
               std::filesystem::path imagePath = std::filesystem::relative(basePath.string() + ".png", m_tablesPath);
               imageFile = imagePath.string();
            } else if (FileSystem::Exists(basePath.string() + ".jpg")) {
               std::filesystem::path imagePath = std::filesystem::relative(basePath.string() + ".jpg", m_tablesPath);
               imageFile = imagePath.string();
            }

            if (!imageFile.empty()) {
               if (it->image)
                  delete[] it->image;
               it->image = new char[imageFile.length() + 1];
               strcpy(it->image, imageFile.c_str());
            }
         }

         ++it;
      }
   }

   VPINBALL_STATUS saveStatus = Save();
   if (saveStatus != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::Reconcile: Failed to save updated registry");
   }

   PLOGI.printf("TableManager::Reconcile: Reconciliation complete - %zu tables", m_tables.size());
   
   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS TableManager::Save()
{
   try {
      json j;
      j["success"] = true;
      j["tableCount"] = (int)m_tables.size();
      
      json tablesArray = json::array();
      
      vector<Table> sortedTables = m_tables;
      std::sort(sortedTables.begin(), sortedTables.end(), 
         [](const Table& a, const Table& b) {
            string nameA = a.name ? a.name : "";
            string nameB = b.name ? b.name : "";
            std::transform(nameA.begin(), nameA.end(), nameA.begin(), ::tolower);
            std::transform(nameB.begin(), nameB.end(), nameB.begin(), ::tolower);
            return nameA < nameB;
         });
      
      for (const auto& table : sortedTables) {
         json tableJson;
         tableJson["uuid"] = table.uuid ? table.uuid : "";
         tableJson["name"] = table.name ? table.name : "";
         tableJson["path"] = table.path ? table.path : "";
         tableJson["image"] = table.image ? table.image : "";
         tableJson["createdAt"] = (long long)table.createdAt;
         tableJson["modifiedAt"] = (long long)table.modifiedAt;
         
         tablesArray.push_back(tableJson);
      }
      
      j["tables"] = tablesArray;
      
      string jsonString = j.dump(2);
      if (!ValidateJson(jsonString)) {
         PLOGE.printf("TableManager::Save: Generated JSON failed validation");
         return VPINBALL_STATUS_FAILURE;
      }
      
      bool success = FileSystem::WriteFile(m_tablesJsonPath, jsonString);
      if (success)
         return VPINBALL_STATUS_SUCCESS;
      else {
         PLOGE.printf("TableManager::Save: Failed to write file: %s", m_tablesJsonPath.c_str());
         return VPINBALL_STATUS_FAILURE;
      }
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("TableManager::Save: Exception: %s", e.what());
      return VPINBALL_STATUS_FAILURE;
   }
}


VPINBALL_STATUS TableManager::AddTable(const string& filePath, string* outUuid)
{
   if (FindTableByPath(filePath) != nullptr)
      return VPINBALL_STATUS_SUCCESS;

   Table* newTable = CreateTableFromFile(filePath);
   if (newTable) {
      m_tables.push_back(*newTable);

      const Table& addedTable = m_tables.back();

      if (outUuid && addedTable.uuid) {
         *outUuid = addedTable.uuid;
      }

      delete newTable;

      return VPINBALL_STATUS_SUCCESS;
   }

   return VPINBALL_STATUS_FAILURE;
}


VPINBALL_STATUS TableManager::RenameTable(const string& uuid, const string& newName)
{
   PLOGI.printf("TableManager::RenameTable: Renaming table UUID: %s to: %s", uuid.c_str(), newName.c_str());
   
   Table* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("TableManager::RenameTable: Table not found with UUID: %s", uuid.c_str());
      return VPINBALL_STATUS_FAILURE;
   }
   
   if (table->name)
      delete[] table->name;

   table->name = new char[newName.length() + 1];
   strcpy(table->name, newName.c_str());
   
   table->modifiedAt = std::time(nullptr);

   if (!m_tablesPath.empty()) {
      VPINBALL_STATUS saveStatus = Save();
      if (saveStatus != VPINBALL_STATUS_SUCCESS) {
         PLOGE.printf("TableManager::RenameTable: Failed to save updated registry after rename");
      }
   }
   
   PLOGI.printf("TableManager::RenameTable: Successfully renamed table from UUID %s to: %s", uuid.c_str(), newName.c_str());

   return VPINBALL_STATUS_SUCCESS;
}

Table* TableManager::GetTable(const string& uuid)
{
   return FindTableByUUID(uuid);
}

string TableManager::GetTableImageFullPath(const string& uuid)
{
   Table* table = GetTable(uuid);
   if (!table || !table->image || table->image[0] == '\0')
      return "";

   if (FileSystem::IsSAFUri(m_tablesPath)) {
      // SAF: Construct content:// URI
      return m_tablesPath + "/" + string(table->image);
   } else {
      // Regular filesystem
      return m_tablesPath + "/" + string(table->image);
   }
}

void TableManager::GetAllTables(TablesData& tablesData)
{
   // Deduplicate tables by UUID (defensive guard against corrupted registry)
   vector<Table> uniqueTables;
   std::unordered_set<string> seenUuids;

   for (const auto& table : m_tables) {
      string uuid = table.uuid ? table.uuid : "";
      if (!uuid.empty() && seenUuids.find(uuid) == seenUuids.end()) {
         uniqueTables.push_back(table);
         seenUuids.insert(uuid);
      } else if (!uuid.empty()) {
         PLOGW.printf("TableManager::GetAllTables: Skipping duplicate UUID: %s", uuid.c_str());
      }
   }

   tablesData.tableCount = (int)uniqueTables.size();
   tablesData.success = true;

   if (tablesData.tableCount > 0) {
      tablesData.tables = new Table[tablesData.tableCount];
      for (int i = 0; i < tablesData.tableCount; i++) {
         const Table& src = uniqueTables[i];
         Table& dst = tablesData.tables[i];

         dst.uuid = new char[strlen(src.uuid) + 1];
         strcpy(dst.uuid, src.uuid);
         dst.name = new char[strlen(src.name) + 1];
         strcpy(dst.name, src.name);
         dst.fileName = new char[strlen(src.fileName) + 1];
         strcpy(dst.fileName, src.fileName);
         dst.fullPath = new char[strlen(src.fullPath) + 1];
         strcpy(dst.fullPath, src.fullPath);
         dst.path = new char[strlen(src.path) + 1];
         strcpy(dst.path, src.path);

         if (src.image) {
            dst.image = new char[strlen(src.image) + 1];
            strcpy(dst.image, src.image);
         } else {
            dst.image = new char[1];
            dst.image[0] = '\0';
         }

         dst.createdAt = src.createdAt;
         dst.modifiedAt = src.modifiedAt;
      }
   }
   else
      tablesData.tables = nullptr;
}

size_t TableManager::GetTableCount()
{
   return m_tables.size();
}

void TableManager::ClearAll()
{
   for (auto& table : m_tables)
      CleanupTable(table);

   m_tables.clear();
}

Table* TableManager::FindTableByPath(const string& fullPath)
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(), 
                         [&fullPath](const Table& table) {
                            return table.fullPath && string(table.fullPath) == fullPath;
                         });
   return (it != m_tables.end()) ? &(*it) : nullptr;
}

Table* TableManager::FindTableByUUID(const string& uuid)
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(),
                         [&uuid](const Table& table) {
                            return table.uuid && string(table.uuid) == uuid;
                         });
   return (it != m_tables.end()) ? &(*it) : nullptr;
}

void TableManager::RemoveDuplicateUUIDs()
{
   if (m_tables.empty())
      return;

   // PLOGI.printf("TableManager::RemoveDuplicateUUIDs: Checking for duplicate UUIDs in %zu tables", m_tables.size());

   std::unordered_set<string> seenUuids;
   vector<Table> uniqueTables;

   for (auto& table : m_tables) {
      string uuid = table.uuid ? table.uuid : "";
      if (!uuid.empty() && seenUuids.find(uuid) == seenUuids.end()) {
         uniqueTables.push_back(table);
         seenUuids.insert(uuid);
      } else if (!uuid.empty()) {
         PLOGW.printf("TableManager::RemoveDuplicateUUIDs: Removing duplicate UUID: %s (name: %s)",
                      uuid.c_str(), table.name ? table.name : "unknown");
         CleanupTable(table);
      } else {
         PLOGW.printf("TableManager::RemoveDuplicateUUIDs: Removing table with empty UUID (name: %s)",
                      table.name ? table.name : "unknown");
         CleanupTable(table);
      }
   }

   size_t originalCount = m_tables.size();
   size_t uniqueCount = uniqueTables.size();

   if (originalCount != uniqueCount) {
      PLOGW.printf("TableManager::RemoveDuplicateUUIDs: Removed %zu duplicate entries",
                   originalCount - uniqueCount);
      m_tables = std::move(uniqueTables);

      Save();
   }
   // else: No duplicates found (silent)
}

Table* TableManager::CreateTableFromFile(const string& filePath)
{
   Table* table = new Table();
   memset(table, 0, sizeof(Table));

   std::filesystem::path path(filePath);

   // Generate UUID and ensure it's unique (defensive guard against duplicate UUIDs)
   string uuid = GenerateUUID();
   int retries = 0;
   const int MAX_RETRIES = 100;
   while (FindTableByUUID(uuid) != nullptr && retries < MAX_RETRIES) {
      PLOGW.printf("TableManager::CreateTableFromFile: UUID collision detected: %s, regenerating (retry %d/%d)",
                   uuid.c_str(), retries + 1, MAX_RETRIES);
      uuid = GenerateUUID();
      retries++;
   }

   if (retries >= MAX_RETRIES) {
      PLOGE.printf("TableManager::CreateTableFromFile: Failed to generate unique UUID after %d retries", MAX_RETRIES);
      delete table;
      return nullptr;
   }

   table->uuid = new char[uuid.length() + 1];
   strcpy(table->uuid, uuid.c_str());
   
   table->fileName = new char[path.filename().string().length() + 1];
   strcpy(table->fileName, path.filename().c_str());
   
   table->fullPath = new char[filePath.length() + 1];
   strcpy(table->fullPath, filePath.c_str());
   
   std::filesystem::path relativePath = std::filesystem::relative(path, m_tablesPath);
   string relPath = relativePath.string();
   table->path = new char[relPath.length() + 1];
   strcpy(table->path, relPath.c_str());
   
   string name = path.stem().string();
   std::replace(name.begin(), name.end(), '_', ' ');
   table->name = new char[name.length() + 1];
   strcpy(table->name, name.c_str());
   
   try {
      auto ftime = std::filesystem::last_write_time(path);
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
         ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
      table->modifiedAt = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
      table->createdAt = table->modifiedAt;
   } catch (...) {
      table->modifiedAt = std::chrono::duration_cast<std::chrono::seconds>(
         std::chrono::system_clock::now().time_since_epoch()).count();
      table->createdAt = table->modifiedAt;
   }
   
   std::filesystem::path basePath = path.parent_path() / path.stem();
   string imageFile;
   if (std::filesystem::exists(basePath.string() + ".png")) {
      std::filesystem::path imagePath = std::filesystem::relative(basePath.string() + ".png", m_tablesPath);
      imageFile = imagePath.string();
   } else if (std::filesystem::exists(basePath.string() + ".jpg")) {
      std::filesystem::path imagePath = std::filesystem::relative(basePath.string() + ".jpg", m_tablesPath);
      imageFile = imagePath.string();
   }
   
   table->image = new char[imageFile.length() + 1];
   strcpy(table->image, imageFile.c_str());

   return table;
}

void TableManager::CleanupTable(Table& table)
{
   delete[] table.uuid;
   delete[] table.name;
   delete[] table.fileName;
   delete[] table.fullPath;
   delete[] table.path;
   delete[] table.image;
}

string TableManager::GenerateUUID() const
{
   static thread_local std::random_device rd;
   static thread_local std::mt19937 generator(rd());
   static thread_local uuids::uuid_random_generator uuidGen(generator);
   return uuids::to_string(uuidGen());
}



VPINBALL_STATUS TableManager::RefreshTables()
{

   VPINBALL_STATUS status = Load();
   if (status != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::RefreshTables: Failed to load from JSON");
      return status;
   }
   
   status = Reconcile();
   if (status != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::RefreshTables: Failed to reconcile with filesystem");
      return status;
   }

   return VPINBALL_STATUS_SUCCESS;
}

bool TableManager::ValidateJson(const string& jsonString) const
{
   try {
      json validationJson = json::parse(jsonString);
      
      if (!validationJson.contains("tables") || !validationJson["tables"].is_array()) {
         PLOGE.printf("TableManager::ValidateJson: Missing tables array");
         return false;
      }
      
      if (validationJson["tableCount"] != (int)m_tables.size()) {
         PLOGE.printf("TableManager::ValidateJson: Table count mismatch");
         return false;
      }
      
      return true;
   } catch (const json::parse_error& e) {
      PLOGE.printf("TableManager::ValidateJson: Parse error: %s", e.what());
      return false;
   }
}

VPINBALL_STATUS TableManager::ScanDirectory(const string& path)
{
   if (!FileSystem::Exists(path)) {
      PLOGW.printf("TableManager::ScanDirectory: Directory does not exist: %s", path.c_str());
      return VPINBALL_STATUS_SUCCESS;
   }

   vector<string> vpxFiles = FileSystem::ListFilesRecursive(path, ".vpx");

   for (const string& filePath : vpxFiles)
      AddTable(filePath);

   return VPINBALL_STATUS_SUCCESS;
}



VPINBALL_STATUS TableManager::ImportTable(const string& sourceFile)
{
   if (!std::filesystem::exists(sourceFile)) {
      PLOGE.printf("TableManager::ImportTable: Source file does not exist: %s", sourceFile.c_str());
      return VPINBALL_STATUS_FAILURE;
   }
   
   std::filesystem::path sourcePath(sourceFile);
   string ext = sourcePath.extension().string();
   std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
   
   PLOGI.printf("TableManager::ImportTable: Importing %s (extension: %s)", sourceFile.c_str(), ext.c_str());
   
   string name = sourcePath.stem().string();
   std::replace(name.begin(), name.end(), '_', ' ');
   
   string tablesPath = m_tablesPath;
   
   try {
      if (ext == ".vpxz") {
         PLOGI.printf("TableManager::ImportTable: Extracting VPXZ archive: %s", sourceFile.c_str());
         
         string tempDir = std::filesystem::temp_directory_path().string() + "/vpinball_import_" + std::to_string(std::time(nullptr));
         std::filesystem::create_directories(tempDir);
         
         VPINBALL_STATUS extractStatus = ExtractZipToDirectory(sourceFile, tempDir);
         if (extractStatus != VPINBALL_STATUS_SUCCESS) {
            std::filesystem::remove_all(tempDir);
            return extractStatus;
         }

         vector<string> vpxFiles;
         for (const auto& entry : std::filesystem::recursive_directory_iterator(tempDir)) {
            if (entry.is_regular_file()) {
               string fileExt = entry.path().extension().string();
               std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);
               
               if (fileExt == ".vpx") {
                  vpxFiles.push_back(entry.path().string());
               }
            }
         }
         
         if (vpxFiles.empty()) {
            PLOGE.printf("TableManager::ImportTable: No VPX files found in archive");
            std::filesystem::remove_all(tempDir);
            return VPINBALL_STATUS_FAILURE;
         }
         
         VPINBALL_STATUS result = VPINBALL_STATUS_SUCCESS;
         string lastUuid;
         for (const string& vpxFile : vpxFiles) {
            std::filesystem::path vpxPath(vpxFile);
            string vpxName = vpxPath.stem().string();

            string folderName = SanitizeTableName(vpxName);
            folderName = GetUniqueTableFolder(folderName, tablesPath);

            string destFolder = FileSystem::JoinPath(tablesPath, folderName);
            FileSystem::CreateDirectories(destFolder);

            std::filesystem::path sourceDir = vpxPath.parent_path();

            bool copySuccess = FileSystem::CopyDirectory(sourceDir.string(), destFolder);
            if (!copySuccess) {
               PLOGE.printf("TableManager::ImportTable: Failed to copy directory structure from %s to %s",
                  sourceDir.string().c_str(), destFolder.c_str());
               result = VPINBALL_STATUS_FAILURE;
               continue;
            }

            PLOGI.printf("TableManager::ImportTable: Copied entire directory structure from %s to %s",
               sourceDir.string().c_str(), destFolder.c_str());

            string destFile = FileSystem::JoinPath(destFolder, vpxPath.filename().string());

            string uuid;
            VPINBALL_STATUS addStatus = AddTable(destFile, &uuid);
            if (addStatus != VPINBALL_STATUS_SUCCESS)
               result = VPINBALL_STATUS_FAILURE;
            else {
               lastUuid = uuid;
               PLOGI.printf("TableManager::ImportTable: Successfully imported VPX from archive: %s", vpxName.c_str());
            }
         }
         std::filesystem::remove_all(tempDir);

         return result;
      }
      else if (ext == ".vpx") {
         PLOGI.printf("TableManager::ImportTable: Importing single VPX file: %s", sourceFile.c_str());

         string folderName = SanitizeTableName(name);
         folderName = GetUniqueTableFolder(folderName, tablesPath);

         string destFolder = FileSystem::JoinPath(tablesPath, folderName);

         FileSystem::CreateDirectories(destFolder);

         string destFile = FileSystem::JoinPath(destFolder, sourcePath.filename().string());

         bool copySuccess = FileSystem::CopyFile(sourcePath.string(), destFile);
         if (!copySuccess) {
            PLOGE.printf("TableManager::ImportTable: Failed to copy file: %s", sourceFile.c_str());
            return VPINBALL_STATUS_FAILURE;
         }

         string uuid;
         VPINBALL_STATUS addStatus = AddTable(destFile, &uuid);
         if (addStatus == VPINBALL_STATUS_SUCCESS) {
            PLOGI.printf("TableManager::ImportTable: Successfully imported table: %s", name.c_str());
            // Event will be sent by caller (VPinballLib level)
         }

         return addStatus;
      }
      
      PLOGE.printf("TableManager::ImportTable: Unsupported file extension: %s", ext.c_str());
      return VPINBALL_STATUS_FAILURE;
      
   } catch (const std::exception& e) {
      PLOGE.printf("TableManager::ImportTable: Exception: %s", e.what());
      return VPINBALL_STATUS_FAILURE;
   }
}


VPINBALL_STATUS TableManager::ExtractZipToDirectory(const string& zipFile, const string& destDir)
{
   PLOGI.printf("ExtractZipToDirectory: extracting %s to %s", zipFile.c_str(), destDir.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(zipFile.c_str(), ZIP_RDONLY, &error);
   if (!zip_archive) {
      PLOGE.printf("ExtractZipToDirectory: Failed to open zip file: %s (error: %d)", zipFile.c_str(), error);
      return VPINBALL_STATUS_FAILURE;
   }

   zip_int64_t file_count = zip_get_num_entries(zip_archive, 0);
   PLOGI.printf("ExtractZipToDirectory: Found %lld files in archive", file_count);
   
   for (zip_uint64_t i = 0; i < (zip_uint64_t)file_count; ++i) {
      zip_stat_t st;
      if (zip_stat_index(zip_archive, i, ZIP_STAT_NAME, &st) != 0)
         continue;

      string filename = st.name;
      if (filename.starts_with("__MACOSX") || filename.starts_with(".DS_Store"))
         continue;

      std::filesystem::path out = std::filesystem::path(destDir) / filename;
      
      if (filename.back() == '/') {
         std::filesystem::create_directories(out);
      } else {
         std::filesystem::create_directories(out.parent_path());
         zip_file_t* zip_file = zip_fopen_index(zip_archive, i, 0);
         if (!zip_file) {
            PLOGE.printf("ExtractZipToDirectory: Failed to open file in archive: %s", filename.c_str());
            zip_close(zip_archive);
            return VPINBALL_STATUS_FAILURE;
         }
         std::ofstream ofs(out, std::ios::binary);
         char buf[4096];
         zip_int64_t len;
         while ((len = zip_fread(zip_file, buf, sizeof(buf))) > 0)
            ofs.write(buf, len);
         zip_fclose(zip_file);
         
         PLOGI.printf("ExtractZipToDirectory: Extracted file: %s", out.string().c_str());
      }

      json progressJson;
      progressJson["progress"] = int((i * 100) / file_count);
      string progressStr = progressJson.dump();
   }

   zip_close(zip_archive);
   PLOGI.printf("ExtractZipToDirectory: Extraction completed successfully");
   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS TableManager::DeleteTable(const string& uuid)
{
   PLOGI.printf("TableManager::DeleteTable: Deleting table with UUID: %s", uuid.c_str());
   
   // Find the table by UUID in our internal registry
   Table* table = nullptr;
   for (auto& t : m_tables) {
      if (string(t.uuid) == uuid) {
         table = &t;
         break;
      }
   }
   
   if (!table || !table->path) {
      PLOGE.printf("TableManager::DeleteTable: Table with UUID %s not found", uuid.c_str());
      return VPINBALL_STATUS_FAILURE;
   }
   
   // Get the full path to the VPX file using FileSystem abstraction
   string tablePath = FileSystem::JoinPath(m_tablesPath, table->path);

   // Extract table directory from path
   std::filesystem::path pathObj(tablePath);
   string tableDir = pathObj.parent_path().string();

   PLOGI.printf("TableManager::DeleteTable: Table path: %s", tablePath.c_str());
   PLOGI.printf("TableManager::DeleteTable: Table directory: %s", tableDir.c_str());
   PLOGI.printf("TableManager::DeleteTable: Main tables directory: %s", m_tablesPath.c_str());

   // Safety check: Never delete the main tables directory
   if (tableDir == m_tablesPath || tableDir + "/" == m_tablesPath || tableDir + "\\" == m_tablesPath) {
      PLOGE.printf("TableManager::DeleteTable: Refusing to delete from main tables directory");
      return VPINBALL_STATUS_FAILURE;
   }

   // Check if the table directory exists using FileSystem abstraction
   if (!FileSystem::Exists(tableDir)) {
      PLOGI.printf("TableManager::DeleteTable: Table directory does not exist, only removing from registry");
   } else {
      int vpxCount = 0;
      vector<string> vpxFiles;

      // List directory contents using FileSystem abstraction
      vector<string> entries = FileSystem::ListDirectory(tableDir);

      for (const auto& entry : entries) {
         // Extract extension from entry path
         std::filesystem::path entryPath(entry);
         string fileExt = entryPath.extension().string();
         std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);

         if (fileExt == ".vpx") {
            vpxCount++;
            vpxFiles.push_back(entry);
         }
      }

      PLOGI.printf("TableManager::DeleteTable: Found %d VPX files in directory", vpxCount);

      if (vpxCount <= 1) {
         PLOGI.printf("TableManager::DeleteTable: Table is alone in subfolder, deleting entire directory");
         if (!FileSystem::Delete(tableDir)) {
            PLOGE.printf("TableManager::DeleteTable: Failed to delete directory: %s", tableDir.c_str());
            return VPINBALL_STATUS_FAILURE;
         }
         PLOGI.printf("TableManager::DeleteTable: Successfully deleted directory: %s", tableDir.c_str());
      } else {
         PLOGI.printf("TableManager::DeleteTable: Multiple VPX files in directory, deleting only VPX file");
         if (!FileSystem::Delete(tablePath)) {
            PLOGE.printf("TableManager::DeleteTable: Failed to delete VPX file: %s", tablePath.c_str());
            return VPINBALL_STATUS_FAILURE;
         }
         PLOGI.printf("TableManager::DeleteTable: Successfully deleted VPX file: %s", tablePath.c_str());
      }
   }
   
   PLOGI.printf("TableManager::DeleteTable: File deletion completed, removing from registry");
   
   for (auto it = m_tables.begin(); it != m_tables.end(); ++it) {
      if (string(it->uuid) == uuid) {
         Table removedTable = *it;
         CleanupTable(*it);
         m_tables.erase(it);
         // Event will be sent by caller (VPinballLib level)
         PLOGI.printf("TableManager::DeleteTable: Successfully removed table from registry");
         return VPINBALL_STATUS_SUCCESS;
      }
   }
   
   PLOGE.printf("TableManager::DeleteTable: Table with UUID %s not found in registry", uuid.c_str());
   return VPINBALL_STATUS_FAILURE;
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
      } else {
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

   // Use FileSystem::JoinPath and FileSystem::Exists to handle both normal paths and SAF
   while (FileSystem::Exists(FileSystem::JoinPath(tablesPath, candidate))) {
      candidate = sanitizedName + "-" + std::to_string(counter);
      counter++;
   }

   return candidate;
}

VPINBALL_STATUS TableManager::SetTableImage(const string& uuid, const string& imagePath)
{
   PLOGI.printf("TableManager::SetTableImage: Setting image for UUID: %s to: %s", uuid.c_str(), imagePath.c_str());
   
   Table* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("TableManager::SetTableImage: Table not found with UUID: %s", uuid.c_str());
      return VPINBALL_STATUS_FAILURE;
   }
   
   if (!imagePath.empty()) {
      if (table->image)
         delete[] table->image;
      table->image = new char[imagePath.length() + 1];
      strcpy(table->image, imagePath.c_str());
   } else {
      if (table->image && strlen(table->image) > 0) {
         string currentImagePath = FileSystem::JoinPath(m_tablesPath, table->image);
         if (FileSystem::Exists(currentImagePath)) {
            if (FileSystem::Delete(currentImagePath)) {
               PLOGI.printf("TableManager::SetTableImage: Removed image file: %s", currentImagePath.c_str());
            } else {
               PLOGE.printf("TableManager::SetTableImage: Failed to remove image file: %s", currentImagePath.c_str());
            }
         }
      }

      if (table->image)
         delete[] table->image;
      table->image = new char[1];
      table->image[0] = '\0';
   }

   table->modifiedAt = std::time(nullptr);

   PLOGI.printf("TableManager::SetTableImage: Successfully set image for table: %s",
               table->name ? table->name : "unknown");

   Save();

   return VPINBALL_STATUS_SUCCESS;
}

const string& TableManager::GetTablesPath()
{
   return m_tablesPath;
}

VPINBALL_STATUS TableManager::ReloadTablesPath()
{
   PLOGI.printf("TableManager::ReloadTablesPath: Reloading tables path");
   return Start();
}

VPINBALL_STATUS TableManager::CompressDirectory(const string& sourcePath, const string& destinationPath)
{
   PLOGI.printf("TableManager::CompressDirectory: Compressing %s to %s", sourcePath.c_str(), destinationPath.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(destinationPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
   if (!zip_archive) {
      PLOGE.printf("TableManager::CompressDirectory: Failed to create archive: %s (error: %d)", destinationPath.c_str(), error);
      return VPINBALL_STATUS_FAILURE;
   }

   std::filesystem::path base(sourcePath);
   size_t base_len = base.string().length();

   vector<std::filesystem::path> items;
   try {
      for (auto& item : std::filesystem::recursive_directory_iterator(base))
         items.push_back(item.path());
   } catch (const std::filesystem::filesystem_error& ex) {
      PLOGE.printf("TableManager::CompressDirectory: Error scanning directory: %s", ex.what());
      zip_close(zip_archive);
      return VPINBALL_STATUS_FAILURE;
   }

   size_t total = items.size();
   size_t done = 0;

   for (auto& item : items) {
      string rel = item.string().substr(base_len + 1);
      if (std::filesystem::is_directory(item)) {
         zip_dir_add(zip_archive, (rel + '/').c_str(), ZIP_FL_ENC_UTF_8);
      } else {
         zip_source_t* zip_source = zip_source_file(zip_archive, item.string().c_str(), 0, 0);
         if (!zip_source) {
            PLOGE.printf("TableManager::CompressDirectory: Failed to create source for file: %s", item.string().c_str());
            zip_close(zip_archive);
            return VPINBALL_STATUS_FAILURE;
         }
         if (zip_file_add(zip_archive, rel.c_str(), zip_source, ZIP_FL_ENC_UTF_8) < 0) {
            PLOGE.printf("TableManager::CompressDirectory: Failed to add file to archive: %s", rel.c_str());
            zip_source_free(zip_source);
            zip_close(zip_archive);
            return VPINBALL_STATUS_FAILURE;
         }
      }

      ++done;
      int progress = int((done * 100) / total);
      if (done % 10 == 0) {
         PLOGI.printf("TableManager::CompressDirectory: Progress: %d%% (%zu/%zu files)", progress, done, total);
      }
   }

   zip_close(zip_archive);
   PLOGI.printf("TableManager::CompressDirectory: Successfully compressed %zu files", total);
   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS TableManager::UncompressArchive(const string& archivePath)
{
   PLOGI.printf("TableManager::UncompressArchive: Extracting %s", archivePath.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(archivePath.c_str(), ZIP_RDONLY, &error);
   if (!zip_archive) {
      PLOGE.printf("TableManager::UncompressArchive: Failed to open archive: %s (error: %d)", archivePath.c_str(), error);
      return VPINBALL_STATUS_FAILURE;
   }

   zip_int64_t file_count = zip_get_num_entries(zip_archive, 0);
   PLOGI.printf("TableManager::UncompressArchive: Found %lld files in archive", file_count);
   
   for (zip_uint64_t i = 0; i < (zip_uint64_t)file_count; ++i) {
      zip_stat_t st;
      if (zip_stat_index(zip_archive, i, ZIP_STAT_NAME, &st) != 0)
         continue;

      string filename = st.name;
      if (filename.starts_with("__MACOSX") || filename.starts_with(".DS_Store"))
         continue;

      std::filesystem::path out = std::filesystem::path(archivePath).parent_path() / filename;
      
      if (filename.back() == '/') {
         std::filesystem::create_directories(out);
      } else {
         std::filesystem::create_directories(out.parent_path());
         zip_file_t* zip_file = zip_fopen_index(zip_archive, i, 0);
         if (!zip_file) {
            PLOGE.printf("TableManager::UncompressArchive: Failed to open file in archive: %s", filename.c_str());
            zip_close(zip_archive);
            return VPINBALL_STATUS_FAILURE;
         }
         std::ofstream ofs(out, std::ios::binary);
         char buf[4096];
         zip_int64_t len;
         while ((len = zip_fread(zip_file, buf, sizeof(buf))) > 0)
            ofs.write(buf, len);
         zip_fclose(zip_file);
         
         PLOGI.printf("TableManager::UncompressArchive: Extracted file: %s", out.string().c_str());
      }

      int progress = int((i * 100) / file_count);
      if (i % 10 == 0) {
         PLOGI.printf("TableManager::UncompressArchive: Progress: %d%% (%llu/%lld files)", progress, i, file_count);
      }
   }

   zip_close(zip_archive);
   PLOGI.printf("TableManager::UncompressArchive: Successfully extracted archive");
   return VPINBALL_STATUS_SUCCESS;
}

string TableManager::ExportTable(const string& uuid)
{
   PLOGI.printf("TableManager::ExportTable: Exporting table with UUID: %s", uuid.c_str());

   Table* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("TableManager::ExportTable: Table not found with UUID: %s", uuid.c_str());
      return "";
   }

   string sanitizedName = table->name;
   std::replace_if(sanitizedName.begin(), sanitizedName.end(),
                   [](char c) { return c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|'; },
                   '_');

   std::filesystem::path tempDir = std::filesystem::temp_directory_path();
   std::filesystem::path tempFile = tempDir / (sanitizedName + ".vpxz");

   try {
      if (std::filesystem::exists(tempFile))
         std::filesystem::remove(tempFile);
   }
   catch (const std::filesystem::filesystem_error& ex) {
      PLOGE.printf("TableManager::ExportTable: Failed to remove existing temp file: %s", ex.what());
   }

   string tableDirToCompress;

   // Handle content:// URIs (SAF tables)
   if (FileSystem::IsSAFUri(table->fullPath)) {
      PLOGI.printf("TableManager::ExportTable: SAF table detected, copying to temp directory first");

      // Extract relative path to get the table directory
      string relativePath = FileSystem::ExtractRelativePath(table->fullPath);
      std::filesystem::path relativePathObj(relativePath);
      string tableDir = relativePathObj.parent_path().string();

      // Create temp directory for SAF copy
      std::filesystem::path safTempDir = tempDir / "saf_export";
      std::filesystem::path safTempTableDir = safTempDir / tableDir;

      PLOGI.printf("TableManager::ExportTable: Copying SAF directory to: %s", safTempTableDir.string().c_str());

      // Clean up any existing temp export directory
      try {
         if (std::filesystem::exists(safTempTableDir))
            std::filesystem::remove_all(safTempTableDir);
         std::filesystem::create_directories(safTempTableDir);
      } catch (const std::filesystem::filesystem_error& ex) {
         PLOGE.printf("TableManager::ExportTable: Failed to create temp export directory: %s", ex.what());
         return "";
      }

      // Build the content:// URI for the table directory
      string safTableDir = m_tablesPath + "/" + tableDir;

      // Copy SAF directory to temp
      if (!FileSystem::CopyDirectory(safTableDir, safTempTableDir.string())) {
         PLOGE.printf("TableManager::ExportTable: Failed to copy SAF directory to temp");
         return "";
      }

      tableDirToCompress = safTempTableDir.string();
      PLOGI.printf("TableManager::ExportTable: SAF directory copied successfully");
   } else {
      // Normal filesystem table
      std::filesystem::path tablePath(table->fullPath);
      tableDirToCompress = tablePath.parent_path().string();
   }

   VPINBALL_STATUS compressResult = CompressDirectory(tableDirToCompress, tempFile.string());
   if (compressResult != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::ExportTable: Failed to compress table directory");
      return "";
   }

   PLOGI.printf("TableManager::ExportTable: Successfully exported table to: %s", tempFile.string().c_str());
   return tempFile.string();
}

const char* TableManager::GetTablesJsonCopy()
{
   thread_local string jsonStr;

   json j;
   j["success"] = true;
   j["tableCount"] = static_cast<int>(m_tables.size());

   json tablesArray = json::array();
   for (const auto& table : m_tables) {
      json tableJson;
      tableJson["uuid"] = table.uuid ? table.uuid : "";
      tableJson["name"] = table.name ? table.name : "";
      tableJson["path"] = table.path ? table.path : "";
      tableJson["image"] = table.image ? table.image : "";
      tableJson["createdAt"] = static_cast<long long>(table.createdAt);
      tableJson["modifiedAt"] = static_cast<long long>(table.modifiedAt);

      tablesArray.push_back(tableJson);
   }

   j["tables"] = tablesArray;
   jsonStr = j.dump();

   return jsonStr.c_str();
}

}