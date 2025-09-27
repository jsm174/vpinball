#include <core/stdafx.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include "../include/vpinball/VPinballLib_C.h"
#include "VPinballLib.h"

// SDL Main Callbacks

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
   return VPinballLib::VPinballLib::Instance().AppInit(argc, argv) ?
      SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
   VPinballLib::VPinballLib::Instance().AppIterate();
   return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
   VPinballLib::VPinballLib::Instance().AppEvent(event);
   return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
}

VPINBALLAPI const char* VPinballGetVersionStringFull()
{
   thread_local string version;
   version = VPinballLib::VPinballLib::Instance().GetVersionStringFull();
   return version.c_str();
}

VPINBALLAPI void VPinballSetEventCallback(VPinballEventCallback callback)
{
   VPinballLib::VPinballLib::Instance().SetEventCallback(callback);
}

VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* message)
{
   VPinballLib::VPinballLib::Instance().Log(level, message);
}

VPINBALLAPI void VPinballResetLog()
{
   VPinballLib::VPinballLib::Instance().ResetLog();
}

VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue)
{
   return VPinballLib::VPinballLib::Instance().LoadValueInt(pSectionName, pKey, defaultValue);
}

VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue)
{
   return VPinballLib::VPinballLib::Instance().LoadValueFloat(pSectionName, pKey, defaultValue);
}

VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue)
{
   thread_local string value;
   value = VPinballLib::VPinballLib::Instance().LoadValueString(pSectionName, pKey, pDefaultValue);
   return value.c_str();
}

VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value)
{
   VPinballLib::VPinballLib::Instance().SaveValueInt(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value)
{
   VPinballLib::VPinballLib::Instance().SaveValueFloat(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue)
{
   VPinballLib::VPinballLib::Instance().SaveValueString(pSectionName, pKey, pValue);
}

VPINBALLAPI VPINBALL_STATUS VPinballResetIni()
{
   return VPinballLib::VPinballLib::Instance().ResetIni();
}

VPINBALLAPI void VPinballStartWebServer()
{
   VPinballLib::VPinballLib::Instance().StartWebServer();
}

VPINBALLAPI void VPinballStopWebServer()
{
   VPinballLib::VPinballLib::Instance().StopWebServer();
}

VPINBALLAPI VPINBALL_STATUS VPinballLoad(const char* pUuid)
{
   return VPinballLib::VPinballLib::Instance().Load(pUuid);
}

VPINBALLAPI VPINBALL_STATUS VPinballExtractScript()
{
   return VPinballLib::VPinballLib::Instance().ExtractScript();
}

VPINBALLAPI VPINBALL_STATUS VPinballPlay()
{
   return VPinballLib::VPinballLib::Instance().Play();
}

VPINBALLAPI VPINBALL_STATUS VPinballStop()
{
   return VPinballLib::VPinballLib::Instance().Stop();
}

VPINBALLAPI bool VPinballFileExists(const char* pPath)
{
   string path(pPath);
   return VPinballLib::VPinballLib::Instance().FileExists(path);
}

VPINBALLAPI const char* VPinballPrepareFileForViewing(const char* pPath)
{
   thread_local string viewPath;
   string path(pPath);
   viewPath = VPinballLib::VPinballLib::Instance().PrepareFileForViewing(path);
   return viewPath.c_str();
}

VPINBALLAPI VPINBALL_STATUS VPinballRefreshTables()
{
   return VPinballLib::VPinballLib::Instance().RefreshTables();
}

VPINBALLAPI VPINBALL_STATUS VPinballReloadTablesPath()
{
   return VPinballLib::VPinballLib::Instance().ReloadTablesPath();
}

VPINBALLAPI const char* VPinballGetTables()
{
   return VPinballLib::VPinballLib::Instance().GetTables();
}

VPINBALLAPI const char* VPinballGetTablesPath()
{
   return VPinballLib::VPinballLib::Instance().GetTablesPath();
}

VPINBALLAPI VPINBALL_STATUS VPinballRenameTable(const char* pUuid, const char* pNewName)
{
   string uuid(pUuid);
   string newName(pNewName);
   return VPinballLib::VPinballLib::Instance().RenameTable(uuid, newName);
}

VPINBALLAPI VPINBALL_STATUS VPinballDeleteTable(const char* pUuid)
{
   string uuid(pUuid);
   return VPinballLib::VPinballLib::Instance().DeleteTable(uuid);
}

VPINBALLAPI VPINBALL_STATUS VPinballImportTable(const char* pSourceFile)
{
   string sourceFile(pSourceFile);
   return VPinballLib::VPinballLib::Instance().ImportTable(sourceFile);
}

VPINBALLAPI VPINBALL_STATUS VPinballSetTableArtwork(const char* pUuid, const char* pArtworkPath)
{
   string uuid(pUuid ? pUuid : "");
   string artworkPath(pArtworkPath ? pArtworkPath : "");
   return VPinballLib::VPinballLib::Instance().SetTableArtwork(uuid, artworkPath);
}

VPINBALLAPI const char* VPinballExportTable(const char* pUuid)
{
   string uuid(pUuid);
   static string exportPath;
   exportPath = VPinballLib::VPinballLib::Instance().ExportTable(uuid);
   return !exportPath.empty() ? exportPath.c_str() : nullptr;
}

VPINBALLAPI void VPinballFreeString(char* jsonString)
{
   if (jsonString) {
      delete[] jsonString;
   }
}

