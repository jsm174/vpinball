#include "core/stdafx.h"

#include "FileSystem.h"
#include "FileSystemProvider.h"
#ifdef __ANDROID__
#include "SAFFileSystemProvider.h"
#endif
#include <filesystem>
#include <fstream>
#include <zip.h>
#include <nlohmann/json.hpp>

namespace VPinballLib {

static FileSystemProvider s_filesystemProvider;
#ifdef __ANDROID__
static SAFFileSystemProvider s_safProvider;
#endif

void FileSystem::Init()
{
#ifdef __ANDROID__
   SAFFileSystemProvider::Init();
#endif
}

bool FileSystem::IsSAFUri(const string& path)
{
   return path.starts_with("content://");
}

string FileSystem::ExtractRelativePath(const string& fullPath, const string& tablesPath)
{
   try {
      if (!IsSAFUri(fullPath))
         return fullPath;

      if (tablesPath.empty()) {
         PLOGE.printf("TablesPath not set, cannot extract relative path");
         return fullPath;
      }

      if (fullPath.starts_with(tablesPath)) {
         string relativePath = fullPath.substr(tablesPath.length());
         if (!relativePath.empty() && relativePath[0] == '/')
            relativePath = relativePath.substr(1);
         PLOGD.printf("Extracted '%s' from '%s'", relativePath.c_str(), fullPath.c_str());
         return relativePath;
      }

      PLOGE.printf("Path '%s' doesn't start with TablesPath '%s'", fullPath.c_str(), tablesPath.c_str());
      return fullPath;
   }

   catch (const std::exception& e) {
      PLOGE.printf("Exception: %s", e.what());
      return fullPath;
   }
}

bool FileSystem::WriteFile(const string& path, const string& content, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.WriteFile(path, content);
   }
#endif
   return s_filesystemProvider.WriteFile(path, content);
}

string FileSystem::ReadFile(const string& path, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.ReadFile(path);
   }
#endif
   return s_filesystemProvider.ReadFile(path);
}

bool FileSystem::Exists(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path))
      return s_safProvider.Exists(path);
#endif
   return s_filesystemProvider.Exists(path);
}

vector<string> FileSystem::ListFilesRecursive(const string& path, const string& extension, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.ListFilesRecursive(path, extension);
   }
#endif
   return s_filesystemProvider.ListFilesRecursive(path, extension);
}

bool FileSystem::CreateDirectories(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path))
      return s_safProvider.CreateDirectories(path);
#endif
   return s_filesystemProvider.CreateDirectories(path);
}

bool FileSystem::CopyFile(const string& sourcePath, const string& destPath, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(destPath)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.CopyFile(sourcePath, destPath);
   }

   if (IsSAFUri(sourcePath) && !IsSAFUri(destPath)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.CopyDirectory(sourcePath, destPath);
   }
#endif

   return s_filesystemProvider.CopyFile(sourcePath, destPath);
}

bool FileSystem::CopyDirectory(const string& sourcePath, const string& destPath, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(sourcePath) || IsSAFUri(destPath)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.CopyDirectory(sourcePath, destPath);
   }
#endif

   return s_filesystemProvider.CopyDirectory(sourcePath, destPath);
}

string FileSystem::JoinPath(const string& basePath, const string& relativePath)
{
#ifdef __ANDROID__
   if (IsSAFUri(basePath))
      return s_safProvider.JoinPath(basePath, relativePath);
#endif
   return s_filesystemProvider.JoinPath(basePath, relativePath);
}

bool FileSystem::Delete(const string& path, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.Delete(path);
   }
#endif
   return s_filesystemProvider.Delete(path);
}

bool FileSystem::IsDirectory(const string& path, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.IsDirectory(path);
   }
#endif
   return s_filesystemProvider.IsDirectory(path);
}

vector<string> FileSystem::ListDirectory(const string& path, const string& tablesPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      s_safProvider.SetTablesPath(tablesPath);
      return s_safProvider.ListDirectory(path);
   }
#endif
   return s_filesystemProvider.ListDirectory(path);
}

bool FileSystem::ExtractZipToDirectory(const string& zipFile, const string& destDir)
{
   PLOGI.printf("ExtractZipToDirectory: extracting %s to %s", zipFile.c_str(), destDir.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(zipFile.c_str(), ZIP_RDONLY, &error);
   if (!zip_archive) {
      PLOGE.printf("ExtractZipToDirectory: Failed to open zip file: %s (error: %d)", zipFile.c_str(), error);
      return false;
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

      if (filename.back() == '/')
         std::filesystem::create_directories(out);
      else {
         std::filesystem::create_directories(out.parent_path());
         zip_file_t* zip_file = zip_fopen_index(zip_archive, i, 0);
         if (!zip_file) {
            PLOGE.printf("ExtractZipToDirectory: Failed to open file in archive: %s", filename.c_str());
            zip_close(zip_archive);
            return false;
         }
         std::ofstream ofs(out, std::ios::binary);
         char buf[4096];
         zip_int64_t len;
         while ((len = zip_fread(zip_file, buf, sizeof(buf))) > 0)
            ofs.write(buf, len);
         zip_fclose(zip_file);

         PLOGI.printf("ExtractZipToDirectory: Extracted file: %s", out.string().c_str());
      }

      using json = nlohmann::json;
      json progressJson;
      progressJson["progress"] = int((i * 100) / file_count);
      string progressStr = progressJson.dump();
   }

   zip_close(zip_archive);
   PLOGI.printf("ExtractZipToDirectory: Extraction completed successfully");
   return true;
}

bool FileSystem::CompressDirectory(const string& sourcePath, const string& destinationPath)
{
   PLOGI.printf("Compressing %s to %s", sourcePath.c_str(), destinationPath.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(destinationPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
   if (!zip_archive) {
      PLOGE.printf("Failed to create archive: %s (error: %d)", destinationPath.c_str(), error);
      return false;
   }

   std::filesystem::path base(sourcePath);
   size_t base_len = base.string().length();

   vector<std::filesystem::path> items;
   try {
      for (auto& item : std::filesystem::recursive_directory_iterator(base))
         items.push_back(item.path());
   }
   catch (const std::filesystem::filesystem_error& ex) {
      PLOGE.printf("Error scanning directory: %s", ex.what());
      zip_close(zip_archive);
      return false;
   }

   size_t total = items.size();
   size_t done = 0;

   for (auto& item : items) {
      string rel = item.string().substr(base_len + 1);
      if (std::filesystem::is_directory(item))
         zip_dir_add(zip_archive, (rel + '/').c_str(), ZIP_FL_ENC_UTF_8);
      else {
         zip_source_t* zip_source = zip_source_file(zip_archive, item.string().c_str(), 0, 0);
         if (!zip_source) {
            PLOGE.printf("Failed to create source for file: %s", item.string().c_str());
            zip_close(zip_archive);
            return false;
         }
         if (zip_file_add(zip_archive, rel.c_str(), zip_source, ZIP_FL_ENC_UTF_8) < 0) {
            PLOGE.printf("Failed to add file to archive: %s", rel.c_str());
            zip_source_free(zip_source);
            zip_close(zip_archive);
            return false;
         }
      }

      ++done;
      int progress = int((done * 100) / total);
      if (done % 10 == 0) {
         PLOGI.printf("Progress: %d%% (%zu/%zu files)", progress, done, total);
      }
   }

   zip_close(zip_archive);
   PLOGI.printf("Successfully compressed %zu files", total);
   return true;
}

bool FileSystem::UncompressArchive(const string& archivePath)
{
   PLOGI.printf("Extracting %s", archivePath.c_str());

   int error = 0;
   zip_t* zip_archive = zip_open(archivePath.c_str(), ZIP_RDONLY, &error);
   if (!zip_archive) {
      PLOGE.printf("Failed to open archive: %s (error: %d)", archivePath.c_str(), error);
      return false;
   }

   zip_int64_t file_count = zip_get_num_entries(zip_archive, 0);
   PLOGI.printf("Found %lld files in archive", file_count);

   for (zip_uint64_t i = 0; i < (zip_uint64_t)file_count; ++i) {
      zip_stat_t st;
      if (zip_stat_index(zip_archive, i, ZIP_STAT_NAME, &st) != 0)
         continue;

      string filename = st.name;
      if (filename.starts_with("__MACOSX") || filename.starts_with(".DS_Store"))
         continue;

      std::filesystem::path out = std::filesystem::path(archivePath).parent_path() / filename;

      if (filename.back() == '/')
         std::filesystem::create_directories(out);
      else {
         std::filesystem::create_directories(out.parent_path());
         zip_file_t* zip_file = zip_fopen_index(zip_archive, i, 0);
         if (!zip_file) {
            PLOGE.printf("Failed to open file in archive: %s", filename.c_str());
            zip_close(zip_archive);
            return false;
         }
         std::ofstream ofs(out, std::ios::binary);
         char buf[4096];
         zip_int64_t len;
         while ((len = zip_fread(zip_file, buf, sizeof(buf))) > 0)
            ofs.write(buf, len);
         zip_fclose(zip_file);

         PLOGI.printf("Extracted file: %s", out.string().c_str());
      }

      int progress = int((i * 100) / file_count);
      if (i % 10 == 0) {
         PLOGI.printf("Progress: %d%% (%llu/%lld files)", progress, i, file_count);
      }
   }

   zip_close(zip_archive);
   PLOGI.printf("Successfully extracted archive");
   return true;
}

string FileSystem::GetFileName(const string& path)
{
   if (path.empty())
      return "";

   size_t lastSep = path.find_last_of(PATH_SEPARATOR_CHAR);
   if (lastSep == string::npos)
      return path;

   return path.substr(lastSep + 1);
}

string FileSystem::GetParentPath(const string& path)
{
   if (path.empty())
      return "";

   size_t lastSep = path.find_last_of(PATH_SEPARATOR_CHAR);
   if (lastSep == string::npos)
      return "";

   return path.substr(0, lastSep);
}

string FileSystem::GetStem(const string& path)
{
   string fileName = GetFileName(path);
   if (fileName.empty())
      return "";

   size_t lastDot = fileName.find_last_of('.');
   if (lastDot == string::npos)
      return fileName;

   return fileName.substr(0, lastDot);
}

string FileSystem::GetExtension(const string& path)
{
   string fileName = GetFileName(path);
   if (fileName.empty())
      return "";

   size_t lastDot = fileName.find_last_of('.');
   if (lastDot == string::npos)
      return "";

   return fileName.substr(lastDot);
}

string FileSystem::GetRelativePath(const string& fullPath, const string& basePath)
{
   if (fullPath.empty() || basePath.empty())
      return fullPath;

   if (!fullPath.starts_with(basePath))
      return fullPath;

   size_t baseLen = basePath.length();
   if (fullPath.length() <= baseLen)
      return "";

   size_t startPos = baseLen;
   if (fullPath[startPos] == PATH_SEPARATOR_CHAR)
      startPos++;

   return fullPath.substr(startPos);
}

string FileSystem::GetTempDirectoryPath()
{
   return std::filesystem::temp_directory_path().string();
}

bool FileSystem::RemoveAll(const string& path)
{
   try {
      std::filesystem::remove_all(path);
      return true;
   }
   catch (const std::exception& e) {
      PLOGE.printf("Failed to remove directory: %s, error: %s", path.c_str(), e.what());
      return false;
   }
}

vector<string> FileSystem::ListFilesRecursiveLocal(const string& path, const string& extension)
{
   vector<string> files;

   try {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
         if (entry.is_regular_file()) {
            string filePath = entry.path().string();

            if (extension.empty() || GetExtension(filePath) == extension) {
               files.push_back(filePath);
            }
         }
      }
   }
   catch (const std::exception& e) {
      PLOGE.printf("Error listing files in %s: %s", path.c_str(), e.what());
   }

   return files;
}

}
