#include <core/stdafx.h>
#include <nlohmann/json.hpp>
#include <mutex>

#include "VPinball_C.h"
#include "VPinballLib.h"

using json = nlohmann::json;

namespace {
   bool TryParseJson(const char* jsonStr, json& result, string& errorMsg) {
      if (!jsonStr) {
         errorMsg = "Null JSON string";
         return false;
      }
      
      try {
         result = json::parse(jsonStr);
         return true;
      } catch (const json::parse_error& e) {
         errorMsg = string("JSON parse error: ") + e.what();
         return false;
      } catch (const std::exception& e) {
         errorMsg = string("JSON error: ") + e.what();
         return false;
      }
   }
}

VPINBALLAPI const char* VPinballGetVersionStringFull()
{
   thread_local string version;
   version = VPinballLib::VPinball::GetInstance().GetVersionStringFull();
   return version.c_str();
}

VPINBALLAPI void VPinballInit(VPinballEventCallback callback)
{
   auto eventCallback = [callback](VPinballLib::Event event, void* data) -> void* {
      thread_local string jsonString;
      const char* jsonData = nullptr;
      
      // Convert C structs to JSON strings using nlohmann/json
      if (data != nullptr) {
         json j;
         
         switch(event) {
            case VPinballLib::Event::ArchiveUncompressing:
            case VPinballLib::Event::ArchiveCompressing:
            case VPinballLib::Event::LoadingItems:
            case VPinballLib::Event::LoadingSounds:
            case VPinballLib::Event::LoadingImages:
            case VPinballLib::Event::LoadingFonts:
            case VPinballLib::Event::LoadingCollections:
            case VPinballLib::Event::Prerendering: {
               VPinballLib::ProgressData* progressData = (VPinballLib::ProgressData*)data;
               j["progress"] = progressData->progress;
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPinballLib::Event::Rumble: {
               VPinballLib::RumbleData* rumbleData = (VPinballLib::RumbleData*)data;
               j["lowFrequencyRumble"] = rumbleData->lowFrequencyRumble;
               j["highFrequencyRumble"] = rumbleData->highFrequencyRumble;
               j["durationMs"] = rumbleData->durationMs;
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPinballLib::Event::ScriptError: {
               VPinballLib::ScriptErrorData* scriptErrorData = (VPinballLib::ScriptErrorData*)data;
               j["error"] = (int)scriptErrorData->error;
               j["line"] = scriptErrorData->line;
               j["position"] = scriptErrorData->position;
               j["description"] = scriptErrorData->description ? scriptErrorData->description : "";
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPinballLib::Event::WebServer: {
               VPinballLib::WebServerData* webServerData = (VPinballLib::WebServerData*)data;
               j["url"] = webServerData->url ? webServerData->url : "";
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPinballLib::Event::CaptureScreenshot: {
               VPinballLib::CaptureScreenshotData* screenshotData = (VPinballLib::CaptureScreenshotData*)data;
               j["success"] = screenshotData->success;
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            default:
               // For events without specific data, pass null
               break;
         }
      }
      
      // Pass both JSON string and original void pointer for hybrid access
      return callback((VPINBALL_EVENT)event, jsonData, data);
   };

   VPinballLib::VPinball::GetInstance().Init(eventCallback);
}

VPINBALLAPI void VPinballSetupEventCallback()
{
   VPinballLib::VPinball::GetInstance().SetupEventCallback();
}

VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* pMessage)
{
   VPinballLib::VPinball::GetInstance().Log((VPinballLib::LogLevel)level, pMessage);
}

VPINBALLAPI void VPinballResetLog()
{
   VPinballLib::VPinball::GetInstance().ResetLog();
}

VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue)
{
   return VPinballLib::VPinball::GetInstance().LoadValueInt(pSectionName, pKey, defaultValue);
}

VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue)
{
   return VPinballLib::VPinball::GetInstance().LoadValueFloat(pSectionName, pKey, defaultValue);
}

VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue)
{
   thread_local string value;
   value = VPinballLib::VPinball::GetInstance().LoadValueString(pSectionName, pKey, pDefaultValue);
   return value.c_str();
}

VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value)
{
   VPinballLib::VPinball::GetInstance().SaveValueInt(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value)
{
   VPinballLib::VPinball::GetInstance().SaveValueFloat(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue)
{
   VPinballLib::VPinball::GetInstance().SaveValueString(pSectionName, pKey, pValue);
}

VPINBALLAPI void VPinballUpdateWebServer()
{
   VPinballLib::VPinball::GetInstance().UpdateWebServer();
}

VPINBALLAPI void VPinballSetWebServerUpdated()
{
   VPinballLib::VPinball::GetInstance().SetWebServerUpdated();
}

VPINBALLAPI VPINBALL_STATUS VPinballResetIni()
{
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().ResetIni();
}

VPINBALLAPI VPINBALL_STATUS VPinballLoad(const char* pSource)
{
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().Load(pSource);
}

VPINBALLAPI VPINBALL_STATUS VPinballExtractScript(const char* pSource)
{
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().ExtractScript(pSource);
}

VPINBALLAPI VPINBALL_STATUS VPinballPlay()
{
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().Play();
}

VPINBALLAPI VPINBALL_STATUS VPinballStop()
{
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().Stop();
}

VPINBALLAPI void VPinballToggleFPS()
{
   VPinballLib::VPinball::GetInstance().ToggleFPS();
}


VPINBALLAPI void VPinballCaptureScreenshot(const char* pFilename)
{
   return VPinballLib::VPinball::GetInstance().CaptureScreenshot(pFilename);
}

VPINBALLAPI VPINBALL_STATUS VPinballRefreshTables()
{
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().RefreshTables();
}

VPINBALLAPI char* VPinballGetVPXTables()
{
   PLOGI.printf("VPinballGetVPXTables: Starting");
   
   char* result = VPinballLib::VPinball::GetInstance().GetTablesJson();
   
   PLOGI.printf("VPinballGetVPXTables: Successfully created JSON string");
   return result;
}

VPINBALLAPI char* VPinballGetVPXTable(const char* uuid)
{
   if (!uuid) {
      string errorJson = "{\"success\":false,\"error\":\"Invalid UUID\"}";
      char* result = new char[errorJson.length() + 1];
      strcpy(result, errorJson.c_str());
      return result;
   }
   
   string uuidStr(uuid);
   return VPinballLib::VPinball::GetInstance().GetTableJson(uuidStr);
}

VPINBALLAPI VPINBALL_STATUS VPinballAddVPXTable(const char* pFilePath)
{
   string filePath(pFilePath);
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().AddVPXTable(filePath);
}

VPINBALLAPI VPINBALL_STATUS VPinballRemoveVPXTable(const char* pUuid)
{
   string uuid(pUuid);
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().RemoveVPXTable(uuid);
}

VPINBALLAPI VPINBALL_STATUS VPinballRenameVPXTable(const char* pUuid, const char* pNewName)
{
   string uuid(pUuid);
   string newName(pNewName);
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().RenameVPXTable(uuid, newName);
}

VPINBALLAPI VPINBALL_STATUS VPinballImportTableFile(const char* pSourceFile)
{
   string sourceFile(pSourceFile);
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().ImportTableFile(sourceFile);
}

VPINBALLAPI VPINBALL_STATUS VPinballSetTableArtwork(const char* pUuid, const char* pArtworkPath)
{
   string uuid(pUuid ? pUuid : "");
   string artworkPath(pArtworkPath ? pArtworkPath : "");
   return (VPINBALL_STATUS)VPinballLib::VPinball::GetInstance().SetTableArtwork(uuid, artworkPath);
}

VPINBALLAPI const char* VPinballGetTablesPath()
{
   return VPinballLib::VPinball::GetInstance().GetTablesPath();
}

VPINBALLAPI const char* VPinballExportTable(const char* pUuid)
{
   if (!pUuid) {
      return nullptr;
   }
   
   string uuid(pUuid);
   static string exportPath;
   exportPath = VPinballLib::VPinball::GetInstance().ExportTable(uuid);
   
   if (exportPath.empty()) {
      return nullptr;
   }
   
   return exportPath.c_str();
}


VPINBALLAPI void VPinballFreeString(char* jsonString)
{
   if (jsonString) {
      delete[] jsonString;
   }
}

