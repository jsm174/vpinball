#include <core/stdafx.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include "../include/vpinball/VPinballLib_C.h"
#include "VPinballLib.h"
#include "FileSystem.h"

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
   return VPinballLib::VPinballLib::Instance().AppInit(argc, argv) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
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

VPINBALLAPI void VPinballInit(VPinballEventCallback callback)
{
   VPinballLib::VPinballLib::Instance().Init(callback);
}

VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* pMessage)
{
   if (pMessage != nullptr)
      VPinballLib::VPinballLib::Instance().Log(level, pMessage);
}

VPINBALLAPI void VPinballResetLog()
{
   VPinballLib::VPinballLib::Instance().ResetLog();
}

VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue)
{
    if (pSectionName == nullptr || pKey == nullptr)
      return defaultValue;

   return VPinballLib::VPinballLib::Instance().LoadValueInt(pSectionName, pKey, defaultValue);
}

VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue)
{
   if (pSectionName == nullptr || pKey == nullptr)
      return defaultValue;

   return VPinballLib::VPinballLib::Instance().LoadValueFloat(pSectionName, pKey, defaultValue);
}

VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue)
{
   if (pSectionName == nullptr || pKey == nullptr)
      return pDefaultValue;

   thread_local string value;
   value = VPinballLib::VPinballLib::Instance().LoadValueString(pSectionName, pKey, pDefaultValue);
   return value.c_str();
}

VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value)
{
   if (pSectionName == nullptr || pKey == nullptr)
      return;

   VPinballLib::VPinballLib::Instance().SaveValueInt(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value)
{
   if (pSectionName == nullptr || pKey == nullptr)
      return;

   VPinballLib::VPinballLib::Instance().SaveValueFloat(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue)
{
   if (pSectionName == nullptr || pKey == nullptr || pValue == nullptr)
      return;

   VPinballLib::VPinballLib::Instance().SaveValueString(pSectionName, pKey, pValue);
}

VPINBALLAPI VPINBALL_STATUS VPinballResetIni()
{
   return VPinballLib::VPinballLib::Instance().ResetIni();
}

VPINBALLAPI void VPinballUpdateWebServer()
{
   VPinballLib::VPinballLib::Instance().UpdateWebServer();
}

VPINBALLAPI VPINBALL_STATUS VPinballLoadTable(const char* pUuid)
{
   if (pUuid == nullptr)
      return VPINBALL_STATUS_FAILURE;

   return VPinballLib::VPinballLib::Instance().LoadTable(pUuid);
}

VPINBALLAPI VPINBALL_STATUS VPinballExtractTableScript()
{
   return VPinballLib::VPinballLib::Instance().ExtractTableScript();
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
   if (pPath == nullptr)
      return false;

   return VPinballLib::VPinballLib::Instance().FileExists(pPath);
}

VPINBALLAPI bool VPinballDeleteFile(const char* pPath)
{
   if (pPath == nullptr)
      return false;

   return VPinballLib::VPinballLib::Instance().DeleteFile(pPath);
}

VPINBALLAPI bool VPinballCopyFile(const char* pSourcePath, const char* pDestPath)
{
   if (pSourcePath == nullptr || pDestPath == nullptr)
      return false;

   return VPinballLib::FileSystem::CopyFile(pSourcePath, pDestPath, "");
}

VPINBALLAPI const char* VPinballStageFile(const char* pPath)
{
   if (pPath == nullptr)
      return nullptr;

   thread_local string stagedPath;
   string path(pPath);
   stagedPath = VPinballLib::VPinballLib::Instance().StageFile(path);
   return stagedPath.c_str();
}

VPINBALLAPI const char* VPinballGetTables()
{
   thread_local string jsonStr;
   jsonStr = VPinballLib::VPinballLib::Instance().GetTables();
   return jsonStr.c_str();
}

VPINBALLAPI VPINBALL_STATUS VPinballRefreshTables()
{
   return VPinballLib::VPinballLib::Instance().RefreshTables();
}

VPINBALLAPI const char* VPinballGetTablesPath()
{
   thread_local string tablesPath;
   tablesPath = VPinballLib::VPinballLib::Instance().GetTablesPath();
   return tablesPath.c_str();
}

VPINBALLAPI VPINBALL_STATUS VPinballRenameTable(const char* pUuid, const char* pNewName)
{
   if (pUuid == nullptr || pNewName == nullptr)
      return VPINBALL_STATUS_FAILURE;

   return VPinballLib::VPinballLib::Instance().RenameTable(pUuid, pNewName);
}

VPINBALLAPI VPINBALL_STATUS VPinballDeleteTable(const char* pUuid)
{
   if (pUuid == nullptr)
      return VPINBALL_STATUS_FAILURE;

   return VPinballLib::VPinballLib::Instance().DeleteTable(pUuid);
}

VPINBALLAPI VPINBALL_STATUS VPinballImportTable(const char* pSourceFile)
{
   if (pSourceFile == nullptr)
      return VPINBALL_STATUS_FAILURE;

   return VPinballLib::VPinballLib::Instance().ImportTable(pSourceFile);
}

VPINBALLAPI VPINBALL_STATUS VPinballSetTableImage(const char* pUuid, const char* pPath)
{
   if (pUuid == nullptr)
      return VPINBALL_STATUS_FAILURE;

   return VPinballLib::VPinballLib::Instance().SetTableImage(pUuid, pPath != nullptr ? pPath : string(""));
}

VPINBALLAPI const char* VPinballExportTable(const char* pUuid)
{
   if (pUuid == nullptr)
      return nullptr;

   thread_local string exportPath;
   exportPath = VPinballLib::VPinballLib::Instance().ExportTable(pUuid);
   return !exportPath.empty() ? exportPath.c_str() : nullptr;
}
