#pragma once

#include <string>
#include <vector>

namespace VPinballLib {

// Platform-agnostic file system abstraction
// Handles both normal file paths and special URIs (e.g., "saf://" on Android)
class FileSystem {
public:
   // Write content to file
   // Returns true on success, false on failure
   static bool WriteFile(const std::string& path, const std::string& content);

   // Read entire file as string
   // Returns empty string on failure
   static std::string ReadFile(const std::string& path);

   // Check if file or directory exists
   static bool Exists(const std::string& path);

   // List all files with given extension recursively
   // Returns vector of full paths (relative to root for special URIs)
   static std::vector<std::string> ListFilesRecursive(const std::string& path, const std::string& extension);

   // Create directories (including parent directories)
   // Returns true on success or if already exists
   static bool CreateDirectories(const std::string& path);

   // Copy file from source to destination
   // Returns true on success, false on failure
   static bool CopyFile(const std::string& sourcePath, const std::string& destPath);

   // Copy directory recursively from source to destination
   // Returns true on success, false on failure
   static bool CopyDirectory(const std::string& sourcePath, const std::string& destPath);

   // Join path components (handles both normal paths and special URIs like "saf://")
   static std::string JoinPath(const std::string& basePath, const std::string& relativePath);

   // Delete file or directory
   // Returns true on success, false on failure
   static bool Delete(const std::string& path);

   // Check if path is a directory
   // Returns true if directory, false if file or doesn't exist
   static bool IsDirectory(const std::string& path);

   // List immediate children in a directory (non-recursive)
   // Returns vector of full paths
   static std::vector<std::string> ListDirectory(const std::string& path);

   // Set the TablesPath (used for extracting relative paths from content:// URIs)
   static void SetTablesPath(const std::string& tablesPath);

   // Helper to check if path is a special URI (e.g., "content://")
   static bool IsSpecialUri(const std::string& path);

   // Extract relative path from full content:// URI
   // For content:// URIs: removes tree URI base to get relative path
   // For normal paths: returns as-is
   static std::string ExtractRelativePath(const std::string& fullPath);

private:

   // Parse JSON array of strings (for ListFilesRecursive on special URIs)
   static std::vector<std::string> ParseJsonArray(const char* json);

   static std::string s_tablesPath;
};

}
