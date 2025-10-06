#include "core/stdafx.h"
#include "SAFFileSystemProvider.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_system.h>
#include <jni.h>

using json = nlohmann::json;

namespace VPinballLib {

struct SAFJNICache {
   jclass cls;
   struct {
      jmethodID writeFile;
      jmethodID readFile;
      jmethodID exists;
      jmethodID listFiles;
      jmethodID copyFile;
      jmethodID copyDirectory;
      jmethodID copySAFToFilesystem;
      jmethodID deleteFile;
      jmethodID isDirectory;
      jmethodID listDirectory;
   } methods;
};

static SAFJNICache g_safJNICache = {nullptr, {nullptr}};

void SAFFileSystemProvider::Init()
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env)
      return;

   if (g_safJNICache.cls)
      return;

   jclass localClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!localClass)
      return;

   g_safJNICache.cls = (jclass)env->NewGlobalRef(localClass);
   env->DeleteLocalRef(localClass);

   g_safJNICache.methods.writeFile = env->GetStaticMethodID(g_safJNICache.cls, "SAFWriteFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
   g_safJNICache.methods.readFile = env->GetStaticMethodID(g_safJNICache.cls, "SAFReadFile", "(Ljava/lang/String;)Ljava/lang/String;");
   g_safJNICache.methods.exists = env->GetStaticMethodID(g_safJNICache.cls, "SAFExists", "(Ljava/lang/String;)Z");
   g_safJNICache.methods.listFiles = env->GetStaticMethodID(g_safJNICache.cls, "SAFListFiles", "(Ljava/lang/String;)Ljava/lang/String;");
   g_safJNICache.methods.copyFile = env->GetStaticMethodID(g_safJNICache.cls, "SAFCopyFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
   g_safJNICache.methods.copyDirectory = env->GetStaticMethodID(g_safJNICache.cls, "SAFCopyDirectory", "(Ljava/lang/String;Ljava/lang/String;)Z");
   g_safJNICache.methods.copySAFToFilesystem = env->GetStaticMethodID(g_safJNICache.cls, "SAFCopySAFToFilesystem", "(Ljava/lang/String;Ljava/lang/String;)Z");
   g_safJNICache.methods.deleteFile = env->GetStaticMethodID(g_safJNICache.cls, "SAFDelete", "(Ljava/lang/String;)Z");
   g_safJNICache.methods.isDirectory = env->GetStaticMethodID(g_safJNICache.cls, "SAFIsDirectory", "(Ljava/lang/String;)Z");
   g_safJNICache.methods.listDirectory = env->GetStaticMethodID(g_safJNICache.cls, "SAFListDirectory", "(Ljava/lang/String;)Ljava/lang/String;");

   PLOGI.printf("Initialized successfully");
}

void SAFFileSystemProvider::SetTablesPath(const string& tablesPath)
{
   m_tablesPath = tablesPath;
   PLOGD.printf("Set to '%s'", tablesPath.c_str());
}

string SAFFileSystemProvider::ExtractRelativePath(const string& fullPath)
{
   try {
      if (m_tablesPath.empty()) {
         PLOGE.printf("TablesPath not set, cannot extract relative path");
         return fullPath;
      }

      if (fullPath.starts_with(m_tablesPath)) {
         string relativePath = fullPath.substr(m_tablesPath.length());
         if (!relativePath.empty() && relativePath[0] == '/') {
            relativePath = relativePath.substr(1);
         }
         PLOGD.printf("Extracted '%s' from '%s'", relativePath.c_str(), fullPath.c_str());
         return relativePath;
      }

      PLOGE.printf("Path '%s' doesn't start with TablesPath '%s'", fullPath.c_str(), m_tablesPath.c_str());
      return fullPath;
   }
   catch (const std::exception& e) {
      PLOGE.printf("Exception: %s", e.what());
      return fullPath;
   }
}

vector<string> SAFFileSystemProvider::ParseJsonArray(const char* jsonStr)
{
   vector<string> result;

   if (!jsonStr || jsonStr[0] == '\0')
      return result;

   try {
      json j = json::parse(jsonStr);
      if (j.is_array()) {
         for (const auto& item : j) {
            if (item.is_string()) {
               result.push_back(item.get<string>());
            }
         }
      }
   } catch (const std::exception& e) {
      PLOGE.printf("Failed to parse JSON: %s", e.what());
   }

   return result;
}

bool SAFFileSystemProvider::WriteFile(const string& path, const string& content)
{
   string relativePath = ExtractRelativePath(path);
   PLOGD.printf("Using SAF for: %s", relativePath.c_str());

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath.c_str());
   jstring jContent = env->NewStringUTF(content.c_str());

   jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.writeFile, jPath, jContent);

   env->DeleteLocalRef(jPath);
   env->DeleteLocalRef(jContent);

   PLOGD.printf("SAF write result: %d", result);
   return (bool)result;
}

string SAFFileSystemProvider::ReadFile(const string& path)
{
   string relativePath = ExtractRelativePath(path);
   PLOGD.printf("Using SAF for: %s", relativePath.c_str());

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return "";
   }

   jstring jPath = env->NewStringUTF(relativePath.c_str());
   jstring jResult = (jstring)env->CallStaticObjectMethod(g_safJNICache.cls, g_safJNICache.methods.readFile, jPath);

   string content;
   if (jResult) {
      const char* cStr = env->GetStringUTFChars(jResult, nullptr);
      content = string(cStr);
      env->ReleaseStringUTFChars(jResult, cStr);
      env->DeleteLocalRef(jResult);
      PLOGD.printf("SAF read %zu bytes", content.length());
   }
   else {
      PLOGE.printf("SAF read returned null");
   }

   env->DeleteLocalRef(jPath);

   return content;
}

bool SAFFileSystemProvider::Exists(const string& path)
{
   string relativePath = ExtractRelativePath(path);

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath.c_str());
   jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.exists, jPath);

   env->DeleteLocalRef(jPath);

   PLOGD.printf("SAF check for '%s': %d", relativePath.c_str(), result);
   return (bool)result;
}

vector<string> SAFFileSystemProvider::ListFilesRecursive(const string& path, const string& extension)
{
   PLOGD.printf("Using SAF for extension: %s", extension.c_str());

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return {};
   }

   jstring jExtension = env->NewStringUTF(extension.c_str());
   jstring jResult = (jstring)env->CallStaticObjectMethod(g_safJNICache.cls, g_safJNICache.methods.listFiles, jExtension);

   vector<string> files;
   if (jResult) {
      const char* cStr = env->GetStringUTFChars(jResult, nullptr);
      files = ParseJsonArray(cStr);
      env->ReleaseStringUTFChars(jResult, cStr);
      env->DeleteLocalRef(jResult);
      PLOGD.printf("SAF returned %zu files", files.size());

      for (auto& file : files) {
         file = m_tablesPath + "/" + file;
      }
   }
   else {
      PLOGE.printf("SAF returned null");
   }

   env->DeleteLocalRef(jExtension);

   return files;
}

bool SAFFileSystemProvider::CreateDirectories(const string& path)
{
   PLOGD.printf("SAF path, will auto-create: %s", path.c_str());
   return true;
}

bool SAFFileSystemProvider::CopyFile(const string& sourcePath, const string& destPath)
{
   PLOGD.printf("Copying '%s' to '%s'", sourcePath.c_str(), destPath.c_str());

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return false;
   }

   bool sourceIsSAF = sourcePath.starts_with("content://");
   bool destIsSAF = destPath.starts_with("content://");

   if (!sourceIsSAF && destIsSAF) {
      string destRelativePath = ExtractRelativePath(destPath);
      PLOGD.printf("filesystem->SAF, dest relative path: %s", destRelativePath.c_str());

      jstring jSource = env->NewStringUTF(sourcePath.c_str());
      jstring jDest = env->NewStringUTF(destRelativePath.c_str());
      jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.copyDirectory, jSource, jDest);

      env->DeleteLocalRef(jSource);
      env->DeleteLocalRef(jDest);

      return (bool)result;
   }
   else if (sourceIsSAF && destIsSAF) {
      string sourceRelativePath = ExtractRelativePath(sourcePath);
      string destRelativePath = ExtractRelativePath(destPath);
      PLOGD.printf("SAF->SAF, source: %s, dest: %s", sourceRelativePath.c_str(), destRelativePath.c_str());

      jstring jSource = env->NewStringUTF(sourceRelativePath.c_str());
      jstring jDest = env->NewStringUTF(destRelativePath.c_str());
      jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.copyFile, jSource, jDest);

      env->DeleteLocalRef(jSource);
      env->DeleteLocalRef(jDest);

      return (bool)result;
   }
   else {
      PLOGE.printf("Unsupported copy operation (SAF->filesystem not supported)");
      return false;
   }
}

bool SAFFileSystemProvider::CopyDirectory(const string& sourcePath, const string& destPath)
{
   PLOGD.printf("Copying '%s' to '%s'", sourcePath.c_str(), destPath.c_str());

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return false;
   }

   bool sourceIsSAF = sourcePath.starts_with("content://");
   bool destIsSAF = destPath.starts_with("content://");

   if (sourceIsSAF && !destIsSAF) {
      string relativePath = ExtractRelativePath(sourcePath);
      PLOGD.printf("SAF->filesystem, using relative path: %s", relativePath.c_str());

      jstring jSource = env->NewStringUTF(relativePath.c_str());
      jstring jDest = env->NewStringUTF(destPath.c_str());
      jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.copySAFToFilesystem, jSource, jDest);

      env->DeleteLocalRef(jSource);
      env->DeleteLocalRef(jDest);

      return (bool)result;
   }
   else if (!sourceIsSAF && destIsSAF) {
      string destRelativePath = ExtractRelativePath(destPath);
      PLOGD.printf("filesystem->SAF, dest relative path: %s", destRelativePath.c_str());

      jstring jSource = env->NewStringUTF(sourcePath.c_str());
      jstring jDest = env->NewStringUTF(destRelativePath.c_str());
      jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.copyDirectory, jSource, jDest);

      env->DeleteLocalRef(jSource);
      env->DeleteLocalRef(jDest);

      return (bool)result;
   }
   else {
      PLOGE.printf("Unsupported copy operation (both SAF or both filesystem)");
      return false;
   }
}

string SAFFileSystemProvider::JoinPath(const string& basePath, const string& relativePath)
{
   return basePath + "/" + relativePath;
}

bool SAFFileSystemProvider::Delete(const string& path)
{
   string relativePath = ExtractRelativePath(path);
   PLOGD.printf("Deleting: %s", relativePath.c_str());

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath.c_str());
   jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.deleteFile, jPath);

   env->DeleteLocalRef(jPath);

   PLOGD.printf("SAF delete result: %d", result);
   return (bool)result;
}

bool SAFFileSystemProvider::IsDirectory(const string& path)
{
   string relativePath = ExtractRelativePath(path);

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath.c_str());
   jboolean result = env->CallStaticBooleanMethod(g_safJNICache.cls, g_safJNICache.methods.isDirectory, jPath);

   env->DeleteLocalRef(jPath);

   PLOGD.printf("IsDirectory: SAF check for '%s': %d", relativePath.c_str(), result);
   return (bool)result;
}

vector<string> SAFFileSystemProvider::ListDirectory(const string& path)
{
   string relativePath = ExtractRelativePath(path);
   PLOGD.printf("Using SAF for: %s", relativePath.c_str());

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("Failed to get JNI environment");
      return {};
   }

   jstring jPath = env->NewStringUTF(relativePath.c_str());
   jstring jResult = (jstring)env->CallStaticObjectMethod(g_safJNICache.cls, g_safJNICache.methods.listDirectory, jPath);

   vector<string> entries;
   if (jResult) {
      const char* cStr = env->GetStringUTFChars(jResult, nullptr);
      entries = ParseJsonArray(cStr);
      env->ReleaseStringUTFChars(jResult, cStr);
      env->DeleteLocalRef(jResult);
      PLOGD.printf("SAF returned %zu entries", entries.size());

      for (auto& entry : entries) {
         entry = m_tablesPath + "/" + entry;
      }
   }
   else {
      PLOGE.printf("SAF returned null");
   }

   env->DeleteLocalRef(jPath);

   return entries;
}

}
