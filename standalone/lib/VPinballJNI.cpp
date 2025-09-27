#include "core/stdafx.h"

#include "VPinball_C.h"
#include "VPinballLib.h"

#include <SDL3/SDL_system.h>
#include <jni.h>

using namespace VPinballLib;

static jobject gJNICallbackObject = nullptr;
static jmethodID gJNIOnEventMethod = nullptr;

static jclass gJNIProgressDataClass = nullptr;
static jclass gJNIScriptErrorDataClass = nullptr;
static jclass gJNIRumbleDataClass = nullptr;
static jclass gJNIWebServerDataClass = nullptr;
static jclass gJNICaptureScreenshotDataClass = nullptr;
static jclass gJNIScriptErrorTypeClass = nullptr;
static jclass gJNIToneMapperClass = nullptr;
static jclass gJNIViewLayoutClass = nullptr;
static jclass gJNITableEventDataClass = nullptr;

void* VPinballJNI_OnEventCallback(VPINBALL_EVENT event, const char* jsonData, void* data)
{
   if (!gJNICallbackObject || !gJNIOnEventMethod)
      return nullptr;

   jobject eventDataObject = nullptr;

   JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();

   switch(event) {
      case VPINBALL_EVENT_ARCHIVE_UNCOMPRESSING:
      case VPINBALL_EVENT_ARCHIVE_COMPRESSING:
      case VPINBALL_EVENT_LOADING_ITEMS:
      case VPINBALL_EVENT_LOADING_IMAGES:
      case VPINBALL_EVENT_LOADING_SOUNDS:
      case VPINBALL_EVENT_LOADING_FONTS:
      case VPINBALL_EVENT_LOADING_COLLECTIONS:
      case VPINBALL_EVENT_PRERENDERING:
      {
         if (gJNIProgressDataClass) {
            jmethodID constructorMethod = env->GetMethodID(gJNIProgressDataClass, "<init>", "(I)V");
            if (constructorMethod) {
               ProgressData* pData = (ProgressData*)(data);
               if (pData)
                  eventDataObject = env->NewObject(gJNIProgressDataClass, constructorMethod, pData->progress);
            }
         }
         break;
      }
      case VPINBALL_EVENT_SCRIPT_ERROR:
      {
         jmethodID constructorMethod = env->GetMethodID(gJNIScriptErrorDataClass, "<init>", "(Lorg/vpinball/app/jni/VPinballScriptErrorType;IILjava/lang/String;)V");
         jmethodID fromIntMethod = env->GetStaticMethodID(gJNIScriptErrorTypeClass, "fromInt", "(I)Lorg/vpinball/app/jni/VPinballScriptErrorType;");
         if (constructorMethod && fromIntMethod) {
            ScriptErrorData* pData = (ScriptErrorData*)(data);
            if (pData) {
               jobject errorType = env->CallStaticObjectMethod(gJNIScriptErrorTypeClass, fromIntMethod, (jint)pData->error);
               jstring descriptionString = env->NewStringUTF(pData->description ? pData->description : "");
               jobject scriptErrorObject = env->NewObject(gJNIScriptErrorDataClass, constructorMethod, errorType, pData->line, pData->position, descriptionString);
               env->DeleteLocalRef(descriptionString);
               env->DeleteLocalRef(errorType);
               eventDataObject = scriptErrorObject;
            }
         }
         break;
      }
      case VPINBALL_EVENT_RUMBLE:
      {
         if (gJNIRumbleDataClass) {
            jmethodID constructorMethod = env->GetMethodID(gJNIRumbleDataClass, "<init>", "(III)V");
            if (constructorMethod) {
               RumbleData* pData = (RumbleData*)(data);
               if (pData) {
                  eventDataObject = env->NewObject(gJNIRumbleDataClass, constructorMethod,
                     (jint)pData->lowFrequencyRumble,
                     (jint)pData->highFrequencyRumble,
                     (jint)pData->durationMs);
               }
            }
         }
         break;
      }
      case VPINBALL_EVENT_WEB_SERVER:
      {
         if (gJNIWebServerDataClass) {
            jmethodID constructorMethod = env->GetMethodID(gJNIWebServerDataClass, "<init>", "(Ljava/lang/String;)V");
            if (constructorMethod) {
               WebServerData* pData = (WebServerData*)(data);
               if (pData) {
                  jstring urlString = env->NewStringUTF(pData->url ? pData->url : "");
                  eventDataObject = env->NewObject(gJNIWebServerDataClass, constructorMethod, urlString);
                  env->DeleteLocalRef(urlString);
               }
            }
         }
         break;
      }
      case VPINBALL_EVENT_CAPTURE_SCREENSHOT:
      {
         if (gJNICaptureScreenshotDataClass) {
            jmethodID constructorMethod = env->GetMethodID(gJNICaptureScreenshotDataClass, "<init>", "(Z)V");
            if (constructorMethod) {
                CaptureScreenshotData* pData = (CaptureScreenshotData*)(data);
                if (pData) {
                    jboolean success = pData->success ? JNI_TRUE : JNI_FALSE;
                    eventDataObject = env->NewObject(gJNICaptureScreenshotDataClass, constructorMethod, success);
                }
            }
         }
         break;
      }
      case VPINBALL_EVENT_TABLE_LIST:
      {
         eventDataObject = nullptr;
         break;
      }
      case VPINBALL_EVENT_TABLE_IMPORT:
      case VPINBALL_EVENT_TABLE_RENAME:
      case VPINBALL_EVENT_TABLE_DELETE:
      {
         if (gJNITableEventDataClass) {
            jmethodID constructorMethod = env->GetMethodID(gJNITableEventDataClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V");
            if (constructorMethod) {
                TableEventData* pData = (TableEventData*)(data);
                if (pData) {
                    jstring tableIdString = env->NewStringUTF(pData->tableId ? pData->tableId : "");
                    jstring newNameString = env->NewStringUTF(pData->newName ? pData->newName : "");
                    jstring pathString = env->NewStringUTF(pData->path ? pData->path : "");
                    jboolean success = pData->success ? JNI_TRUE : JNI_FALSE;

                    eventDataObject = env->NewObject(gJNITableEventDataClass, constructorMethod, 
                                                   tableIdString, newNameString, pathString, success);

                    env->DeleteLocalRef(tableIdString);
                    env->DeleteLocalRef(newNameString);
                    env->DeleteLocalRef(pathString);
                }
            }
         }
         break;
      }
      default:
         break;
   }

   jobject result = env->CallObjectMethod(gJNICallbackObject, gJNIOnEventMethod, (jint)event, eventDataObject);

   if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return nullptr;
   }

   void* nativeResult = nullptr;
   if (result && event == VPINBALL_EVENT_TABLE_LIST) {
      TablesData* tablesData = (TablesData*)data;
      if (tablesData) {
         jclass tablesDataClass = env->GetObjectClass(result);
         jfieldID successField = env->GetFieldID(tablesDataClass, "success", "Z");
         jboolean success = env->GetBooleanField(result, successField);
         tablesData->success = success;

         if (success) {
            jfieldID tablesField = env->GetFieldID(tablesDataClass, "tables", "Ljava/util/List;");
            jobject tablesList = env->GetObjectField(result, tablesField);

            if (tablesList) {
               jclass listClass = env->GetObjectClass(tablesList);
               jmethodID sizeMethod = env->GetMethodID(listClass, "size", "()I");
               jint listSize = env->CallIntMethod(tablesList, sizeMethod);

               if (listSize > 0) {
                  tablesData->tables = new TableInfo[listSize];
                  tablesData->tableCount = listSize;

                  jmethodID getMethod = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
                  for (int i = 0; i < listSize; i++) {
                     jobject tableInfo = env->CallObjectMethod(tablesList, getMethod, i);
                     if (tableInfo) {
                        jclass tableInfoClass = env->GetObjectClass(tableInfo);
                        jfieldID tableIdField = env->GetFieldID(tableInfoClass, "tableId", "Ljava/lang/String;");
                        jfieldID nameField = env->GetFieldID(tableInfoClass, "name", "Ljava/lang/String;");

                        jstring tableIdStr = (jstring)env->GetObjectField(tableInfo, tableIdField);
                        jstring nameStr = (jstring)env->GetObjectField(tableInfo, nameField);

                        const char* tableIdChars = env->GetStringUTFChars(tableIdStr, nullptr);
                        const char* nameChars = env->GetStringUTFChars(nameStr, nullptr);

                        tablesData->tables[i].tableId = new char[strlen(tableIdChars) + 1];
                        strcpy(tablesData->tables[i].tableId, tableIdChars);

                        tablesData->tables[i].name = new char[strlen(nameChars) + 1];
                        strcpy(tablesData->tables[i].name, nameChars);

                        env->ReleaseStringUTFChars(tableIdStr, tableIdChars);
                        env->ReleaseStringUTFChars(nameStr, nameChars);
                        env->DeleteLocalRef(tableInfo);
                     }
                  }
               }
            }
         }
      }
      env->DeleteLocalRef(result);
   } else if (event == VPINBALL_EVENT_TABLE_RENAME || event == VPINBALL_EVENT_TABLE_DELETE || event == VPINBALL_EVENT_TABLE_IMPORT) {
      TableEventData* eventData = (TableEventData*)data;
      if (eventData && eventDataObject) {
         jclass eventDataClass = env->GetObjectClass(eventDataObject);
         jfieldID successField = env->GetFieldID(eventDataClass, "success", "Z");
         jboolean success = env->GetBooleanField(eventDataObject, successField);
         eventData->success = success;
      }
      if (result)
         env->DeleteLocalRef(result);
   } else if (result) {
      nativeResult = env->GetDirectBufferAddress(result);
      env->DeleteLocalRef(result);
   }

   if (eventDataObject)
      env->DeleteLocalRef(eventDataObject);

   return nativeResult;
}

extern "C" {

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballGetVersionStringFull(JNIEnv* env, jobject obj)
{
   return env->NewStringUTF(VPinballGetVersionStringFull());
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballInit(JNIEnv* env, jobject obj, jobject callback)
{
   if (!callback)
      return;

   gJNICallbackObject = env->NewGlobalRef(callback);
   gJNIOnEventMethod = env->GetMethodID(env->GetObjectClass(gJNICallbackObject), "onEvent", "(ILjava/lang/Object;)Ljava/lang/Object;");

   gJNIProgressDataClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballProgressData"));
   gJNIScriptErrorDataClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballScriptErrorData"));
   gJNIRumbleDataClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballRumbleData"));
   gJNIWebServerDataClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballWebServerData"));
   gJNICaptureScreenshotDataClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballCaptureScreenshotData"));
   gJNIScriptErrorTypeClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballScriptErrorType"));
   gJNIToneMapperClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballToneMapper"));
   gJNIViewLayoutClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballViewLayoutMode"));

   gJNITableEventDataClass = (jclass)env->NewGlobalRef(env->FindClass("org/vpinball/app/jni/VPinballTableEventData"));

   VPinballInit(VPinballJNI_OnEventCallback);
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

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballUpdateWebServer(JNIEnv* env, jobject obj)
{
   VPinballUpdateWebServer();
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballSetWebServerUpdated(JNIEnv* env, jobject obj)
{
   VPinballSetWebServerUpdated();
}

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

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballExtractScript(JNIEnv* env, jobject obj, jstring source)
{
   const char* pSource = env->GetStringUTFChars(source, nullptr);
   VPINBALL_STATUS status = VPinballExtractScript(pSource);
   env->ReleaseStringUTFChars(source, pSource);
   return status;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballPlay(JNIEnv* env, jobject obj)
{
   return VPinballPlay();
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballStop(JNIEnv* env, jobject obj)
{
   return VPinballStop();
}

JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballToggleFPS(JNIEnv* env, jobject obj)
{
   VPinballToggleFPS();
}






JNIEXPORT void JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballCaptureScreenshot(JNIEnv* env, jobject obj, jstring filename)
{
   const char* pFilename = env->GetStringUTFChars(filename, nullptr);
   VPinballCaptureScreenshot(pFilename);
   env->ReleaseStringUTFChars(filename, pFilename);
}

JNIEXPORT jobject JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballGetTableList(JNIEnv* env, jobject obj)
{
   if (!gJNICallbackObject)
      return nullptr;

   jclass managerClass = env->GetObjectClass(gJNICallbackObject);
   jmethodID getTableListMethod = env->GetMethodID(managerClass, "getTableList", "()Lorg/vpinball/app/jni/VPinballTablesData;");

   if (getTableListMethod)
      return env->CallObjectMethod(gJNICallbackObject, getTableListMethod);

   return nullptr;
}

// Table Management JNI Implementations
JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballRefreshTables(JNIEnv* env, jobject obj)
{
   return VPinballRefreshTables();
}

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballGetVPXTables(JNIEnv* env, jobject obj)
{
   char* result = VPinballGetVPXTables();
   if (result) {
      jstring jResult = env->NewStringUTF(result);
      VPinballFreeString(result);
      return jResult;
   }
   return nullptr;
}

JNIEXPORT jstring JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballGetVPXTable(JNIEnv* env, jobject obj, jstring uuid)
{
   const char* pUuid = env->GetStringUTFChars(uuid, nullptr);
   char* result = VPinballGetVPXTable(pUuid);
   env->ReleaseStringUTFChars(uuid, pUuid);
   
   if (result) {
      jstring jResult = env->NewStringUTF(result);
      VPinballFreeString(result);
      return jResult;
   }
   return nullptr;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballAddVPXTable(JNIEnv* env, jobject obj, jstring filePath)
{
   const char* pFilePath = env->GetStringUTFChars(filePath, nullptr);
   int result = VPinballAddVPXTable(pFilePath);
   env->ReleaseStringUTFChars(filePath, pFilePath);
   return result;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballRemoveVPXTable(JNIEnv* env, jobject obj, jstring uuid)
{
   const char* pUuid = env->GetStringUTFChars(uuid, nullptr);
   int result = VPinballRemoveVPXTable(pUuid);
   env->ReleaseStringUTFChars(uuid, pUuid);
   return result;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballRenameVPXTable(JNIEnv* env, jobject obj, jstring uuid, jstring newName)
{
   const char* pUuid = env->GetStringUTFChars(uuid, nullptr);
   const char* pNewName = env->GetStringUTFChars(newName, nullptr);
   int result = VPinballRenameVPXTable(pUuid, pNewName);
   env->ReleaseStringUTFChars(uuid, pUuid);
   env->ReleaseStringUTFChars(newName, pNewName);
   return result;
}

JNIEXPORT jint JNICALL Java_org_vpinball_app_jni_VPinballJNI_VPinballImportTableFile(JNIEnv* env, jobject obj, jstring sourceFile)
{
   const char* pSourceFile = env->GetStringUTFChars(sourceFile, nullptr);
   int result = VPinballImportTableFile(pSourceFile);
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


}