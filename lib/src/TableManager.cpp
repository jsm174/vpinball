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
#include <nlohmann/json.hpp>
#include <stduuid/uuid.h>
#include <zip.h>

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

using json = nlohmann::json;

namespace VPinballLib {

TableManager::TableManager()
   : m_systemEventCallback(nullptr)
{
   // Read TablesPath from INI (defaults to empty string)
   string tablesPathIni = g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "TablesPath"s, ""s);

   if (tablesPathIni.empty()) {
      // Default: use internal app data directory
      m_tablesPath = string(g_pvp->m_myPrefPath) + "tables" + PATH_SEPARATOR_CHAR;
      PLOGI.printf("TableManager: Using default tables path: %s", m_tablesPath.c_str());
   } else {
      // Custom path from INI (could be normal path or content:// URI for SAF)
      m_tablesPath = tablesPathIni;
      if (FileSystem::IsSpecialUri(m_tablesPath)) {
         PLOGI.printf("TableManager: Using SAF external storage: %s", m_tablesPath.c_str());
      } else {
         PLOGI.printf("TableManager: Using custom tables path: %s", m_tablesPath.c_str());
      }
   }

   // Update FileSystem's cached TablesPath for content:// URI handling
   FileSystem::SetTablesPath(m_tablesPath);

   // Create tables directory if it doesn't exist (skip for content:// URIs - they're always "valid")
   if (!FileSystem::IsSpecialUri(m_tablesPath)) {
      if (!FileSystem::Exists(m_tablesPath)) {
         if (!FileSystem::CreateDirectories(m_tablesPath)) {
            PLOGE.printf("TableManager: Failed to create tables directory: %s", m_tablesPath.c_str());
         } else {
            PLOGI.printf("TableManager: Created tables directory: %s", m_tablesPath.c_str());
         }
      }

      // Load existing tables.json if it exists (preserves metadata)
      LoadFromJson();

      // Reconcile with filesystem (scan for new files, remove missing ones)
      ReconcileWithFilesystem();
   } else {
      // For content:// URIs, defer loading until after construction to avoid circular dependency
      PLOGI.printf("TableManager: Deferring table loading for content:// URI");
   }
}

TableManager::~TableManager()
{
   ClearAll();
}

void TableManager::CompleteDeferredInitialization()
{
   if (FileSystem::IsSpecialUri(m_tablesPath)) {
      PLOGI.printf("TableManager::CompleteDeferredInitialization: Completing deferred initialization for content:// URI");

      // Load existing tables.json if it exists (preserves metadata)
      LoadFromJson();

      // Reconcile with filesystem (scan for new files, remove missing ones)
      ReconcileWithFilesystem();

      PLOGI.printf("TableManager::CompleteDeferredInitialization: Completed");
   }
}

VPINBALL_STATUS TableManager::LoadFromJson()
{
   string path = GetTablesJsonPath();

   // Check if tables.json exists using FileSystem abstraction (handles both normal paths and saf://)
   if (!FileSystem::Exists(path)) {
      PLOGW.printf("TableManager::LoadFromJson: tables.json does not exist: %s", path.c_str());
      PLOGI.printf("TableManager::LoadFromJson: Starting with empty table list, will create after scan");

      // Don't create it yet - let ReconcileWithFilesystem scan first, then SaveToJson will create it
      return VPINBALL_STATUS_SUCCESS;
   }

   // Read existing tables.json to preserve metadata (names, artwork, etc.)
   PLOGI.printf("TableManager::LoadFromJson: Reading existing tables.json from: %s", path.c_str());

   try {
      string rawContent = FileSystem::ReadFile(path);

      if (rawContent.empty()) {
         PLOGW.printf("TableManager::LoadFromJson: tables.json is empty, starting fresh");
         return VPINBALL_STATUS_SUCCESS;
      }
      
      json jsonData;

      try {
         jsonData = json::parse(rawContent);
      } 
      
      catch (const json::parse_error& e) {
         PLOGE.printf("Unable to parse tables.json: error=%s", e.what());
         return VPINBALL_STATUS_FAILURE;
      }
      
      if (!jsonData.contains("tables") || !jsonData["tables"].is_array()) {
         PLOGE.printf("Unable to parse tables.json: missing tables array");
         return VPINBALL_STATUS_FAILURE;
      }
      
      for (auto& table : m_tables) {
         CleanupTable(table);
      }
      m_tables.clear();
      
      for (const auto& tableJson : jsonData["tables"]) {
         VPXTable table;
         memset(&table, 0, sizeof(VPXTable));
         
         string uuid = tableJson.value("uuid", "");
         string name = tableJson.value("name", "");
         string fullPath = tableJson.value("fullPath", "");
         string path = tableJson.value("path", "");
         string artwork = tableJson.value("artwork", "");
         
         // Derive fileName from path
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
         table.artwork = new char[artwork.length() + 1];
         strcpy(table.artwork, artwork.c_str());
         
         table.createdAt = tableJson.value("createdAt", 0LL);
         table.modifiedAt = tableJson.value("modifiedAt", 0LL);
         
         m_tables.push_back(table);
      }

      PLOGI.printf("TableManager::LoadFromJson: Successfully loaded %zu tables", m_tables.size());
      return VPINBALL_STATUS_SUCCESS;
      
   } catch (const std::exception& e) {
      PLOGI.printf("TableManager::LoadFromJson: Exception: %s", e.what());
      return VPINBALL_STATUS_FAILURE;
   }
}


VPINBALL_STATUS TableManager::AddTable(const std::string& filePath, std::string* outUuid)
{
   if (FindTableByPath(filePath) != nullptr)
      return VPINBALL_STATUS_SUCCESS;

   VPXTable* newTable = CreateTableFromFile(filePath);
   if (newTable) {
      m_tables.push_back(*newTable);

      const VPXTable& addedTable = m_tables.back();

      if (outUuid && addedTable.uuid) {
         *outUuid = addedTable.uuid;
      }

      delete newTable;

      SendSystemEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, nullptr);
      return VPINBALL_STATUS_SUCCESS;
   }

   return VPINBALL_STATUS_FAILURE;
}


VPINBALL_STATUS TableManager::RenameTable(const std::string& uuid, const std::string& newName)
{
   PLOGI.printf("TableManager::RenameTable: Renaming table UUID: %s to: %s", uuid.c_str(), newName.c_str());
   
   VPXTable* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("TableManager::RenameTable: Table not found with UUID: %s", uuid.c_str());
      return VPINBALL_STATUS_FAILURE;
   }
   
   if (table->name)
      delete[] table->name;

   table->name = new char[newName.length() + 1];
   strcpy(table->name, newName.c_str());
   
   table->modifiedAt = std::time(nullptr);
   
   // Save updated tables to JSON
   if (!m_tablesPath.empty()) {
      string jsonPath = GetTablesJsonPath();
      VPINBALL_STATUS saveStatus = SaveToJson(jsonPath);
      if (saveStatus != VPINBALL_STATUS_SUCCESS) {
         PLOGE.printf("TableManager::RenameTable: Failed to save updated registry after rename");
      }
   }
   
   PLOGI.printf("TableManager::RenameTable: Successfully renamed table from UUID %s to: %s", uuid.c_str(), newName.c_str());

   thread_local string focusJson;
   focusJson = "{\"focusUuid\":\"" + uuid + "\"}";
   SendSystemEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, (void*)focusJson.c_str());

   return VPINBALL_STATUS_SUCCESS;
}

VPXTable* TableManager::GetTable(const std::string& uuid)
{
   return FindTableByUUID(uuid);
}

void TableManager::GetAllTables(VPXTablesData& tablesData)
{
   tablesData.tableCount = (int)m_tables.size();
   tablesData.success = true;
   
   if (tablesData.tableCount > 0) {
      tablesData.tables = new VPXTable[tablesData.tableCount];
      for (int i = 0; i < tablesData.tableCount; i++) {
         const VPXTable& src = m_tables[i];
         VPXTable& dst = tablesData.tables[i];
         
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
         
         if (src.artwork) {
            dst.artwork = new char[strlen(src.artwork) + 1];
            strcpy(dst.artwork, src.artwork);
         } else {
            dst.artwork = new char[1];
            dst.artwork[0] = '\0';
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

void TableManager::SetSystemEventCallback(std::function<void*(VPINBALL_EVENT, void*)> callback)
{
   m_systemEventCallback = callback;
}

void TableManager::SendSystemEvent(VPINBALL_EVENT event, void* data)
{
   if (m_systemEventCallback) {
      m_systemEventCallback(event, data);
   }
}

VPXTable* TableManager::FindTableByPath(const std::string& fullPath)
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(), 
                         [&fullPath](const VPXTable& table) {
                            return table.fullPath && std::string(table.fullPath) == fullPath;
                         });
   return (it != m_tables.end()) ? &(*it) : nullptr;
}

VPXTable* TableManager::FindTableByUUID(const std::string& uuid)
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(), 
                         [&uuid](const VPXTable& table) {
                            return table.uuid && std::string(table.uuid) == uuid;
                         });
   return (it != m_tables.end()) ? &(*it) : nullptr;
}

VPXTable* TableManager::CreateTableFromFile(const std::string& filePath)
{
   VPXTable* table = new VPXTable();
   memset(table, 0, sizeof(VPXTable));
   
   std::filesystem::path path(filePath);
   
   string uuid = GenerateUUID();
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
   string artworkFile;
   if (std::filesystem::exists(basePath.string() + ".png")) {
      std::filesystem::path artworkPath = std::filesystem::relative(basePath.string() + ".png", m_tablesPath);
      artworkFile = artworkPath.string();
   } else if (std::filesystem::exists(basePath.string() + ".jpg")) {
      std::filesystem::path artworkPath = std::filesystem::relative(basePath.string() + ".jpg", m_tablesPath);
      artworkFile = artworkPath.string();
   }
   
   table->artwork = new char[artworkFile.length() + 1];
   strcpy(table->artwork, artworkFile.c_str());
   
   return table;
}

void TableManager::CleanupTable(VPXTable& table)
{
   delete[] table.uuid;
   delete[] table.name;
   delete[] table.fileName;
   delete[] table.fullPath;
   delete[] table.path;
   delete[] table.artwork;
}

std::string TableManager::GenerateUUID() const
{
   static thread_local std::random_device rd;
   static thread_local std::mt19937 generator(rd());
   static thread_local uuids::uuid_random_generator uuidGen(generator);
   return uuids::to_string(uuidGen());
}

VPINBALL_STATUS TableManager::SaveToJson(const std::string& jsonPath)
{
   PLOGI.printf("TableManager::SaveToJson: Saving %zu tables to: %s", m_tables.size(), jsonPath.c_str());
   
   try {
      json j;
      j["success"] = true;
      j["tableCount"] = (int)m_tables.size();
      
      json tablesArray = json::array();
      
      std::vector<VPXTable> sortedTables = m_tables;
      std::sort(sortedTables.begin(), sortedTables.end(), 
         [](const VPXTable& a, const VPXTable& b) {
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
         tableJson["artwork"] = table.artwork ? table.artwork : "";
         tableJson["createdAt"] = (long long)table.createdAt;
         tableJson["modifiedAt"] = (long long)table.modifiedAt;
         
         tablesArray.push_back(tableJson);
      }
      
      j["tables"] = tablesArray;
      
      string jsonString = j.dump(2);
      if (!ValidateJson(jsonString)) {
         PLOGE.printf("TableManager::SaveToJson: Generated JSON failed validation");
         return VPINBALL_STATUS_FAILURE;
      }
      
      // Use FileSystem abstraction (handles both normal paths and saf://)
      bool success = FileSystem::WriteFile(jsonPath, jsonString);
      if (success) {
         PLOGI.printf("TableManager::SaveToJson: Successfully saved %zu tables to: %s", m_tables.size(), jsonPath.c_str());
         return VPINBALL_STATUS_SUCCESS;
      } else {
         PLOGE.printf("TableManager::SaveToJson: Failed to write file: %s", jsonPath.c_str());
         return VPINBALL_STATUS_FAILURE;
      }
      
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("TableManager::SaveToJson: Exception: %s", e.what());
      return VPINBALL_STATUS_FAILURE;
   }
}

VPINBALL_STATUS TableManager::RefreshTables()
{
   VPINBALL_STATUS loadStatus = LoadFromJson();
   if (loadStatus != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::RefreshTables: Failed to load from JSON");
      return loadStatus;
   }
   
   VPINBALL_STATUS reconcileStatus = ReconcileWithFilesystem();
   if (reconcileStatus != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::RefreshTables: Failed to reconcile with filesystem");
      return reconcileStatus;
   }

   return VPINBALL_STATUS_SUCCESS;
}

bool TableManager::ValidateJson(const std::string& jsonString) const
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

VPINBALL_STATUS TableManager::ScanDirectory(const std::string& path)
{
   PLOGI.printf("TableManager::ScanDirectory: Scanning directory: %s", path.c_str());

   if (!FileSystem::Exists(path)) {
      PLOGI.printf("TableManager::ScanDirectory: Directory does not exist: %s", path.c_str());
      return VPINBALL_STATUS_SUCCESS;
   }

   // Use FileSystem abstraction (handles both normal paths and saf://)
   std::vector<std::string> vpxFiles = FileSystem::ListFilesRecursive(path, ".vpx");

   PLOGI.printf("TableManager::ScanDirectory: Found %zu VPX files", vpxFiles.size());

   for (const string& filePath : vpxFiles) {
      PLOGI.printf("TableManager::ScanDirectory: Adding table: %s", filePath.c_str());
      AddTable(filePath);
   }

   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS TableManager::ReconcileWithFilesystem()
{
   PLOGI.printf("TableManager::ReconcileWithFilesystem: Reconciling with path: %s", m_tablesPath.c_str());
   
   PLOGI.printf("TableManager::ReconcileWithFilesystem: Scanning for new tables");
   VPINBALL_STATUS scanStatus = ScanDirectory(m_tablesPath);
   if (scanStatus != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::ReconcileWithFilesystem: Directory scan failed");
      return scanStatus;
   }
   
   auto it = m_tables.begin();
   while (it != m_tables.end()) {
      if (it->fullPath && !FileSystem::Exists(it->fullPath)) {
         PLOGI.printf("TableManager::ReconcileWithFilesystem: Removing missing table: %s", it->fullPath);
         CleanupTable(*it);
         it = m_tables.erase(it);
      } else {
         ++it;
      }
   }
   
   string jsonPath = GetTablesJsonPath();
   VPINBALL_STATUS saveStatus = SaveToJson(jsonPath);
   if (saveStatus != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::ReconcileWithFilesystem: Failed to save updated registry");
   }
   
   PLOGI.printf("TableManager::ReconcileWithFilesystem: Reconciliation complete, %zu tables in registry", m_tables.size());
   
   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS TableManager::ImportTable(const std::string& sourceFile)
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

         std::vector<std::string> vpxFiles;
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

         if (result == VPINBALL_STATUS_SUCCESS && !lastUuid.empty()) {
            thread_local string focusJson;
            focusJson = "{\"focusUuid\":\"" + lastUuid + "\"}";
            SendSystemEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, (void*)focusJson.c_str());
         }

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

            thread_local string focusJson;
            focusJson = "{\"focusUuid\":\"" + uuid + "\"}";
            SendSystemEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, (void*)focusJson.c_str());
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


VPINBALL_STATUS TableManager::ExtractZipToDirectory(const std::string& zipFile, const std::string& destDir)
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

      std::string filename = st.name;
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
      std::string progressStr = progressJson.dump();
   }

   zip_close(zip_archive);
   PLOGI.printf("ExtractZipToDirectory: Extraction completed successfully");
   return VPINBALL_STATUS_SUCCESS;
}


std::string TableManager::GetTablesJsonPath()
{
   std::string path = m_tablesPath;

   // Special case for content:// URIs - just append with /
   if (FileSystem::IsSpecialUri(path)) {
      return path + "/tables.json";
   }

   if (!path.empty() && path.back() != '/' && path.back() != '\\') {
      path += PATH_SEPARATOR_CHAR;
   }
   return path + "tables.json";
}


VPINBALL_STATUS TableManager::DeleteTable(const std::string& uuid)
{
   PLOGI.printf("TableManager::DeleteTable: Deleting table with UUID: %s", uuid.c_str());
   
   // Find the table by UUID in our internal registry
   VPXTable* table = nullptr;
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
      std::vector<string> vpxFiles;

      // List directory contents using FileSystem abstraction
      std::vector<string> entries = FileSystem::ListDirectory(tableDir);

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
         VPXTable removedTable = *it;
         CleanupTable(*it);
         m_tables.erase(it);
         SendSystemEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, nullptr);
         PLOGI.printf("TableManager::DeleteTable: Successfully removed table from registry");
         return VPINBALL_STATUS_SUCCESS;
      }
   }
   
   PLOGE.printf("TableManager::DeleteTable: Table with UUID %s not found in registry", uuid.c_str());
   return VPINBALL_STATUS_FAILURE;
}

std::string TableManager::SanitizeTableName(const std::string& name)
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

std::string TableManager::GetUniqueTableFolder(const std::string& baseName, const std::string& tablesPath)
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

VPINBALL_STATUS TableManager::SetTableArtwork(const std::string& uuid, const std::string& artworkPath)
{
   PLOGI.printf("TableManager::SetTableArtwork: Setting artwork for UUID: %s to: %s", uuid.c_str(), artworkPath.c_str());
   
   VPXTable* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("TableManager::SetTableArtwork: Table not found with UUID: %s", uuid.c_str());
      return VPINBALL_STATUS_FAILURE;
   }
   
   if (!artworkPath.empty()) {
      if (table->artwork)
         delete[] table->artwork;
      table->artwork = new char[artworkPath.length() + 1];
      strcpy(table->artwork, artworkPath.c_str());
   } else {
      if (table->artwork && strlen(table->artwork) > 0) {
         string currentArtworkPath = m_tablesPath + table->artwork;
         if (std::filesystem::exists(currentArtworkPath)) {
            std::error_code ec;
            std::filesystem::remove(currentArtworkPath, ec);
            if (!ec) {
               PLOGI.printf("TableManager::SetTableArtwork: Removed artwork file: %s", currentArtworkPath.c_str());
            } else {
               PLOGE.printf("TableManager::SetTableArtwork: Failed to remove artwork file: %s - %s", 
                           currentArtworkPath.c_str(), ec.message().c_str());
            }
         }
      }
      
      if (table->artwork)
         delete[] table->artwork;
      table->artwork = new char[1];
      table->artwork[0] = '\0';
   }
   
   table->modifiedAt = std::time(nullptr);
   
   PLOGI.printf("TableManager::SetTableArtwork: Successfully set artwork for table: %s", 
               table->name ? table->name : "unknown");

   SaveToJson(GetTablesJsonPath());

   return VPINBALL_STATUS_SUCCESS;
}

const std::string& TableManager::GetTablesPath()
{
   return m_tablesPath;
}

VPINBALL_STATUS TableManager::ReloadTablesPath()
{
   PLOGI.printf("TableManager::ReloadTablesPath: === STARTING RELOAD ===");

   // Clear current tables
   PLOGI.printf("TableManager::ReloadTablesPath: Clearing current tables (count: %zu)", m_tables.size());
   ClearAll();
   PLOGI.printf("TableManager::ReloadTablesPath: Cleared. New count: %zu", m_tables.size());

   // Read TablesPath from INI (defaults to empty string)
   PLOGI.printf("TableManager::ReloadTablesPath: Reading TablesPath from ini");
   string tablesPathIni = g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "TablesPath"s, ""s);
   PLOGI.printf("TableManager::ReloadTablesPath: TablesPath = '%s'", tablesPathIni.c_str());

   if (tablesPathIni.empty()) {
      // Default: use internal app data directory
      m_tablesPath = string(g_pvp->m_myPrefPath) + "tables" + PATH_SEPARATOR_CHAR;
      PLOGI.printf("TableManager::ReloadTablesPath: Using default tables path: %s", m_tablesPath.c_str());
   } else {
      // Custom path from INI (could be normal path or content:// URI for SAF)
      m_tablesPath = tablesPathIni;
      if (FileSystem::IsSpecialUri(m_tablesPath)) {
         PLOGI.printf("TableManager::ReloadTablesPath: Using SAF external storage: %s", m_tablesPath.c_str());
      } else {
         PLOGI.printf("TableManager::ReloadTablesPath: Using custom tables path: %s", m_tablesPath.c_str());
      }
   }

   // Update FileSystem's cached TablesPath for content:// URI handling
   FileSystem::SetTablesPath(m_tablesPath);

   // Create directory if it doesn't exist (skip for content:// URIs - they're always "valid")
   if (!FileSystem::IsSpecialUri(m_tablesPath)) {
      PLOGI.printf("TableManager::ReloadTablesPath: Checking if directory exists");
      if (!FileSystem::Exists(m_tablesPath)) {
         PLOGI.printf("TableManager::ReloadTablesPath: Directory does not exist, creating...");
         if (!FileSystem::CreateDirectories(m_tablesPath)) {
            PLOGE.printf("TableManager::ReloadTablesPath: FAILED to create directory: %s", m_tablesPath.c_str());
            return VPINBALL_STATUS_FAILURE;
         } else {
            PLOGI.printf("TableManager::ReloadTablesPath: Successfully created directory");
         }
      } else {
         PLOGI.printf("TableManager::ReloadTablesPath: Directory already exists");
      }
   } else {
      PLOGI.printf("TableManager::ReloadTablesPath: Using content:// URI, skipping directory creation");
   }

   // Load existing tables from JSON in new location
   PLOGI.printf("TableManager::ReloadTablesPath: Loading tables.json from new location");
   LoadFromJson();
   PLOGI.printf("TableManager::ReloadTablesPath: Loaded %zu tables from JSON", m_tables.size());

   // Scan for new tables and reconcile with filesystem
   PLOGI.printf("TableManager::ReloadTablesPath: Starting filesystem reconciliation");
   VPINBALL_STATUS status = ReconcileWithFilesystem();
   if (status != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("TableManager::ReloadTablesPath: FAILED to reconcile with filesystem");
      return status;
   }
   PLOGI.printf("TableManager::ReloadTablesPath: Reconciliation complete. Total tables: %zu", m_tables.size());

   // Save tables.json in new location
   string jsonPath = GetTablesJsonPath();
   PLOGI.printf("TableManager::ReloadTablesPath: Saving tables.json to: %s", jsonPath.c_str());
   SaveToJson(jsonPath);

   SendSystemEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, nullptr);

   PLOGI.printf("TableManager::ReloadTablesPath: === RELOAD COMPLETE - SUCCESS ===");
   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS TableManager::CompressDirectory(const std::string& sourcePath, const std::string& destinationPath)
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

VPINBALL_STATUS TableManager::UncompressArchive(const std::string& archivePath)
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

std::string TableManager::ExportTable(const std::string& uuid)
{
   PLOGI.printf("TableManager::ExportTable: Exporting table with UUID: %s", uuid.c_str());

   VPXTable* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("TableManager::ExportTable: Table not found with UUID: %s", uuid.c_str());
      return "";
   }

   std::string sanitizedName = table->name;
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

   std::string tableDirToCompress;

   // Handle content:// URIs (SAF tables)
   if (FileSystem::IsSpecialUri(table->fullPath)) {
      PLOGI.printf("TableManager::ExportTable: SAF table detected, copying to temp directory first");

      // Extract relative path to get the table directory
      std::string relativePath = FileSystem::ExtractRelativePath(table->fullPath);
      std::filesystem::path relativePathObj(relativePath);
      std::string tableDir = relativePathObj.parent_path().string();

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
      std::string safTableDir = m_tablesPath + "/" + tableDir;

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
   thread_local std::string jsonStr;

   json j;
   j["success"] = true;
   j["tableCount"] = static_cast<int>(m_tables.size());

   json tablesArray = json::array();
   for (const auto& table : m_tables) {
      json tableJson;
      tableJson["uuid"] = table.uuid ? table.uuid : "";
      tableJson["name"] = table.name ? table.name : "";
      tableJson["path"] = table.path ? table.path : "";
      tableJson["artwork"] = table.artwork ? table.artwork : "";
      tableJson["createdAt"] = static_cast<long long>(table.createdAt);
      tableJson["modifiedAt"] = static_cast<long long>(table.modifiedAt);

      tablesArray.push_back(tableJson);
   }

   j["tables"] = tablesArray;
   jsonStr = j.dump();

   return jsonStr.c_str();
}

char* TableManager::GetTableJsonCopy(const std::string& uuid)
{
   VPXTable* table = FindTableByUUID(uuid);
   
   json j;
   if (table) {
      j["success"] = true;
      j["uuid"] = table->uuid ? table->uuid : "";
      j["name"] = table->name ? table->name : "";
      j["path"] = table->path ? table->path : "";
      j["artwork"] = table->artwork ? table->artwork : "";
      j["createdAt"] = static_cast<long long>(table->createdAt);
      j["modifiedAt"] = static_cast<long long>(table->modifiedAt);
   } else {
      j["success"] = false;
      j["error"] = "Table not found";
   }
   
   std::string jsonStr = j.dump();
   char* result = new char[jsonStr.length() + 1];
   strcpy(result, jsonStr.c_str());
   return result;
}

}