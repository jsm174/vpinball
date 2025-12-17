// license:GPLv3+

#include "core/stdafx.h"
#include "ZipUtils.h"

#include <zip.h>
#include <filesystem>
#include <fstream>

static bool IsExcludedPath(const std::string& path)
{
   return path.rfind("__MACOSX", 0) == 0 || path.find("/__MACOSX") != std::string::npos;
}

static int CountEntriesInDirectory(const std::string& dirPath)
{
   int count = 0;
   for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath)) {
      std::string relativePath = std::filesystem::relative(entry.path(), dirPath).string();
      if (!IsExcludedPath(relativePath))
         count++;
   }
   return count;
}

ZipResult ZipUtils::Zip(const char* sourcePath, const char* destPath, ProgressCallback callback)
{
   ZipResult result = { false, "" };

   if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
      result.error = "Source directory does not exist";
      return result;
   }

   int error = 0;
   zip_t* archive = zip_open(destPath, ZIP_CREATE | ZIP_TRUNCATE, &error);
   if (!archive) {
      result.error = "Failed to create zip archive";
      return result;
   }

   int totalEntries = CountEntriesInDirectory(sourcePath);
   int currentEntry = 0;

   for (const auto& entry : std::filesystem::recursive_directory_iterator(sourcePath)) {
      std::string relativePath = std::filesystem::relative(entry.path(), sourcePath).string();

      if (IsExcludedPath(relativePath))
         continue;

      if (entry.is_directory()) {
         std::string dirPath = relativePath + "/";
         zip_dir_add(archive, dirPath.c_str(), ZIP_FL_ENC_UTF_8);
      }
      else if (entry.is_regular_file()) {
         zip_source_t* fileSource = zip_source_file(archive, entry.path().string().c_str(), 0, -1);
         if (fileSource) {
            zip_int64_t index = zip_file_add(archive, relativePath.c_str(), fileSource, ZIP_FL_ENC_UTF_8);
            if (index < 0) {
               zip_source_free(fileSource);
               PLOGE.printf("Failed to add file to zip: %s", relativePath.c_str());
            }
         }
      }

      currentEntry++;
      if (callback)
         callback(currentEntry, totalEntries, relativePath.c_str());
   }

   if (zip_close(archive) < 0) {
      result.error = "Failed to finalize zip archive";
      return result;
   }

   result.success = true;
   return result;
}

ZipResult ZipUtils::Unzip(const char* sourcePath, const char* destPath, ProgressCallback callback)
{
   ZipResult result = { false, "" };

   int error = 0;
   zip_t* archive = zip_open(sourcePath, ZIP_RDONLY, &error);
   if (!archive) {
      result.error = "Unable to open zip file";
      PLOGE.printf("Unable to unzip file: source=%s", sourcePath);
      return result;
   }

   zip_int64_t totalEntries = zip_get_num_entries(archive, 0);
   std::string destDir = destPath;

   for (zip_uint64_t i = 0; i < (zip_uint64_t)totalEntries; ++i) {
      zip_stat_t fileStat;
      if (zip_stat_index(archive, i, ZIP_STAT_NAME, &fileStat) != 0)
         continue;

      std::string filename = fileStat.name;

      if (IsExcludedPath(filename))
         continue;

      std::filesystem::path destFilePath = std::filesystem::path(destDir) / filename;

      if (filename.back() == '/') {
         std::filesystem::create_directories(destFilePath);
      }
      else {
         std::filesystem::create_directories(destFilePath.parent_path());

         zip_file_t* zipFile = zip_fopen_index(archive, i, 0);
         if (!zipFile) {
            PLOGE.printf("Unable to extract file: %s", destFilePath.string().c_str());
            continue;
         }

         std::ofstream ofs(destFilePath, std::ios::binary);
         char buf[4096];
         zip_int64_t len;
         while ((len = zip_fread(zipFile, buf, sizeof(buf))) > 0)
            ofs.write(buf, len);
         zip_fclose(zipFile);
      }

      if (callback)
         callback((int)(i + 1), (int)totalEntries, filename.c_str());
   }

   zip_close(archive);
   result.success = true;
   return result;
}
