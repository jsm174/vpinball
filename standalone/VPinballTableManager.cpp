#include "core/stdafx.h"
#include "VPinballTableManager.h"
#include "VPinballLib.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <zip.h>
#include <uuid/uuid.h>

using json = nlohmann::json;

namespace VPinballLib {

VPinballTableManager::VPinballTableManager()
{
   // Check ini file for custom tables path first
   string iniTablesPath = g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "TablesPath"s, ""s);
   
   if (!iniTablesPath.empty()) {
      // Use custom path from ini file, ensure it ends with path separator
      m_tablesPath = iniTablesPath;
      if (m_tablesPath.back() != PATH_SEPARATOR_CHAR) {
         m_tablesPath += PATH_SEPARATOR_CHAR;
      }
   } else {
      // Use default: prefpath + /tables
      m_tablesPath = string(g_pvp->m_myPrefPath) + "tables" + PATH_SEPARATOR_CHAR;
   }
   
   if (!std::filesystem::exists(m_tablesPath)) {
      std::error_code ec;
      std::filesystem::create_directories(m_tablesPath, ec);
      if (ec) {
         PLOGE.printf("VPinballTableManager: Failed to create tables directory: %s", ec.message().c_str());
      } else {
         PLOGI.printf("VPinballTableManager: Created tables directory: %s", m_tablesPath.c_str());
      }
   }
   
   LoadFromJson();
   SaveToJson(GetTablesJsonPath());
}

VPinballTableManager::VPinballTableManager(const std::string& prefPath)
{
   m_tablesPath = prefPath + "tables" + PATH_SEPARATOR_CHAR;
}

VPinballTableManager::~VPinballTableManager()
{
   ClearAll();
}

VPinballStatus VPinballTableManager::LoadFromJson()
{
   string path = GetTablesJsonPath();

   if (!std::filesystem::exists(path)) {
      PLOGW.printf("tables.json does not exist: path=%s, creating empty file", path.c_str());
      
      // Create empty JSON file
      try {
         json emptyJson;
         emptyJson["success"] = true;
         emptyJson["tableCount"] = 0;
         emptyJson["tables"] = json::array();
         
         std::ofstream outFile(path);
         if (outFile.is_open()) {
            outFile << emptyJson.dump(2);
            outFile.close();
            PLOGI.printf("Created empty tables.json: %s", path.c_str());
         } else {
            PLOGE.printf("Failed to create empty tables.json: %s", path.c_str());
            return VPinballStatus::Failure;
         }
      } catch (const std::exception& e) {
         PLOGE.printf("Exception creating empty tables.json: %s", e.what());
         return VPinballStatus::Failure;
      }
   }
   
   try {
      std::ifstream inFile(path);
      if (!inFile.is_open()) 
         return VPinballStatus::Failure;

      string rawContent((std::istreambuf_iterator<char>(inFile)),
                        std::istreambuf_iterator<char>());
      inFile.close();
    
      if (rawContent.empty())
         return VPinballStatus::Failure;
      
      json jsonData;

      try {
         jsonData = json::parse(rawContent);
      } 
      
      catch (const json::parse_error& e) {
         PLOGE.printf("Unable to parse tables.json: error=%s", e.what());
         return VPinballStatus::Failure;
      }
      
      if (!jsonData.contains("tables") || !jsonData["tables"].is_array()) {
         PLOGE.printf("Unable to parse tables.json: missing tables array");
         return VPinballStatus::Failure;
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
      
      PLOGI.printf("VPinballTableManager::LoadFromJson: Successfully loaded %zu tables", m_tables.size());
      NotifyEvent(TableEvent::TablesChanged, nullptr);
      return VPinballStatus::Success;
      
   } catch (const std::exception& e) {
      PLOGI.printf("VPinballTableManager::LoadFromJson: Exception: %s", e.what());
      return VPinballStatus::Failure;
   }
}


VPinballStatus VPinballTableManager::AddTable(const std::string& filePath)
{
   if (FindTableByPath(filePath) != nullptr)
      return VPinballStatus::Success;
   
   VPXTable* newTable = CreateTableFromFile(filePath);
   if (newTable) {
      m_tables.push_back(*newTable);
      delete newTable;
      
      const VPXTable& addedTable = m_tables.back();
      
      NotifyEvent(TableEvent::TablesChanged, &addedTable);
      return VPinballStatus::Success;
   }
   
   return VPinballStatus::Failure;
}


VPinballStatus VPinballTableManager::RenameTable(const std::string& uuid, const std::string& newName)
{
   PLOGI.printf("VPinballTableManager::RenameTable: Renaming table UUID: %s to: %s", uuid.c_str(), newName.c_str());
   
   VPXTable* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("VPinballTableManager::RenameTable: Table not found with UUID: %s", uuid.c_str());
      return VPinballStatus::Failure;
   }
   
   if (table->name)
      delete[] table->name;

   table->name = new char[newName.length() + 1];
   strcpy(table->name, newName.c_str());
   
   table->modifiedAt = std::time(nullptr);
   
   // Save updated tables to JSON
   if (!m_tablesPath.empty()) {
      string jsonPath = GetTablesJsonPath();
      VPinballStatus saveStatus = SaveToJson(jsonPath);
      if (saveStatus != VPinballStatus::Success) {
         PLOGE.printf("VPinballTableManager::RenameTable: Failed to save updated registry after rename");
      }
   }
   
   PLOGI.printf("VPinballTableManager::RenameTable: Successfully renamed table from UUID %s to: %s", uuid.c_str(), newName.c_str());
   
   NotifyEvent(TableEvent::TablesChanged, table);
   
   return VPinballStatus::Success;
}

VPXTable* VPinballTableManager::GetTable(const std::string& uuid)
{
   return FindTableByUUID(uuid);
}

void VPinballTableManager::GetAllTables(VPXTablesData& tablesData)
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

size_t VPinballTableManager::GetTableCount()
{
   return m_tables.size();
}

void VPinballTableManager::ClearAll()
{
   for (auto& table : m_tables)
      CleanupTable(table);

   m_tables.clear();
}

void VPinballTableManager::SetEventCallback(std::function<void(TableEvent, const VPXTable*)> callback)
{
   m_eventCallback = callback;
}

void VPinballTableManager::NotifyEvent(TableEvent event, const VPXTable* table)
{
   if (m_eventCallback)
      m_eventCallback(event, table);
}

VPXTable* VPinballTableManager::FindTableByPath(const std::string& fullPath)
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(), 
                         [&fullPath](const VPXTable& table) {
                            return table.fullPath && std::string(table.fullPath) == fullPath;
                         });
   return (it != m_tables.end()) ? &(*it) : nullptr;
}

VPXTable* VPinballTableManager::FindTableByUUID(const std::string& uuid)
{
   auto it = std::find_if(m_tables.begin(), m_tables.end(), 
                         [&uuid](const VPXTable& table) {
                            return table.uuid && std::string(table.uuid) == uuid;
                         });
   return (it != m_tables.end()) ? &(*it) : nullptr;
}

VPXTable* VPinballTableManager::CreateTableFromFile(const std::string& filePath)
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

void VPinballTableManager::CleanupTable(VPXTable& table)
{
   delete[] table.uuid;
   delete[] table.name;
   delete[] table.fileName;
   delete[] table.fullPath;
   delete[] table.path;
   delete[] table.artwork;
}

std::string VPinballTableManager::GenerateUUID() const
{
   uuid_t uuid;
   uuid_generate_random(uuid);
   char uuid_str[37];
   uuid_unparse(uuid, uuid_str);
   return string(uuid_str);
}

VPinballStatus VPinballTableManager::SaveToJson(const std::string& jsonPath)
{
   PLOGI.printf("VPinballTableManager::SaveToJson: Saving %zu tables to: %s", m_tables.size(), jsonPath.c_str());
   
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
         PLOGE.printf("VPinballTableManager::SaveToJson: Generated JSON failed validation");
         return VPinballStatus::Failure;
      }
      
      std::ofstream outFile(jsonPath);
      if (outFile.is_open()) {
         outFile << jsonString;
         outFile.flush();
         outFile.close();
         
         PLOGI.printf("VPinballTableManager::SaveToJson: Successfully saved %zu tables", m_tables.size());
         NotifyEvent(TableEvent::TablesChanged, nullptr);
         return VPinballStatus::Success;
      } else {
         PLOGE.printf("VPinballTableManager::SaveToJson: Failed to open file for writing: %s", jsonPath.c_str());
         return VPinballStatus::Failure;
      }
      
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("VPinballTableManager::SaveToJson: Exception: %s", e.what());
      return VPinballStatus::Failure;
   }
}

VPinballStatus VPinballTableManager::RefreshTables()
{
   NotifyEvent(TableEvent::RefreshStarted);
   
   VPinballStatus loadStatus = LoadFromJson();
   if (loadStatus != VPinballStatus::Success) {
      PLOGE.printf("VPinballTableManager::RefreshTables: Failed to load from JSON");
      return loadStatus;
   }
   
   VPinballStatus reconcileStatus = ReconcileWithFilesystem();
   if (reconcileStatus != VPinballStatus::Success) {
      PLOGE.printf("VPinballTableManager::RefreshTables: Failed to reconcile with filesystem");
      return reconcileStatus;
   }
   
   NotifyEvent(TableEvent::RefreshCompleted);
   return VPinballStatus::Success;
}

bool VPinballTableManager::ValidateJson(const std::string& jsonString) const
{
   try {
      json validationJson = json::parse(jsonString);
      
      if (!validationJson.contains("tables") || !validationJson["tables"].is_array()) {
         PLOGE.printf("VPinballTableManager::ValidateJson: Missing tables array");
         return false;
      }
      
      if (validationJson["tableCount"] != (int)m_tables.size()) {
         PLOGE.printf("VPinballTableManager::ValidateJson: Table count mismatch");
         return false;
      }
      
      return true;
   } catch (const json::parse_error& e) {
      PLOGE.printf("VPinballTableManager::ValidateJson: Parse error: %s", e.what());
      return false;
   }
}

VPinballStatus VPinballTableManager::ScanDirectory(const std::string& path)
{
   PLOGI.printf("VPinballTableManager::ScanDirectory: Scanning directory: %s", path.c_str());
   
   if (!std::filesystem::exists(path)) {
      PLOGI.printf("VPinballTableManager::ScanDirectory: Directory does not exist: %s", path.c_str());
      return VPinballStatus::Success;
   }
   
   std::vector<std::string> vpxFiles;
   
   try {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
         if (entry.is_regular_file()) {
            string filePath = entry.path().string();
            string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            
            if (extension == ".vpx") {
               PLOGI.printf("VPinballTableManager::ScanDirectory: Found VPX file: %s", filePath.c_str());
               vpxFiles.push_back(filePath);
            }
         }
      }
   } catch (const std::filesystem::filesystem_error& ex) {
      PLOGE.printf("VPinballTableManager::ScanDirectory: Error scanning directory: %s", ex.what());
      return VPinballStatus::Failure;
   }
   
   for (const string& filePath : vpxFiles)
      AddTable(filePath);
   
   return VPinballStatus::Success;
}

VPinballStatus VPinballTableManager::ReconcileWithFilesystem()
{
   PLOGI.printf("VPinballTableManager::ReconcileWithFilesystem: Reconciling with path: %s", m_tablesPath.c_str());
   
   PLOGI.printf("VPinballTableManager::ReconcileWithFilesystem: Scanning for new tables");
   VPinballStatus scanStatus = ScanDirectory(m_tablesPath);
   if (scanStatus != VPinballStatus::Success) {
      PLOGE.printf("VPinballTableManager::ReconcileWithFilesystem: Directory scan failed");
      return scanStatus;
   }
   
   auto it = m_tables.begin();
   while (it != m_tables.end()) {
      if (it->fullPath && !std::filesystem::exists(it->fullPath)) {
         PLOGI.printf("VPinballTableManager::ReconcileWithFilesystem: Removing missing table: %s", it->fullPath);
         CleanupTable(*it);
         it = m_tables.erase(it);
      } else {
         ++it;
      }
   }
   
   string jsonPath = GetTablesJsonPath();
   VPinballStatus saveStatus = SaveToJson(jsonPath);
   if (saveStatus != VPinballStatus::Success) {
      PLOGE.printf("VPinballTableManager::ReconcileWithFilesystem: Failed to save updated registry");
   }
   
   PLOGI.printf("VPinballTableManager::ReconcileWithFilesystem: Reconciliation complete, %zu tables in registry", m_tables.size());
   
   return VPinballStatus::Success;
}

VPinballStatus VPinballTableManager::ImportTable(const std::string& sourceFile)
{
   if (!std::filesystem::exists(sourceFile)) {
      PLOGE.printf("VPinballTableManager::ImportTable: Source file does not exist: %s", sourceFile.c_str());
      return VPinballStatus::Failure;
   }
   
   std::filesystem::path sourcePath(sourceFile);
   string ext = sourcePath.extension().string();
   std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
   
   PLOGI.printf("VPinballTableManager::ImportTable: Importing %s (extension: %s)", sourceFile.c_str(), ext.c_str());
   
   string name = sourcePath.stem().string();
   std::replace(name.begin(), name.end(), '_', ' ');
   
   string tablesPath = m_tablesPath;
   
   try {
      if (ext == ".vpxz") {
         PLOGI.printf("VPinballTableManager::ImportTable: Extracting VPXZ archive: %s", sourceFile.c_str());
         
         string tempDir = std::filesystem::temp_directory_path().string() + "/vpinball_import_" + std::to_string(std::time(nullptr));
         std::filesystem::create_directories(tempDir);
         
         VPinballStatus extractStatus = ExtractZipToDirectory(sourceFile, tempDir);
         if (extractStatus != VPinballStatus::Success) {
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
            PLOGE.printf("VPinballTableManager::ImportTable: No VPX files found in archive");
            std::filesystem::remove_all(tempDir);
            return VPinballStatus::Failure;
         }
         
         VPinballStatus result = VPinballStatus::Success;
         for (const string& vpxFile : vpxFiles) {
            std::filesystem::path vpxPath(vpxFile);
            string vpxName = vpxPath.stem().string();
            
            string folderName = SanitizeTableName(vpxName);
            folderName = GetUniqueTableFolder(folderName, tablesPath);
            
            std::filesystem::path destFolder = std::filesystem::path(tablesPath) / folderName;
            std::filesystem::create_directories(destFolder);
            
            std::filesystem::path sourceDir = vpxPath.parent_path();
            
            try {
               std::filesystem::copy(sourceDir, destFolder, 
                  std::filesystem::copy_options::recursive | 
                  std::filesystem::copy_options::overwrite_existing);
               
               PLOGI.printf("VPinballTableManager::ImportTable: Copied entire directory structure from %s to %s", 
                  sourceDir.string().c_str(), destFolder.string().c_str());
            } catch (const std::filesystem::filesystem_error& ex) {
               PLOGE.printf("VPinballTableManager::ImportTable: Failed to copy directory structure: %s", ex.what());
               result = VPinballStatus::Failure;
               continue;
            }
            
            std::filesystem::path destFile = destFolder / vpxPath.filename();
            
            VPinballStatus addStatus = AddTable(destFile.string());
            if (addStatus != VPinballStatus::Success)
               result = VPinballStatus::Failure;
            else {
               PLOGI.printf("VPinballTableManager::ImportTable: Successfully imported VPX from archive: %s", vpxName.c_str());
            }
         }
         
         std::filesystem::remove_all(tempDir);
         return result;
         
      }
      else if (ext == ".vpx") {
         PLOGI.printf("VPinballTableManager::ImportTable: Importing single VPX file: %s", sourceFile.c_str());
         
         string folderName = SanitizeTableName(name);
         folderName = GetUniqueTableFolder(folderName, tablesPath);
         
         std::filesystem::path destFolder = std::filesystem::path(tablesPath) / folderName;
         
         std::filesystem::create_directories(destFolder);
      
         std::filesystem::path destFile = destFolder / sourcePath.filename();
         std::filesystem::copy_file(sourcePath, destFile);
         
         VPinballStatus addStatus = AddTable(destFile.string());
         if (addStatus == VPinballStatus::Success) {
            PLOGI.printf("VPinballTableManager::ImportTable: Successfully imported table: %s", name.c_str());
         }
         
         return addStatus;
      }
      
      PLOGE.printf("VPinballTableManager::ImportTable: Unsupported file extension: %s", ext.c_str());
      return VPinballStatus::Failure;
      
   } catch (const std::exception& e) {
      PLOGE.printf("VPinballTableManager::ImportTable: Exception: %s", e.what());
      return VPinballStatus::Failure;
   }
}


VPinballStatus VPinballTableManager::ExtractZipToDirectory(const std::string& zipFile, const std::string& destDir)
{
   PLOGI.printf("ExtractZipToDirectory: extracting %s to %s", zipFile.c_str(), destDir.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(zipFile.c_str(), ZIP_RDONLY, &error);
   if (!zip_archive) {
      PLOGE.printf("ExtractZipToDirectory: Failed to open zip file: %s (error: %d)", zipFile.c_str(), error);
      return VPinballStatus::Failure;
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
            return VPinballStatus::Failure;
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
   return VPinballStatus::Success;
}


std::string VPinballTableManager::GetTablesJsonPath()
{
   std::string path = m_tablesPath;
   if (!path.empty() && path.back() != '/' && path.back() != '\\') {
      path += PATH_SEPARATOR_CHAR;
   }
   return path + "tables.json";
}


VPinballStatus VPinballTableManager::DeleteTable(const std::string& uuid)
{
   PLOGI.printf("VPinballTableManager::DeleteTable: Deleting table with UUID: %s", uuid.c_str());
   
   // Find the table by UUID in our internal registry
   VPXTable* table = nullptr;
   for (auto& t : m_tables) {
      if (string(t.uuid) == uuid) {
         table = &t;
         break;
      }
   }
   
   if (!table || !table->path) {
      PLOGE.printf("VPinballTableManager::DeleteTable: Table with UUID %s not found", uuid.c_str());
      return VPinballStatus::Failure;
   }
   
   // Get the full path to the VPX file
   std::filesystem::path tablePath = std::filesystem::path(m_tablesPath) / table->path;
   std::filesystem::path tableDir = tablePath.parent_path();
   std::filesystem::path tablesDir(m_tablesPath);
   
   PLOGI.printf("VPinballTableManager::DeleteTable: Table path: %s", tablePath.string().c_str());
   PLOGI.printf("VPinballTableManager::DeleteTable: Table directory: %s", tableDir.string().c_str());
   PLOGI.printf("VPinballTableManager::DeleteTable: Main tables directory: %s", tablesDir.string().c_str());
   
   // Safety check: Never delete the main tables directory
   if (tableDir == tablesDir) {
      PLOGE.printf("VPinballTableManager::DeleteTable: Refusing to delete from main tables directory");
      return VPinballStatus::Failure;
   }
   
   // Check if the table directory exists
   if (!std::filesystem::exists(tableDir)) {
      PLOGI.printf("VPinballTableManager::DeleteTable: Table directory does not exist, only removing from registry");
   } else {
      int vpxCount = 0;
      std::vector<std::filesystem::path> vpxFiles;
      
      try {
         for (const auto& entry : std::filesystem::directory_iterator(tableDir)) {
            if (entry.is_regular_file()) {
               string fileExt = entry.path().extension().string();
               std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);
               if (fileExt == ".vpx") {
                  vpxCount++;
                  vpxFiles.push_back(entry.path());
               }
            }
         }
      } catch (const std::filesystem::filesystem_error& ex) {
         PLOGE.printf("VPinballTableManager::DeleteTable: Error scanning directory: %s", ex.what());
         return VPinballStatus::Failure;
      }
      
      PLOGI.printf("VPinballTableManager::DeleteTable: Found %d VPX files in directory", vpxCount);
      
      if (vpxCount <= 1) {
         PLOGI.printf("VPinballTableManager::DeleteTable: Table is alone in subfolder, deleting entire directory");
         try {
            std::filesystem::remove_all(tableDir);
            PLOGI.printf("VPinballTableManager::DeleteTable: Successfully deleted directory: %s", tableDir.string().c_str());
         } catch (const std::filesystem::filesystem_error& ex) {
            PLOGE.printf("VPinballTableManager::DeleteTable: Failed to delete directory: %s", ex.what());
            return VPinballStatus::Failure;
         }
      } else {
         PLOGI.printf("VPinballTableManager::DeleteTable: Multiple VPX files in directory, deleting only VPX file");
         try {
            std::filesystem::remove(tablePath);
            PLOGI.printf("VPinballTableManager::DeleteTable: Successfully deleted VPX file: %s", tablePath.string().c_str());
         } catch (const std::filesystem::filesystem_error& ex) {
            PLOGE.printf("VPinballTableManager::DeleteTable: Failed to delete VPX file: %s", ex.what());
            return VPinballStatus::Failure;
         }
      }
   }
   
   PLOGI.printf("VPinballTableManager::DeleteTable: File deletion completed, removing from registry");
   
   for (auto it = m_tables.begin(); it != m_tables.end(); ++it) {
      if (string(it->uuid) == uuid) {
         VPXTable removedTable = *it;
         CleanupTable(*it);
         m_tables.erase(it);
         NotifyEvent(TableEvent::TablesChanged, &removedTable);
         PLOGI.printf("VPinballTableManager::DeleteTable: Successfully removed table from registry");
         return VPinballStatus::Success;
      }
   }
   
   PLOGE.printf("VPinballTableManager::DeleteTable: Table with UUID %s not found in registry", uuid.c_str());
   return VPinballStatus::Failure;
}

std::string VPinballTableManager::SanitizeTableName(const std::string& name)
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

std::string VPinballTableManager::GetUniqueTableFolder(const std::string& baseName, const std::string& tablesPath)
{
   string sanitizedName = SanitizeTableName(baseName);
   string candidate = sanitizedName;
   int counter = 2;
   
   while (std::filesystem::exists(std::filesystem::path(tablesPath) / candidate)) {
      candidate = sanitizedName + "-" + std::to_string(counter);
      counter++;
   }
   
   return candidate;
}

VPinballStatus VPinballTableManager::SetTableArtwork(const std::string& uuid, const std::string& artworkPath)
{
   PLOGI.printf("VPinballTableManager::SetTableArtwork: Setting artwork for UUID: %s to: %s", uuid.c_str(), artworkPath.c_str());
   
   VPXTable* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("VPinballTableManager::SetTableArtwork: Table not found with UUID: %s", uuid.c_str());
      return VPinballStatus::Failure;
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
               PLOGI.printf("VPinballTableManager::SetTableArtwork: Removed artwork file: %s", currentArtworkPath.c_str());
            } else {
               PLOGE.printf("VPinballTableManager::SetTableArtwork: Failed to remove artwork file: %s - %s", 
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
   
   PLOGI.printf("VPinballTableManager::SetTableArtwork: Successfully set artwork for table: %s", 
               table->name ? table->name : "unknown");
   
   SaveToJson(GetTablesJsonPath());
   NotifyEvent(TableEvent::TablesChanged, table);
   
   return VPinballStatus::Success;
}

const std::string& VPinballTableManager::GetTablesPath()
{
   return m_tablesPath;
}

VPinballStatus VPinballTableManager::CompressDirectory(const std::string& sourcePath, const std::string& destinationPath)
{
   PLOGI.printf("VPinballTableManager::CompressDirectory: Compressing %s to %s", sourcePath.c_str(), destinationPath.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(destinationPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
   if (!zip_archive) {
      PLOGE.printf("VPinballTableManager::CompressDirectory: Failed to create archive: %s (error: %d)", destinationPath.c_str(), error);
      return VPinballStatus::Failure;
   }

   std::filesystem::path base(sourcePath);
   size_t base_len = base.string().length();

   vector<std::filesystem::path> items;
   try {
      for (auto& item : std::filesystem::recursive_directory_iterator(base))
         items.push_back(item.path());
   } catch (const std::filesystem::filesystem_error& ex) {
      PLOGE.printf("VPinballTableManager::CompressDirectory: Error scanning directory: %s", ex.what());
      zip_close(zip_archive);
      return VPinballStatus::Failure;
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
            PLOGE.printf("VPinballTableManager::CompressDirectory: Failed to create source for file: %s", item.string().c_str());
            zip_close(zip_archive);
            return VPinballStatus::Failure;
         }
         if (zip_file_add(zip_archive, rel.c_str(), zip_source, ZIP_FL_ENC_UTF_8) < 0) {
            PLOGE.printf("VPinballTableManager::CompressDirectory: Failed to add file to archive: %s", rel.c_str());
            zip_source_free(zip_source);
            zip_close(zip_archive);
            return VPinballStatus::Failure;
         }
      }

      ++done;
      int progress = int((done * 100) / total);
      if (m_eventCallback && done % 10 == 0) {
         PLOGI.printf("VPinballTableManager::CompressDirectory: Progress: %d%% (%zu/%zu files)", progress, done, total);
      }
   }

   zip_close(zip_archive);
   PLOGI.printf("VPinballTableManager::CompressDirectory: Successfully compressed %zu files", total);
   return VPinballStatus::Success;
}

VPinballStatus VPinballTableManager::UncompressArchive(const std::string& archivePath)
{
   PLOGI.printf("VPinballTableManager::UncompressArchive: Extracting %s", archivePath.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(archivePath.c_str(), ZIP_RDONLY, &error);
   if (!zip_archive) {
      PLOGE.printf("VPinballTableManager::UncompressArchive: Failed to open archive: %s (error: %d)", archivePath.c_str(), error);
      return VPinballStatus::Failure;
   }

   zip_int64_t file_count = zip_get_num_entries(zip_archive, 0);
   PLOGI.printf("VPinballTableManager::UncompressArchive: Found %lld files in archive", file_count);
   
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
            PLOGE.printf("VPinballTableManager::UncompressArchive: Failed to open file in archive: %s", filename.c_str());
            zip_close(zip_archive);
            return VPinballStatus::Failure;
         }
         std::ofstream ofs(out, std::ios::binary);
         char buf[4096];
         zip_int64_t len;
         while ((len = zip_fread(zip_file, buf, sizeof(buf))) > 0)
            ofs.write(buf, len);
         zip_fclose(zip_file);
         
         PLOGI.printf("VPinballTableManager::UncompressArchive: Extracted file: %s", out.string().c_str());
      }

      int progress = int((i * 100) / file_count);
      if (m_eventCallback && i % 10 == 0) {
         PLOGI.printf("VPinballTableManager::UncompressArchive: Progress: %d%% (%llu/%lld files)", progress, i, file_count);
      }
   }

   zip_close(zip_archive);
   PLOGI.printf("VPinballTableManager::UncompressArchive: Successfully extracted archive");
   return VPinballStatus::Success;
}

std::string VPinballTableManager::ExportTable(const std::string& uuid)
{
   PLOGI.printf("VPinballTableManager::ExportTable: Exporting table with UUID: %s", uuid.c_str());
   
   VPXTable* table = FindTableByUUID(uuid);
   if (!table) {
      PLOGE.printf("VPinballTableManager::ExportTable: Table not found with UUID: %s", uuid.c_str());
      return "";
   }
   
   std::filesystem::path tablePath(table->fullPath);
   std::filesystem::path tableDir = tablePath.parent_path();
   
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
      PLOGE.printf("VPinballTableManager::ExportTable: Failed to remove existing temp file: %s", ex.what());
   }
   
   VPinballStatus compressResult = CompressDirectory(tableDir.string(), tempFile.string());
   if (compressResult != VPinballStatus::Success) {
      PLOGE.printf("VPinballTableManager::ExportTable: Failed to compress table directory");
      return "";
   }
   
   PLOGI.printf("VPinballTableManager::ExportTable: Successfully exported table to: %s", tempFile.string().c_str());
   return tempFile.string();
}

char* VPinballTableManager::GetTablesJsonCopy()
{
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
   std::string jsonStr = j.dump();
   
   char* result = new char[jsonStr.length() + 1];
   strcpy(result, jsonStr.c_str());
   return result;
}

char* VPinballTableManager::GetTableJsonCopy(const std::string& uuid)
{
   VPXTable* table = FindTableByUUID(uuid);
   
   json j;
   if (table) {
      j["success"] = true;
      j["uuid"] = table->uuid ? table->uuid : "";
      j["name"] = table->name ? table->name : "";
      j["fileName"] = table->fileName ? table->fileName : "";
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