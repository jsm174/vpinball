// license:GPLv3+

#pragma once

#include <string>
#include <functional>

struct ZipResult {
   bool success;
   std::string error;
};

class ZipUtils {
public:
   typedef std::function<void(int current, int total, const char* filename)> ProgressCallback;

   static ZipResult Zip(const char* sourcePath, const char* destPath, ProgressCallback callback = nullptr);
   static ZipResult Unzip(const char* sourcePath, const char* destPath, ProgressCallback callback = nullptr);
};
