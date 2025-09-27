#include "core/stdafx.h"

#include "../../../include/vpinball/VPinballLib_C.h"
#include "../../../src/VPinballLib.h"

#include <SDL3/SDL_system.h>
#include <jni.h>

using namespace VPinballLib;

static jobject gJNICallbackObject = nullptr;
static jmethodID gJNIOnEventMethod = nullptr;

void* VPinballJNI_OnEventCallback(VPINBALL_EVENT event, const char* jsonData, void* data)
{
   if (!gJNICallbackObject || !gJNIOnEventMethod)
      return nullptr;

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();

   // Just pass the JSON string, Kotlin will deserialize it
   jstring jsonDataString = jsonData ? env->NewStringUTF(jsonData) : nullptr;
   jobject result = env->CallObjectMethod(gJNICallbackObject, gJNIOnEventMethod, (jint)event, jsonDataString, nullptr);

   if (jsonDataString) {
      env->DeleteLocalRef(jsonDataString);
   }

   if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return nullptr;
   }

   void* nativeResult = nullptr;
   if (result) {
      nativeResult = env->GetDirectBufferAddress(result);
      env->DeleteLocalRef(result);
   }

   return nativeResult;
}

extern "C" {

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballGetVersionStringFull(JNIEnv* env, jobject obj)
{
   return env->NewStringUTF(VPinballGetVersionStringFull());
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballSetEventCallback(JNIEnv* env, jobject obj, jobject callback)
{
   if (!callback)
      return;

   gJNICallbackObject = env->NewGlobalRef(callback);
   gJNIOnEventMethod = env->GetMethodID(env->GetObjectClass(gJNICallbackObject), "onEvent", "(ILjava/lang/String;Ljava/lang/Object;)Ljava/lang/Object;");

   VPinballSetEventCallback(VPinballJNI_OnEventCallback);
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballLog(JNIEnv* env, jobject obj, jint level, jstring message)
{
   const char* pMessage = env->GetStringUTFChars(message, nullptr);
   VPinballLog(static_cast<VPINBALL_LOG_LEVEL>(level), pMessage);
   env->ReleaseStringUTFChars(message, pMessage);
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballResetLog(JNIEnv* env, jobject obj)
{
   VPinballResetLog();
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballLoadValueInt(JNIEnv* env, jobject obj, jstring sectionName, jstring key, jint defaultValue)
{
   const char* pSectionName = env->GetStringUTFChars(sectionName, nullptr);
   const char* pKey = env->GetStringUTFChars(key, nullptr);
   int result = VPinballLoadValueInt(pSectionName, pKey, defaultValue);
   env->ReleaseStringUTFChars(key, pKey);
   env->ReleaseStringUTFChars(sectionName, pSectionName);
   return result;
}

JNIEXPORT jfloat JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballLoadValueFloat(JNIEnv* env, jobject obj, jstring sectionName, jstring key, jfloat defaultValue)
{
   const char* pSectionName = env->GetStringUTFChars(sectionName, nullptr);
   const char* pKey = env->GetStringUTFChars(key, nullptr);
   float result = VPinballLoadValueFloat(pSectionName, pKey, defaultValue);
   env->ReleaseStringUTFChars(key, pKey);
   env->ReleaseStringUTFChars(sectionName, pSectionName);
   return result;
}

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballLoadValueString(JNIEnv* env, jobject obj, jstring sectionName, jstring key, jstring defaultValue)
{
   const char* pSectionName = env->GetStringUTFChars(sectionName, nullptr);
   const char* pKey = env->GetStringUTFChars(key, nullptr);
   const char* pDefaultValue = env->GetStringUTFChars(defaultValue, nullptr);
   const char* pResult = VPinballLoadValueString(pSectionName, pKey, pDefaultValue);
   env->ReleaseStringUTFChars(defaultValue, pDefaultValue);
   env->ReleaseStringUTFChars(key, pKey);
   env->ReleaseStringUTFChars(sectionName, pSectionName);
   return env->NewStringUTF(pResult);
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballSaveValueInt(JNIEnv* env, jobject obj, jstring sectionName, jstring key, jint value)
{
   const char* pSectionName = env->GetStringUTFChars(sectionName, nullptr);
   const char* pKey = env->GetStringUTFChars(key, nullptr);
   VPinballSaveValueInt(pSectionName, pKey, value);
   env->ReleaseStringUTFChars(key, pKey);
   env->ReleaseStringUTFChars(sectionName, pSectionName);
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballSaveValueFloat(JNIEnv* env, jobject obj, jstring sectionName, jstring key, jfloat value)
{
   const char* pSectionName = env->GetStringUTFChars(sectionName, nullptr);
   const char* pKey = env->GetStringUTFChars(key, nullptr);
   VPinballSaveValueFloat(pSectionName, pKey, value);
   env->ReleaseStringUTFChars(key, pKey);
   env->ReleaseStringUTFChars(sectionName, pSectionName);
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballSaveValueString(JNIEnv* env, jobject obj, jstring sectionName, jstring key, jstring value)
{
   const char* pSectionName = env->GetStringUTFChars(sectionName, nullptr);
   const char* pKey = env->GetStringUTFChars(key, nullptr);
   const char* pValue = env->GetStringUTFChars(value, nullptr);
   VPinballSaveValueString(pSectionName, pKey, pValue);
   env->ReleaseStringUTFChars(value, pValue);
   env->ReleaseStringUTFChars(key, pKey);
   env->ReleaseStringUTFChars(sectionName, pSectionName);
}

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballExportTable(JNIEnv* env, jobject obj, jstring uuid)
{
   const char* pUuid = env->GetStringUTFChars(uuid, nullptr);
   const char* exportPath = VPinballExportTable(pUuid);
   env->ReleaseStringUTFChars(uuid, pUuid);
   
   if (exportPath) {
      return env->NewStringUTF(exportPath);
   } else {
      return nullptr;
   }
}

// Removed - VPinballUpdateWebServer no longer in API (use VPinballStartWebServer/StopWebServer)
/*
JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballUpdateWebServer(JNIEnv* env, jobject obj)
{
   VPinballUpdateWebServer();
}
*/

// Removed - VPinballSetWebServerUpdated no longer in API
/*
JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballSetWebServerUpdated(JNIEnv* env, jobject obj)
{
   VPinballSetWebServerUpdated();
}
*/

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballResetIni(JNIEnv* env, jobject obj)
{
   return VPinballResetIni();
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballLoad(JNIEnv* env, jobject obj, jstring source)
{
   const char* pSource = env->GetStringUTFChars(source, nullptr);
   VPINBALL_STATUS status = VPinballLoad(pSource);
   env->ReleaseStringUTFChars(source, pSource);
   return status;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballExtractScript(JNIEnv* env, jobject obj)
{
   return VPinballExtractScript();
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballPlay(JNIEnv* env, jobject obj)
{
   return VPinballPlay();
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballStop(JNIEnv* env, jobject obj)
{
   return VPinballStop();
}

JNIEXPORT jboolean JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballFileExists(JNIEnv* env, jobject obj, jstring path)
{
   const char* pathChars = env->GetStringUTFChars(path, nullptr);
   bool exists = VPinballFileExists(pathChars);
   env->ReleaseStringUTFChars(path, pathChars);
   return exists;
}

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballPrepareFileForViewing(JNIEnv* env, jobject obj, jstring path)
{
   const char* pathChars = env->GetStringUTFChars(path, nullptr);
   const char* viewPath = VPinballPrepareFileForViewing(pathChars);
   env->ReleaseStringUTFChars(path, pathChars);

   if (viewPath && viewPath[0] != '\0')
      return env->NewStringUTF(viewPath);
   return nullptr;
}

// Removed - VPinballToggleFPS no longer in API
/*
JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballToggleFPS(JNIEnv* env, jobject obj)
{
   VPinballToggleFPS();
}
*/

// Removed - VPinballCaptureScreenshot no longer in API
/*
JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballCaptureScreenshot(JNIEnv* env, jobject obj, jstring filename)
{
   const char* pFilename = env->GetStringUTFChars(filename, nullptr);
   VPinballCaptureScreenshot(pFilename);
   env->ReleaseStringUTFChars(filename, pFilename);
}
*/

// Table Management JNI Implementations
JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballRefreshTables(JNIEnv* env, jobject obj)
{
   return VPinballRefreshTables();
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballReloadTablesPath(JNIEnv* env, jobject obj)
{
   return VPinballReloadTablesPath();
}

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballGetTables(JNIEnv* env, jobject obj)
{
   const char* result = VPinballGetTables();
   if (result)
      return env->NewStringUTF(result);
   return nullptr;
}


// Removed - VPinballAddVPXTable no longer in API
/*
JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballAddVPXTable(JNIEnv* env, jobject obj, jstring filePath)
{
   const char* pFilePath = env->GetStringUTFChars(filePath, nullptr);
   int result = VPinballAddVPXTable(pFilePath);
   env->ReleaseStringUTFChars(filePath, pFilePath);
   return result;
}
*/

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballDeleteTable(JNIEnv* env, jobject obj, jstring uuid)
{
   const char* pUuid = env->GetStringUTFChars(uuid, nullptr);
   int result = VPinballDeleteTable(pUuid);
   env->ReleaseStringUTFChars(uuid, pUuid);
   return result;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballRenameTable(JNIEnv* env, jobject obj, jstring uuid, jstring newName)
{
   const char* pUuid = env->GetStringUTFChars(uuid, nullptr);
   const char* pNewName = env->GetStringUTFChars(newName, nullptr);
   int result = VPinballRenameTable(pUuid, pNewName);
   env->ReleaseStringUTFChars(uuid, pUuid);
   env->ReleaseStringUTFChars(newName, pNewName);
   return result;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballImportTable(JNIEnv* env, jobject obj, jstring sourceFile)
{
   const char* pSourceFile = env->GetStringUTFChars(sourceFile, nullptr);
   int result = VPinballImportTable(pSourceFile);
   env->ReleaseStringUTFChars(sourceFile, pSourceFile);
   return result;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballSetTableArtwork(JNIEnv* env, jobject obj, jstring uuid, jstring artworkPath)
{
   const char* pUuid = env->GetStringUTFChars(uuid, nullptr);
   const char* pArtworkPath = env->GetStringUTFChars(artworkPath, nullptr);
   int result = VPinballSetTableArtwork(pUuid, pArtworkPath);
   env->ReleaseStringUTFChars(uuid, pUuid);
   env->ReleaseStringUTFChars(artworkPath, pArtworkPath);
   return result;
}

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballGetTablesPath(JNIEnv* env, jobject obj)
{
   const char* tablesPath = VPinballGetTablesPath();
   return env->NewStringUTF(tablesPath);
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballFreeString(JNIEnv* env, jobject obj, jstring jsonString)
{
   // Note: Java strings are handled by the JVM, this is just for API compatibility
   // The actual freeing happens when we call VPinballFreeString on the C char* before creating jstring
}


// ===== SAF (Storage Access Framework) JNI Bridge =====
// These functions are called FROM C++ FileSystem.cpp TO Java/Kotlin
// They call static methods in VPinballJNI.java

bool JNI_FileSystem_WriteFile(const char* relativePath, const char* content)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_WriteFile: Failed to get JNI environment");
      return false;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_WriteFile: Failed to find VPinballJNI class");
      return false;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFWriteFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_WriteFile: Failed to find SAFWriteFile method");
      env->DeleteLocalRef(jniClass);
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath);
   jstring jContent = env->NewStringUTF(content);

   jboolean result = env->CallStaticBooleanMethod(jniClass, method, jPath, jContent);

   env->DeleteLocalRef(jPath);
   env->DeleteLocalRef(jContent);
   env->DeleteLocalRef(jniClass);

   return (bool)result;
}

char* JNI_FileSystem_ReadFile(const char* relativePath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_ReadFile: Failed to get JNI environment");
      return nullptr;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_ReadFile: Failed to find VPinballJNI class");
      return nullptr;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFReadFile", "(Ljava/lang/String;)Ljava/lang/String;");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_ReadFile: Failed to find SAFReadFile method");
      env->DeleteLocalRef(jniClass);
      return nullptr;
   }

   jstring jPath = env->NewStringUTF(relativePath);
   jstring jResult = (jstring)env->CallStaticObjectMethod(jniClass, method, jPath);

   char* result = nullptr;
   if (jResult) {
      const char* cStr = env->GetStringUTFChars(jResult, nullptr);
      result = strdup(cStr); // Caller must free
      env->ReleaseStringUTFChars(jResult, cStr);
      env->DeleteLocalRef(jResult);
   }

   env->DeleteLocalRef(jPath);
   env->DeleteLocalRef(jniClass);

   return result;
}

bool JNI_FileSystem_Exists(const char* relativePath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_Exists: Failed to get JNI environment");
      return false;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_Exists: Failed to find VPinballJNI class");
      return false;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFExists", "(Ljava/lang/String;)Z");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_Exists: Failed to find SAFExists method");
      env->DeleteLocalRef(jniClass);
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath);
   jboolean result = env->CallStaticBooleanMethod(jniClass, method, jPath);

   env->DeleteLocalRef(jPath);
   env->DeleteLocalRef(jniClass);

   return (bool)result;
}

char* JNI_FileSystem_ListFiles(const char* extension)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_ListFiles: Failed to get JNI environment");
      return nullptr;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_ListFiles: Failed to find VPinballJNI class");
      return nullptr;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFListFiles", "(Ljava/lang/String;)Ljava/lang/String;");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_ListFiles: Failed to find SAFListFiles method");
      env->DeleteLocalRef(jniClass);
      return nullptr;
   }

   jstring jExtension = env->NewStringUTF(extension);
   jstring jResult = (jstring)env->CallStaticObjectMethod(jniClass, method, jExtension);

   char* result = nullptr;
   if (jResult) {
      const char* cStr = env->GetStringUTFChars(jResult, nullptr);
      result = strdup(cStr); // Caller must free
      env->ReleaseStringUTFChars(jResult, cStr);
      env->DeleteLocalRef(jResult);
   }

   env->DeleteLocalRef(jExtension);
   env->DeleteLocalRef(jniClass);

   return result;
}

bool JNI_FileSystem_CopyFile(const char* sourcePath, const char* destPath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_CopyFile: Failed to get JNI environment");
      return false;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_CopyFile: Failed to find VPinballJNI class");
      return false;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFCopyFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_CopyFile: Failed to find SAFCopyFile method");
      env->DeleteLocalRef(jniClass);
      return false;
   }

   jstring jSource = env->NewStringUTF(sourcePath);
   jstring jDest = env->NewStringUTF(destPath);
   jboolean result = env->CallStaticBooleanMethod(jniClass, method, jSource, jDest);

   env->DeleteLocalRef(jSource);
   env->DeleteLocalRef(jDest);
   env->DeleteLocalRef(jniClass);

   return (bool)result;
}

bool JNI_FileSystem_CopyDirectory(const char* sourcePath, const char* destPath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_CopyDirectory: Failed to get JNI environment");
      return false;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_CopyDirectory: Failed to find VPinballJNI class");
      return false;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFCopyDirectory", "(Ljava/lang/String;Ljava/lang/String;)Z");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_CopyDirectory: Failed to find SAFCopyDirectory method");
      env->DeleteLocalRef(jniClass);
      return false;
   }

   jstring jSource = env->NewStringUTF(sourcePath);
   jstring jDest = env->NewStringUTF(destPath);
   jboolean result = env->CallStaticBooleanMethod(jniClass, method, jSource, jDest);

   env->DeleteLocalRef(jSource);
   env->DeleteLocalRef(jDest);
   env->DeleteLocalRef(jniClass);

   return (bool)result;
}

bool JNI_FileSystem_CopySAFToFilesystem(const char* safRelativePath, const char* destPath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_CopySAFToFilesystem: Failed to get JNI environment");
      return false;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_CopySAFToFilesystem: Failed to find VPinballJNI class");
      return false;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFCopySAFToFilesystem", "(Ljava/lang/String;Ljava/lang/String;)Z");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_CopySAFToFilesystem: Failed to find SAFCopySAFToFilesystem method");
      env->DeleteLocalRef(jniClass);
      return false;
   }

   jstring jSource = env->NewStringUTF(safRelativePath);
   jstring jDest = env->NewStringUTF(destPath);
   jboolean result = env->CallStaticBooleanMethod(jniClass, method, jSource, jDest);

   env->DeleteLocalRef(jSource);
   env->DeleteLocalRef(jDest);
   env->DeleteLocalRef(jniClass);

   return (bool)result;
}

bool JNI_FileSystem_Delete(const char* relativePath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_Delete: Failed to get JNI environment");
      return false;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_Delete: Failed to find VPinballJNI class");
      return false;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFDelete", "(Ljava/lang/String;)Z");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_Delete: Failed to find SAFDelete method");
      env->DeleteLocalRef(jniClass);
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath);
   jboolean result = env->CallStaticBooleanMethod(jniClass, method, jPath);

   env->DeleteLocalRef(jPath);
   env->DeleteLocalRef(jniClass);

   return (bool)result;
}

bool JNI_FileSystem_IsDirectory(const char* relativePath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_IsDirectory: Failed to get JNI environment");
      return false;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_IsDirectory: Failed to find VPinballJNI class");
      return false;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFIsDirectory", "(Ljava/lang/String;)Z");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_IsDirectory: Failed to find SAFIsDirectory method");
      env->DeleteLocalRef(jniClass);
      return false;
   }

   jstring jPath = env->NewStringUTF(relativePath);
   jboolean result = env->CallStaticBooleanMethod(jniClass, method, jPath);

   env->DeleteLocalRef(jPath);
   env->DeleteLocalRef(jniClass);

   return (bool)result;
}

char* JNI_FileSystem_ListDirectory(const char* relativePath)
{
   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
   if (!env) {
      PLOGE.printf("JNI_FileSystem_ListDirectory: Failed to get JNI environment");
      return nullptr;
   }

   jclass jniClass = env->FindClass("org/vpinball/app/jni/VPinballJNI");
   if (!jniClass) {
      PLOGE.printf("JNI_FileSystem_ListDirectory: Failed to find VPinballJNI class");
      return nullptr;
   }

   jmethodID method = env->GetStaticMethodID(jniClass, "SAFListDirectory", "(Ljava/lang/String;)Ljava/lang/String;");
   if (!method) {
      PLOGE.printf("JNI_FileSystem_ListDirectory: Failed to find SAFListDirectory method");
      env->DeleteLocalRef(jniClass);
      return nullptr;
   }

   jstring jPath = env->NewStringUTF(relativePath);
   jstring result = (jstring)env->CallStaticObjectMethod(jniClass, method, jPath);

   env->DeleteLocalRef(jPath);
   env->DeleteLocalRef(jniClass);

   if (!result)
      return nullptr;

   const char* resultStr = env->GetStringUTFChars(result, nullptr);
   char* copy = strdup(resultStr);
   env->ReleaseStringUTFChars(result, resultStr);
   env->DeleteLocalRef(result);

   return copy;
}


}
