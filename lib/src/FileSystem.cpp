#include "FileSystem.h"
#include "core/stdafx.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#ifdef __ANDROID__
extern "C" {
   // JNI functions - implemented in VPinballLib_JNI.cpp
   bool JNI_FileSystem_WriteFile(const char* relativePath, const char* content);
   char* JNI_FileSystem_ReadFile(const char* relativePath);
   bool JNI_FileSystem_Exists(const char* relativePath);
   char* JNI_FileSystem_ListFiles(const char* extension);
   bool JNI_FileSystem_CopyFile(const char* sourcePath, const char* destPath);
   bool JNI_FileSystem_CopyDirectory(const char* sourcePath, const char* destPath);
   bool JNI_FileSystem_CopySAFToFilesystem(const char* safRelativePath, const char* destPath);
   bool JNI_FileSystem_Delete(const char* relativePath);
   bool JNI_FileSystem_IsDirectory(const char* relativePath);
   char* JNI_FileSystem_ListDirectory(const char* relativePath);
}
#endif

namespace VPinballLib {

std::string FileSystem::s_tablesPath;

void FileSystem::SetTablesPath(const std::string& tablesPath)
{
   s_tablesPath = tablesPath;
   PLOGI.printf("FileSystem::SetTablesPath: Set to '%s'", tablesPath.c_str());
}

bool FileSystem::IsSpecialUri(const std::string& path)
{
   return path.starts_with("content://");
}

std::string FileSystem::ExtractRelativePath(const std::string& fullPath)
{
   if (!IsSpecialUri(fullPath)) {
      return fullPath;
   }

   if (s_tablesPath.empty()) {
      PLOGE.printf("FileSystem::ExtractRelativePath: TablesPath not set, cannot extract relative path");
      return fullPath;
   }

   if (fullPath.starts_with(s_tablesPath)) {
      std::string relativePath = fullPath.substr(s_tablesPath.length());
      if (!relativePath.empty() && relativePath[0] == '/') {
         relativePath = relativePath.substr(1);
      }
      PLOGI.printf("FileSystem::ExtractRelativePath: Extracted '%s' from '%s'", relativePath.c_str(), fullPath.c_str());
      return relativePath;
   }

   PLOGE.printf("FileSystem::ExtractRelativePath: Path '%s' doesn't start with TablesPath '%s'", fullPath.c_str(), s_tablesPath.c_str());
   return fullPath;
}

std::vector<std::string> FileSystem::ParseJsonArray(const char* jsonStr)
{
   std::vector<std::string> result;

   if (!jsonStr || jsonStr[0] == '\0')
      return result;

   try {
      json j = json::parse(jsonStr);
      if (j.is_array()) {
         for (const auto& item : j) {
            if (item.is_string()) {
               result.push_back(item.get<std::string>());
            }
         }
      }
   } catch (const std::exception& e) {
      PLOGE.printf("FileSystem::ParseJsonArray: Failed to parse JSON: %s", e.what());
   }

   return result;
}

bool FileSystem::WriteFile(const std::string& path, const std::string& content)
{
   PLOGI.printf("FileSystem::WriteFile: Writing to: %s (%zu bytes)", path.c_str(), content.length());

#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      std::string relativePath = ExtractRelativePath(path);
      PLOGI.printf("FileSystem::WriteFile: Using SAF proxy for: %s", relativePath.c_str());
      bool result = JNI_FileSystem_WriteFile(relativePath.c_str(), content.c_str());
      PLOGI.printf("FileSystem::WriteFile: SAF write result: %d", result);
      return result;
   }
#endif

   // Normal filesystem write
   try {
      std::ofstream file(path, std::ios::out | std::ios::trunc);
      if (!file.is_open()) {
         PLOGE.printf("FileSystem::WriteFile: Failed to open file: %s", path.c_str());
         return false;
      }

      file << content;
      file.flush();
      file.close();

      if (!file.good()) {
         PLOGE.printf("FileSystem::WriteFile: Write error for: %s", path.c_str());
         return false;
      }

      PLOGI.printf("FileSystem::WriteFile: Successfully wrote to: %s", path.c_str());
      return true;
   } catch (const std::exception& e) {
      PLOGE.printf("FileSystem::WriteFile: Exception: %s", e.what());
      return false;
   }
}

std::string FileSystem::ReadFile(const std::string& path)
{
   PLOGI.printf("FileSystem::ReadFile: Reading: %s", path.c_str());

#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      std::string relativePath = ExtractRelativePath(path);
      PLOGI.printf("FileSystem::ReadFile: Using SAF proxy for: %s", relativePath.c_str());
      char* result = JNI_FileSystem_ReadFile(relativePath.c_str());
      std::string content;
      if (result) {
         content = std::string(result);
         free(result);
         PLOGI.printf("FileSystem::ReadFile: SAF read %zu bytes", content.length());
      } else {
         PLOGE.printf("FileSystem::ReadFile: SAF read returned null");
      }
      return content;
   }
#endif

   // Normal filesystem read
   try {
      std::ifstream file(path);
      if (!file.is_open()) {
         PLOGE.printf("FileSystem::ReadFile: Failed to open: %s", path.c_str());
         return "";
      }

      std::stringstream buffer;
      buffer << file.rdbuf();
      std::string content = buffer.str();

      PLOGI.printf("FileSystem::ReadFile: Read %zu bytes from: %s", content.length(), path.c_str());
      return content;
   } catch (const std::exception& e) {
      PLOGE.printf("FileSystem::ReadFile: Exception: %s", e.what());
      return "";
   }
}

bool FileSystem::Exists(const std::string& path)
{
#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      std::string relativePath = ExtractRelativePath(path);
      bool result = JNI_FileSystem_Exists(relativePath.c_str());
      PLOGI.printf("FileSystem::Exists: SAF check for '%s': %d", relativePath.c_str(), result);
      return result;
   }
#endif

   // Normal filesystem check
   bool exists = std::filesystem::exists(path);
   PLOGI.printf("FileSystem::Exists: '%s': %d", path.c_str(), exists);
   return exists;
}

std::vector<std::string> FileSystem::ListFilesRecursive(const std::string& path, const std::string& extension)
{
   PLOGI.printf("FileSystem::ListFilesRecursive: Scanning '%s' for '%s' files", path.c_str(), extension.c_str());
   std::vector<std::string> files;

#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      PLOGI.printf("FileSystem::ListFilesRecursive: Using SAF proxy");
      char* jsonResult = JNI_FileSystem_ListFiles(extension.c_str());
      if (jsonResult) {
         files = ParseJsonArray(jsonResult);
         free(jsonResult);
         PLOGI.printf("FileSystem::ListFilesRecursive: SAF returned %zu files", files.size());

         // Prepend tree URI to each relative path
         for (auto& file : files) {
            file = s_tablesPath + "/" + file;
         }
      } else {
         PLOGE.printf("FileSystem::ListFilesRecursive: SAF returned null");
      }
      return files;
   }
#endif

   // Normal filesystem scan
   if (!std::filesystem::exists(path)) {
      PLOGI.printf("FileSystem::ListFilesRecursive: Path does not exist");
      return files;
   }

   try {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
         if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == extension) {
               files.push_back(entry.path().string());
            }
         }
      }
      PLOGI.printf("FileSystem::ListFilesRecursive: Found %zu files", files.size());
   } catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystem::ListFilesRecursive: Error: %s", e.what());
   }

   return files;
}

bool FileSystem::CreateDirectories(const std::string& path)
{
#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      // SAF directories are auto-created when writing files
      PLOGI.printf("FileSystem::CreateDirectories: SAF path, will auto-create: %s", path.c_str());
      return true;
   }
#endif

   // Normal filesystem directory creation
   try {
      std::error_code ec;
      std::filesystem::create_directories(path, ec);
      if (ec) {
         PLOGE.printf("FileSystem::CreateDirectories: Failed for '%s': %s", path.c_str(), ec.message().c_str());
         return false;
      }
      PLOGI.printf("FileSystem::CreateDirectories: Created: %s", path.c_str());
      return true;
   } catch (const std::exception& e) {
      PLOGE.printf("FileSystem::CreateDirectories: Exception: %s", e.what());
      return false;
   }
}

bool FileSystem::CopyFile(const std::string& sourcePath, const std::string& destPath)
{
   PLOGI.printf("FileSystem::CopyFile: Copying '%s' to '%s'", sourcePath.c_str(), destPath.c_str());

#ifdef __ANDROID__
   // If destination is SAF, use JNI copy (handles binary files properly)
   if (IsSpecialUri(destPath)) {
      PLOGI.printf("FileSystem::CopyFile: Destination is SAF, using JNI copy");

      // For normal filesystem -> SAF, use CopyDirectory JNI (it handles individual files too)
      // The JNI implementation will read the file as binary and copy via ContentResolver
      std::string relativeDest = ExtractRelativePath(destPath);

      // Get just the filename for the destination
      std::filesystem::path destFilePath(relativeDest);
      std::string destFileName = destFilePath.filename().string();
      std::string destDir = destFilePath.parent_path().string();

      // If we have a directory component, we need to create it first
      if (!destDir.empty()) {
         CreateDirectories(s_tablesPath + "/" + destDir);
      }

      // Use the directory copy function which handles binary files correctly
      // It expects: source = full filesystem path, dest = SAF relative path
      return JNI_FileSystem_CopyDirectory(sourcePath.c_str(), relativeDest.c_str());
   }

   // If source is SAF and destination is normal filesystem
   if (IsSpecialUri(sourcePath) && !IsSpecialUri(destPath)) {
      PLOGI.printf("FileSystem::CopyFile: SAF source to normal filesystem dest, using JNI");
      std::string relativePath = ExtractRelativePath(sourcePath);
      return JNI_FileSystem_CopySAFToFilesystem(relativePath.c_str(), destPath.c_str());
   }
#endif

   // Normal filesystem copy
   try {
      std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);
      PLOGI.printf("FileSystem::CopyFile: Successfully copied");
      return true;
   } catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystem::CopyFile: Error: %s", e.what());
      return false;
   }
}

bool FileSystem::CopyDirectory(const std::string& sourcePath, const std::string& destPath)
{
   PLOGI.printf("FileSystem::CopyDirectory: Copying '%s' to '%s'", sourcePath.c_str(), destPath.c_str());

#ifdef __ANDROID__
   // If source is SAF and destination is normal filesystem
   if (IsSpecialUri(sourcePath) && !IsSpecialUri(destPath)) {
      PLOGI.printf("FileSystem::CopyDirectory: SAF source to normal filesystem dest, using JNI");
      std::string relativePath = ExtractRelativePath(sourcePath);
      return JNI_FileSystem_CopySAFToFilesystem(relativePath.c_str(), destPath.c_str());
   }

   // If destination is SAF, use SAF copy
   if (IsSpecialUri(destPath)) {
      PLOGI.printf("FileSystem::CopyDirectory: Destination is SAF, using SAF proxy");

      // If source is also SAF, use SAF-to-SAF copy
      if (IsSpecialUri(sourcePath)) {
         std::string relativeSource = ExtractRelativePath(sourcePath);
         std::string relativeDest = ExtractRelativePath(destPath);
         return JNI_FileSystem_CopyDirectory(relativeSource.c_str(), relativeDest.c_str());
      }

      // Copy from normal filesystem to SAF - need to recursively copy
      // Use JNI proxy that takes normal filesystem path
      return JNI_FileSystem_CopyDirectory(sourcePath.c_str(), ExtractRelativePath(destPath).c_str());
   }
#endif

   // Normal filesystem copy
   try {
      std::filesystem::copy(sourcePath, destPath,
         std::filesystem::copy_options::recursive |
         std::filesystem::copy_options::overwrite_existing);
      PLOGI.printf("FileSystem::CopyDirectory: Successfully copied");
      return true;
   } catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystem::CopyDirectory: Error: %s", e.what());
      return false;
   }
}

std::string FileSystem::JoinPath(const std::string& basePath, const std::string& relativePath)
{
   // Handle content:// URIs specially
   if (IsSpecialUri(basePath)) {
      return basePath + "/" + relativePath;
   }

   // For normal paths, use filesystem path joining
   std::filesystem::path result = std::filesystem::path(basePath) / relativePath;
   return result.string();
}

bool FileSystem::Delete(const std::string& path)
{
   PLOGI.printf("FileSystem::Delete: Deleting: %s", path.c_str());

#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      std::string relativePath = ExtractRelativePath(path);
      bool result = JNI_FileSystem_Delete(relativePath.c_str());
      PLOGI.printf("FileSystem::Delete: SAF delete result: %d", result);
      return result;
   }
#endif

   // Normal filesystem delete
   try {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
      if (ec) {
         PLOGE.printf("FileSystem::Delete: Failed for '%s': %s", path.c_str(), ec.message().c_str());
         return false;
      }
      PLOGI.printf("FileSystem::Delete: Successfully deleted: %s", path.c_str());
      return true;
   } catch (const std::exception& e) {
      PLOGE.printf("FileSystem::Delete: Exception: %s", e.what());
      return false;
   }
}

bool FileSystem::IsDirectory(const std::string& path)
{
#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      std::string relativePath = ExtractRelativePath(path);
      bool result = JNI_FileSystem_IsDirectory(relativePath.c_str());
      PLOGI.printf("FileSystem::IsDirectory: SAF check for '%s': %d", relativePath.c_str(), result);
      return result;
   }
#endif

   // Normal filesystem check
   bool isDir = std::filesystem::is_directory(path);
   PLOGI.printf("FileSystem::IsDirectory: '%s': %d", path.c_str(), isDir);
   return isDir;
}

std::vector<std::string> FileSystem::ListDirectory(const std::string& path)
{
   PLOGI.printf("FileSystem::ListDirectory: Listing: %s", path.c_str());
   std::vector<std::string> entries;

#ifdef __ANDROID__
   if (IsSpecialUri(path)) {
      std::string relativePath = ExtractRelativePath(path);
      PLOGI.printf("FileSystem::ListDirectory: Using SAF proxy for: %s", relativePath.c_str());
      char* jsonResult = JNI_FileSystem_ListDirectory(relativePath.c_str());
      if (jsonResult) {
         entries = ParseJsonArray(jsonResult);
         free(jsonResult);
         PLOGI.printf("FileSystem::ListDirectory: SAF returned %zu entries", entries.size());

         // Prepend tree URI to each relative path
         for (auto& entry : entries) {
            entry = s_tablesPath + "/" + entry;
         }
      } else {
         PLOGE.printf("FileSystem::ListDirectory: SAF returned null");
      }
      return entries;
   }
#endif

   // Normal filesystem listing
   try {
      for (const auto& entry : std::filesystem::directory_iterator(path)) {
         entries.push_back(entry.path().string());
      }
      PLOGI.printf("FileSystem::ListDirectory: Found %zu entries", entries.size());
   } catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystem::ListDirectory: Error: %s", e.what());
   }

   return entries;
}

}
