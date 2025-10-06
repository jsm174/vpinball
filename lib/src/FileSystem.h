#pragma once

#include <string>
#include <vector>

namespace VPinballLib {

class FileSystem
{
public:
   static void Init();
   static bool IsSAFUri(const string& path);
   static bool WriteFile(const string& path, const string& content, const string& tablesPath);
   static string ReadFile(const string& path, const string& tablesPath);
   static bool Exists(const string& path);
   static vector<string> ListFilesRecursive(const string& path, const string& extension, const string& tablesPath);
   static bool CreateDirectories(const string& path);
   static bool CopyFile(const string& sourcePath, const string& destPath, const string& tablesPath);
   static bool CopyDirectory(const string& sourcePath, const string& destPath, const string& tablesPath);
   static string JoinPath(const string& basePath, const string& relativePath);
   static bool Delete(const string& path, const string& tablesPath);
   static bool IsDirectory(const string& path, const string& tablesPath);
   static vector<string> ListDirectory(const string& path, const string& tablesPath);
   static string ExtractRelativePath(const string& fullPath, const string& tablesPath);
   static bool ExtractZipToDirectory(const string& zipFile, const string& destDir);
   static bool CompressDirectory(const string& sourcePath, const string& destinationPath);
   static bool UncompressArchive(const string& archivePath);

   static string GetFileName(const string& path);
   static string GetParentPath(const string& path);
   static string GetStem(const string& path);
   static string GetExtension(const string& path);
   static string GetRelativePath(const string& fullPath, const string& basePath);

   static string GetTempDirectoryPath();
   static bool RemoveAll(const string& path);
   static vector<string> ListFilesRecursiveLocal(const string& path, const string& extension);
};

}
