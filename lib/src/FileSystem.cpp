#include "core/stdafx.h"

#include "FileSystem.h"
#include "FileSystemProvider.h"
#ifdef __ANDROID__
#include "SAFFileSystemProvider.h"
#endif

namespace VPinballLib {

string FileSystem::s_tablesPath;

static FileSystemProvider s_filesystemProvider;
#ifdef __ANDROID__
static SAFFileSystemProvider s_safProvider;
#endif

void FileSystem::SetTablesPath(const string& tablesPath)
{
   s_tablesPath = tablesPath;
   PLOGI.printf("Set to '%s'", tablesPath.c_str());

#ifdef __ANDROID__
   SAFFileSystemProvider::SetTablesPath(tablesPath);
#endif
}

bool FileSystem::IsSAFUri(const string& path)
{
   return path.starts_with("content://");
}

string FileSystem::ExtractRelativePath(const string& fullPath)
{
   try {
      if (!IsSAFUri(fullPath)) {
         return fullPath;
      }

      if (s_tablesPath.empty()) {
         PLOGE.printf("TablesPath not set, cannot extract relative path");
         return fullPath;
      }

      if (fullPath.starts_with(s_tablesPath)) {
         string relativePath = fullPath.substr(s_tablesPath.length());
         if (!relativePath.empty() && relativePath[0] == '/') {
            relativePath = relativePath.substr(1);
         }
         PLOGD.printf("Extracted '%s' from '%s'", relativePath.c_str(), fullPath.c_str());
         return relativePath;
      }

      PLOGE.printf("Path '%s' doesn't start with TablesPath '%s'", fullPath.c_str(), s_tablesPath.c_str());
      return fullPath;
   } catch (const std::exception& e) {
      PLOGE.printf("Exception: %s", e.what());
      return fullPath;
   }
}

bool FileSystem::WriteFile(const string& path, const string& content)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.WriteFile(path, content);
   }
#endif
   return s_filesystemProvider.WriteFile(path, content);
}

string FileSystem::ReadFile(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.ReadFile(path);
   }
#endif
   return s_filesystemProvider.ReadFile(path);
}

bool FileSystem::Exists(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.Exists(path);
   }
#endif
   return s_filesystemProvider.Exists(path);
}

vector<string> FileSystem::ListFilesRecursive(const string& path, const string& extension)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.ListFilesRecursive(path, extension);
   }
#endif
   return s_filesystemProvider.ListFilesRecursive(path, extension);
}

bool FileSystem::CreateDirectories(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.CreateDirectories(path);
   }
#endif
   return s_filesystemProvider.CreateDirectories(path);
}

bool FileSystem::CopyFile(const string& sourcePath, const string& destPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(destPath)) {
      return s_safProvider.CopyFile(sourcePath, destPath);
   }

   if (IsSAFUri(sourcePath) && !IsSAFUri(destPath)) {
      return s_safProvider.CopyDirectory(sourcePath, destPath);
   }
#endif

   return s_filesystemProvider.CopyFile(sourcePath, destPath);
}

bool FileSystem::CopyDirectory(const string& sourcePath, const string& destPath)
{
#ifdef __ANDROID__
   if (IsSAFUri(sourcePath) || IsSAFUri(destPath)) {
      return s_safProvider.CopyDirectory(sourcePath, destPath);
   }
#endif

   return s_filesystemProvider.CopyDirectory(sourcePath, destPath);
}

string FileSystem::JoinPath(const string& basePath, const string& relativePath)
{
#ifdef __ANDROID__
   if (IsSAFUri(basePath)) {
      return s_safProvider.JoinPath(basePath, relativePath);
   }
#endif
   return s_filesystemProvider.JoinPath(basePath, relativePath);
}

bool FileSystem::Delete(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.Delete(path);
   }
#endif
   return s_filesystemProvider.Delete(path);
}

bool FileSystem::IsDirectory(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.IsDirectory(path);
   }
#endif
   return s_filesystemProvider.IsDirectory(path);
}

vector<string> FileSystem::ListDirectory(const string& path)
{
#ifdef __ANDROID__
   if (IsSAFUri(path)) {
      return s_safProvider.ListDirectory(path);
   }
#endif
   return s_filesystemProvider.ListDirectory(path);
}

}
