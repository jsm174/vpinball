#include <core/stdafx.h>
#include <nlohmann/json.hpp>
#include <mutex>

#include "VPinball.h"
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

static VPinballLib::VPinball& s_vpinstance = VPinballLib::VPinball::GetInstance();

VPINBALLAPI const char* VPinballGetVersionStringFull()
{
   thread_local string version;
   version = s_vpinstance.GetVersionStringFull();
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
            case VPinballLib::Event::WindowCreated: {
               VPinballLib::WindowCreatedData* windowData = (VPinballLib::WindowCreatedData*)data;
               j["pWindow"] = windowData->pWindow ? "valid" : "null";
               j["pTitle"] = windowData->pTitle ? windowData->pTitle : "";
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

   s_vpinstance.Init(eventCallback);
}

VPINBALLAPI void VPinballSetupEventCallback()
{
   s_vpinstance.SetupEventCallback();
}

VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* pMessage)
{
   s_vpinstance.Log((VPinballLib::LogLevel)level, pMessage);
}

VPINBALLAPI void VPinballResetLog()
{
   s_vpinstance.ResetLog();
}

VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue)
{
   return s_vpinstance.LoadValueInt(pSectionName, pKey, defaultValue);
}

VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue)
{
   return s_vpinstance.LoadValueFloat(pSectionName, pKey, defaultValue);
}

VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue)
{
   thread_local string value;
   value = s_vpinstance.LoadValueString(pSectionName, pKey, pDefaultValue);
   return value.c_str();
}

VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value)
{
   s_vpinstance.SaveValueInt(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value)
{
   s_vpinstance.SaveValueFloat(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue)
{
   s_vpinstance.SaveValueString(pSectionName, pKey, pValue);
}

VPINBALLAPI void VPinballUpdateWebServer()
{
   s_vpinstance.UpdateWebServer();
}

VPINBALLAPI void VPinballSetWebServerUpdated()
{
   s_vpinstance.SetWebServerUpdated();
}

VPINBALLAPI VPINBALL_STATUS VPinballResetIni()
{
   return (VPINBALL_STATUS)s_vpinstance.ResetIni();
}

VPINBALLAPI VPINBALL_STATUS VPinballLoad(const char* pSource)
{
   return (VPINBALL_STATUS)s_vpinstance.Load(pSource);
}

VPINBALLAPI VPINBALL_STATUS VPinballExtractScript(const char* pSource)
{
   return (VPINBALL_STATUS)s_vpinstance.ExtractScript(pSource);
}

VPINBALLAPI VPINBALL_STATUS VPinballPlay()
{
   return (VPINBALL_STATUS)s_vpinstance.Play();
}

VPINBALLAPI VPINBALL_STATUS VPinballStop()
{
   return (VPINBALL_STATUS)s_vpinstance.Stop();
}

VPINBALLAPI void VPinballSetPlayState(int enable)
{
   s_vpinstance.SetPlayState(enable);
}

VPINBALLAPI void VPinballToggleFPS()
{
   s_vpinstance.ToggleFPS();
}

VPINBALLAPI char* VPinballGetTableOptions()
{
   VPinballLib::TableOptions tableOptions;
   s_vpinstance.GetTableOptions(tableOptions);
   
   json j;
   j["globalEmissionScale"] = tableOptions.globalEmissionScale;
   j["globalDifficulty"] = tableOptions.globalDifficulty;
   j["exposure"] = tableOptions.exposure;
   j["toneMapper"] = tableOptions.toneMapper;
   j["musicVolume"] = tableOptions.musicVolume;
   j["soundVolume"] = tableOptions.soundVolume;
   
   string jsonString = j.dump();
   char* result = new char[jsonString.length() + 1];
   strcpy(result, jsonString.c_str());
   return result;
}

VPINBALLAPI void VPinballSetTableOptions(const char* jsonOptions)
{
   json j;
   string errorMsg;
   
   if (!TryParseJson(jsonOptions, j, errorMsg)) {
      PLOGE.printf("VPinballSetTableOptions failed: %s", errorMsg.c_str());
      return;
   }
      
   VPinballLib::TableOptions options;
   options.globalEmissionScale = j.value("globalEmissionScale", 0.0f);
   options.globalDifficulty = j.value("globalDifficulty", 0.0f);
   options.exposure = j.value("exposure", 0.0f);
   options.toneMapper = j.value("toneMapper", 0);
   options.musicVolume = j.value("musicVolume", 0);
   options.soundVolume = j.value("soundVolume", 0);
   s_vpinstance.SetTableOptions(options);
}

VPINBALLAPI void VPinballSetDefaultTableOptions()
{
   return s_vpinstance.SetDefaultTableOptions();
}

VPINBALLAPI void VPinballResetTableOptions()
{
   return s_vpinstance.ResetTableOptions();
}

VPINBALLAPI void VPinballSaveTableOptions()
{
   return s_vpinstance.SaveTableOptions();
}

VPINBALLAPI int VPinballGetCustomTableOptionsCount()
{
   return s_vpinstance.GetCustomTableOptionsCount();
}

VPINBALLAPI char* VPinballGetCustomTableOptions()
{
   int count = s_vpinstance.GetCustomTableOptionsCount();
   
   json jsonArray = json::array();
   
   for (int i = 0; i < count; ++i) {
      VPinballLib::CustomTableOption option;
      s_vpinstance.GetCustomTableOption(i, option);
      
      json optionJson;
      optionJson["sectionName"] = option.sectionName;
      optionJson["id"] = option.id;
      optionJson["name"] = option.name;
      optionJson["showMask"] = option.showMask;
      optionJson["minValue"] = option.minValue;
      optionJson["maxValue"] = option.maxValue;
      optionJson["step"] = option.step;
      optionJson["defaultValue"] = option.defaultValue;
      optionJson["unit"] = (int)option.unit;
      optionJson["literals"] = option.literals;
      optionJson["value"] = option.value;
      
      jsonArray.push_back(optionJson);
   }
   
   std::string jsonString = jsonArray.dump();
   char* result = new char[jsonString.length() + 1];
   strcpy(result, jsonString.c_str());
   return result;
}

VPINBALLAPI void VPinballSetCustomTableOption(const char* jsonOption)
{
   json j;
   string errorMsg;
   
   if (!TryParseJson(jsonOption, j, errorMsg)) {
      PLOGE.printf("VPinballSetCustomTableOption failed: %s", errorMsg.c_str());
      return;
   }
      
   VPinballLib::CustomTableOption option;
   option.sectionName = j.value("sectionName", "").c_str();
   option.id = j.value("id", "").c_str();
   option.value = j.value("value", 0.0f);
   s_vpinstance.SetCustomTableOption(option);
}

VPINBALLAPI void VPinballSetDefaultCustomTableOptions()
{
   return s_vpinstance.SetDefaultCustomTableOptions();
}

VPINBALLAPI void VPinballResetCustomTableOptions()
{
   return s_vpinstance.ResetCustomTableOptions();
}

VPINBALLAPI void VPinballSaveCustomTableOptions()
{
   return s_vpinstance.SaveCustomTableOptions();
}

VPINBALLAPI char* VPinballGetViewSetup()
{
   VPinballLib::ViewSetup viewSetup;
   s_vpinstance.GetViewSetup(viewSetup);
   
   json j;
   j["viewMode"] = viewSetup.viewMode;
   j["sceneScaleX"] = viewSetup.sceneScaleX;
   j["sceneScaleY"] = viewSetup.sceneScaleY;
   j["sceneScaleZ"] = viewSetup.sceneScaleZ;
   j["viewX"] = viewSetup.viewX;
   j["viewY"] = viewSetup.viewY;
   j["viewZ"] = viewSetup.viewZ;
   j["lookAt"] = viewSetup.lookAt;
   j["viewportRotation"] = viewSetup.viewportRotation;
   j["fov"] = viewSetup.fov;
   j["layback"] = viewSetup.layback;
   j["viewHOfs"] = viewSetup.viewHOfs;
   j["viewVOfs"] = viewSetup.viewVOfs;
   j["windowTopZOfs"] = viewSetup.windowTopZOfs;
   j["windowBottomZOfs"] = viewSetup.windowBottomZOfs;
   
   string jsonString = j.dump();
   char* result = new char[jsonString.length() + 1];
   strcpy(result, jsonString.c_str());
   return result;
}

VPINBALLAPI void VPinballSetViewSetup(const char* jsonSetup)
{
   json j;
   string errorMsg;
   
   if (!TryParseJson(jsonSetup, j, errorMsg)) {
      PLOGE.printf("VPinballSetViewSetup failed: %s", errorMsg.c_str());
      return;
   }
      
   VPinballLib::ViewSetup viewSetup;
   viewSetup.viewMode = j.value("viewMode", 0);
   viewSetup.sceneScaleX = j.value("sceneScaleX", 0.0f);
   viewSetup.sceneScaleY = j.value("sceneScaleY", 0.0f);
   viewSetup.sceneScaleZ = j.value("sceneScaleZ", 0.0f);
   viewSetup.viewX = j.value("viewX", 0.0f);
   viewSetup.viewY = j.value("viewY", 0.0f);
   viewSetup.viewZ = j.value("viewZ", 0.0f);
   viewSetup.lookAt = j.value("lookAt", 0.0f);
   viewSetup.viewportRotation = j.value("viewportRotation", 0.0f);
   viewSetup.fov = j.value("fov", 0.0f);
   viewSetup.layback = j.value("layback", 0.0f);
   viewSetup.viewHOfs = j.value("viewHOfs", 0.0f);
   viewSetup.viewVOfs = j.value("viewVOfs", 0.0f);
   viewSetup.windowTopZOfs = j.value("windowTopZOfs", 0.0f);
   viewSetup.windowBottomZOfs = j.value("windowBottomZOfs", 0.0f);
   s_vpinstance.SetViewSetup(viewSetup);
}

VPINBALLAPI void VPinballSetDefaultViewSetup()
{
   return s_vpinstance.SetDefaultViewSetup();
}

VPINBALLAPI void VPinballResetViewSetup()
{
   return s_vpinstance.ResetViewSetup();
}

VPINBALLAPI void VPinballSaveViewSetup()
{
   return s_vpinstance.SaveViewSetup();
}

VPINBALLAPI void VPinballCaptureScreenshot(const char* pFilename)
{
   return s_vpinstance.CaptureScreenshot(pFilename);
}

VPINBALLAPI VPINBALL_STATUS VPinballRefreshTables()
{
   return (VPINBALL_STATUS)s_vpinstance.RefreshTables();
}

VPINBALLAPI char* VPinballGetVPXTables()
{
   PLOGI.printf("VPinballGetVPXTables: Starting");
   
   char* result = s_vpinstance.GetTablesJson();
   
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
   return s_vpinstance.GetTableJson(uuidStr);
}

VPINBALLAPI VPINBALL_STATUS VPinballAddVPXTable(const char* pFilePath)
{
   string filePath(pFilePath);
   return (VPINBALL_STATUS)s_vpinstance.AddVPXTable(filePath);
}

VPINBALLAPI VPINBALL_STATUS VPinballRemoveVPXTable(const char* pUuid)
{
   string uuid(pUuid);
   return (VPINBALL_STATUS)s_vpinstance.RemoveVPXTable(uuid);
}

VPINBALLAPI VPINBALL_STATUS VPinballRenameVPXTable(const char* pUuid, const char* pNewName)
{
   string uuid(pUuid);
   string newName(pNewName);
   return (VPINBALL_STATUS)s_vpinstance.RenameVPXTable(uuid, newName);
}

VPINBALLAPI VPINBALL_STATUS VPinballImportTableFile(const char* pSourceFile)
{
   string sourceFile(pSourceFile);
   return (VPINBALL_STATUS)s_vpinstance.ImportTableFile(sourceFile);
}

VPINBALLAPI VPINBALL_STATUS VPinballSetTableArtwork(const char* pUuid, const char* pArtworkPath)
{
   string uuid(pUuid ? pUuid : "");
   string artworkPath(pArtworkPath ? pArtworkPath : "");
   return (VPINBALL_STATUS)s_vpinstance.SetTableArtwork(uuid, artworkPath);
}

VPINBALLAPI const char* VPinballGetTablesPath()
{
   return s_vpinstance.GetTablesPath();
}

VPINBALLAPI const char* VPinballExportTable(const char* pUuid)
{
   if (!pUuid) {
      return nullptr;
   }
   
   string uuid(pUuid);
   static string exportPath;
   exportPath = s_vpinstance.ExportTable(uuid);
   
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

