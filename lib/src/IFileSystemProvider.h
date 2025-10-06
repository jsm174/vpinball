#pragma once

#include <string>
#include <vector>

namespace VPinballLib {

class IFileSystemProvider
{
public:
   virtual ~IFileSystemProvider() = default;

   virtual bool WriteFile(const string& path, const string& content) = 0;
   virtual string ReadFile(const string& path) = 0;
   virtual bool Exists(const string& path) = 0;
   virtual vector<string> ListFilesRecursive(const string& path, const string& extension) = 0;
   virtual bool CreateDirectories(const string& path) = 0;
   virtual bool CopyFile(const string& sourcePath, const string& destPath) = 0;
   virtual bool CopyDirectory(const string& sourcePath, const string& destPath) = 0;
   virtual string JoinPath(const string& basePath, const string& relativePath) = 0;
   virtual bool Delete(const string& path) = 0;
   virtual bool IsDirectory(const string& path) = 0;
   virtual vector<string> ListDirectory(const string& path) = 0;
};

}
