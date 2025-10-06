#pragma once

#include "IFileSystemProvider.h"

namespace VPinballLib {

class SAFFileSystemProvider : public IFileSystemProvider
{
public:
   static void Init();
   void SetTablesPath(const string& tablesPath);

   bool WriteFile(const string& path, const string& content) override;
   string ReadFile(const string& path) override;
   bool Exists(const string& path) override;
   vector<string> ListFilesRecursive(const string& path, const string& extension) override;
   bool CreateDirectories(const string& path) override;
   bool CopyFile(const string& sourcePath, const string& destPath) override;
   bool CopyDirectory(const string& sourcePath, const string& destPath) override;
   string JoinPath(const string& basePath, const string& relativePath) override;
   bool Delete(const string& path) override;
   bool IsDirectory(const string& path) override;
   vector<string> ListDirectory(const string& path) override;

private:
   string ExtractRelativePath(const string& fullPath);
   static vector<string> ParseJsonArray(const char* jsonStr);

   string m_tablesPath;
};

}
