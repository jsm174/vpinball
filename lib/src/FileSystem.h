#pragma once

#include <string>
#include <vector>

namespace VPinballLib {

class FileSystem
{
public:
   static bool WriteFile(const string& path, const string& content);
   static string ReadFile(const string& path);
   static bool Exists(const string& path);
   static vector<string> ListFilesRecursive(const string& path, const string& extension);
   static bool CreateDirectories(const string& path);
   static bool CopyFile(const string& sourcePath, const string& destPath);
   static bool CopyDirectory(const string& sourcePath, const string& destPath);
   static string JoinPath(const string& basePath, const string& relativePath);
   static bool Delete(const string& path);
   static bool IsDirectory(const string& path);
   static vector<string> ListDirectory(const string& path);
   static void SetTablesPath(const string& tablesPath);
   static bool IsSAFUri(const string& path);
   static string ExtractRelativePath(const string& fullPath);

private:
   static string s_tablesPath;
};

}
