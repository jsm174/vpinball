#include "core/stdafx.h"

#include "FileSystemProvider.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace VPinballLib {

bool FileSystemProvider::WriteFile(const string& path, const string& content)
{
   PLOGD.printf("Writing to: %s (%zu bytes)", path.c_str(), content.length());

   try {
      std::ofstream file(path, std::ios::out | std::ios::trunc);
      if (!file.is_open()) {
         PLOGE.printf("Failed to open file: %s", path.c_str());
         return false;
      }

      file << content;
      file.flush();
      file.close();

      if (!file.good()) {
         PLOGE.printf("Write error for: %s", path.c_str());
         return false;
      }

      PLOGD.printf("Successfully wrote to: %s", path.c_str());
      return true;
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("Exception: %s", e.what());
      return false;
   }
}

string FileSystemProvider::ReadFile(const string& path)
{
   PLOGD.printf("Reading: %s", path.c_str());

   try {
      std::ifstream file(path);
      if (!file.is_open()) {
         PLOGE.printf("Failed to open: %s", path.c_str());
         return "";
      }

      std::stringstream buffer;
      buffer << file.rdbuf();
      string content = buffer.str();

      PLOGD.printf("Read %zu bytes from: %s", content.length(), path.c_str());
      return content;
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("Exception: %s", e.what());
      return "";
   }
}

bool FileSystemProvider::Exists(const string& path)
{
   try {
      bool exists = std::filesystem::exists(path);
      PLOGD.printf("'%s': %d", path.c_str(), exists);
      return exists;
   }
   
   catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("Error checking '%s': %s", path.c_str(), e.what());
      return false;
   }
}

vector<string> FileSystemProvider::ListFilesRecursive(const string& path, const string& extension)
{
   PLOGD.printf("Scanning '%s' for '%s' files", path.c_str(), extension.c_str());
   vector<string> files;

   if (!std::filesystem::exists(path)) {
      PLOGD.printf("Path does not exist");
      return files;
   }

   try {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
         if (entry.is_regular_file()) {
            string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == extension) {
               files.push_back(entry.path().string());
            }
         }
      }
      PLOGD.printf("Found %zu files", files.size());
   }
   
   catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("Error: %s", e.what());
   }

   return files;
}

bool FileSystemProvider::CreateDirectories(const string& path)
{
   try {
      std::error_code ec;
      std::filesystem::create_directories(path, ec);
      if (ec) {
         PLOGE.printf("Failed for '%s': %s", path.c_str(), ec.message().c_str());
         return false;
      }
      PLOGD.printf("Created: %s", path.c_str());
      return true;
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("Exception: %s", e.what());
      return false;
   }
}

bool FileSystemProvider::CopyFile(const string& sourcePath, const string& destPath)
{
   PLOGD.printf("FileSystemProvider::CopyFile: Copying '%s' to '%s'", sourcePath.c_str(), destPath.c_str());

   try {
      std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);
      PLOGD.printf("FileSystemProvider::CopyFile: Successfully copied");
      return true;
   }
   
   catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystemProvider::CopyFile: Error: %s", e.what());
      return false;
   }
}

bool FileSystemProvider::CopyDirectory(const string& sourcePath, const string& destPath)
{
   PLOGD.printf("FileSystemProvider::CopyDirectory: Copying '%s' to '%s'", sourcePath.c_str(), destPath.c_str());

   try {
      std::filesystem::copy(sourcePath, destPath,
         std::filesystem::copy_options::recursive |
         std::filesystem::copy_options::overwrite_existing);
      PLOGD.printf("FileSystemProvider::CopyDirectory: Successfully copied");
      return true;
   }
   
   catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystemProvider::CopyDirectory: Error: %s", e.what());
      return false;
   }
}

string FileSystemProvider::JoinPath(const string& basePath, const string& relativePath)
{
   try {
      std::filesystem::path result = std::filesystem::path(basePath) / relativePath;
      return result.string();
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("FileSystemProvider::JoinPath: Exception joining '%s' and '%s': %s", basePath.c_str(), relativePath.c_str(), e.what());
      return basePath + "/" + relativePath;
   }
}

bool FileSystemProvider::Delete(const string& path)
{
   PLOGD.printf("FileSystemProvider::Delete: Deleting: %s", path.c_str());

   try {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
      if (ec) {
         PLOGE.printf("FileSystemProvider::Delete: Failed for '%s': %s", path.c_str(), ec.message().c_str());
         return false;
      }
      PLOGD.printf("FileSystemProvider::Delete: Successfully deleted: %s", path.c_str());
      return true;
   }
   
   catch (const std::exception& e) {
      PLOGE.printf("FileSystemProvider::Delete: Exception: %s", e.what());
      return false;
   }
}

bool FileSystemProvider::IsDirectory(const string& path)
{
   try {
      bool isDir = std::filesystem::is_directory(path);
      PLOGD.printf("FileSystemProvider::IsDirectory: '%s': %d", path.c_str(), isDir);
      return isDir;
   }
   
   catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystemProvider::IsDirectory: Error checking '%s': %s", path.c_str(), e.what());
      return false;
   }
}

vector<string> FileSystemProvider::ListDirectory(const string& path)
{
   PLOGD.printf("FileSystemProvider::ListDirectory: Listing: %s", path.c_str());
   vector<string> entries;

   try {
      for (const auto& entry : std::filesystem::directory_iterator(path)) {
         entries.push_back(entry.path().string());
      }
      PLOGD.printf("FileSystemProvider::ListDirectory: Found %zu entries", entries.size());
   }
   
   catch (const std::filesystem::filesystem_error& e) {
      PLOGE.printf("FileSystemProvider::ListDirectory: Error: %s", e.what());
   }

   return entries;
}

}
