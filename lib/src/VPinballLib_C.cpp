#include <core/stdafx.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include "../include/vpinball/VPinballLib_C.h"
#include "VPinballLib.h"
#include "FileSystem.h"

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

VPINBALLAPI void VPinballStartup(VPinballEventCallback callback)
{
   VPinballLib::VPinballLib::Instance().Startup(callback);
}

VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* message)
{
   if (!message) {
      return;
   }
   VPinballLib::VPinballLib::Instance().Log(level, message);
}

VPINBALLAPI void VPinballResetLog()
{
   VPinballLib::VPinballLib::Instance().ResetLog();
}

VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue)
{
   if (!pSectionName || !pKey) {
      return defaultValue;
   }
   return VPinballLib::VPinballLib::Instance().LoadValueInt(pSectionName, pKey, defaultValue);
}

VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue)
{
   if (!pSectionName || !pKey) {
      return defaultValue;
   }
   return VPinballLib::VPinballLib::Instance().LoadValueFloat(pSectionName, pKey, defaultValue);
}

VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue)
{
   if (!pSectionName || !pKey) {
      return pDefaultValue;
   }
   thread_local string value;
   value = VPinballLib::VPinballLib::Instance().LoadValueString(pSectionName, pKey, pDefaultValue);
   return value.c_str();
}

VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value)
{
   if (!pSectionName || !pKey) {
      PLOGE.printf("VPinballSaveValueInt: Null parameter (section=%p, key=%p)", pSectionName, pKey);
      return;
   }
   VPinballLib::VPinballLib::Instance().SaveValueInt(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value)
{
   if (!pSectionName || !pKey) {
      PLOGE.printf("VPinballSaveValueFloat: Null parameter (section=%p, key=%p)", pSectionName, pKey);
      return;
   }
   VPinballLib::VPinballLib::Instance().SaveValueFloat(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue)
{
   if (!pSectionName || !pKey || !pValue) {
      PLOGE.printf("VPinballSaveValueString: Null parameter (section=%p, key=%p, value=%p)", pSectionName, pKey, pValue);
      return;
   }
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
   if (!pUuid) {
      PLOGE.printf("VPinballLoad: Null UUID parameter");
      return VPINBALL_STATUS_FAILURE;
   }
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
   if (!pPath) {
      return false;
   }
   string path(pPath);
   return VPinballLib::VPinballLib::Instance().FileExists(path);
}

VPINBALLAPI bool VPinballDeleteFile(const char* pPath)
{
   if (!pPath) {
      PLOGE.printf("VPinballDeleteFile: Null path parameter");
      return false;
   }
   string path(pPath);
   return VPinballLib::VPinballLib::Instance().DeleteFile(path);
}

VPINBALLAPI bool VPinballCopyFile(const char* pSourcePath, const char* pDestPath)
{
   if (!pSourcePath || !pDestPath) {
      PLOGE.printf("VPinballCopyFile: Null parameter (source=%p, dest=%p)", pSourcePath, pDestPath);
      return false;
   }
   string sourcePath(pSourcePath);
   string destPath(pDestPath);
   return VPinballLib::FileSystem::CopyFile(sourcePath, destPath);
}

VPINBALLAPI const char* VPinballPrepareFileForViewing(const char* pPath)
{
   if (!pPath) {
      return nullptr;
   }
   thread_local string viewPath;
   string path(pPath);
   viewPath = VPinballLib::VPinballLib::Instance().PrepareFileForViewing(path);
   return viewPath.c_str();
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
   if (!pUuid || !pNewName) {
      PLOGE.printf("VPinballRenameTable: Null parameter (uuid=%p, newName=%p)", pUuid, pNewName);
      return VPINBALL_STATUS_FAILURE;
   }
   string uuid(pUuid);
   string newName(pNewName);
   return VPinballLib::VPinballLib::Instance().RenameTable(uuid, newName);
}

VPINBALLAPI VPINBALL_STATUS VPinballDeleteTable(const char* pUuid)
{
   if (!pUuid) {
      PLOGE.printf("VPinballDeleteTable: Null UUID parameter");
      return VPINBALL_STATUS_FAILURE;
   }
   string uuid(pUuid);
   return VPinballLib::VPinballLib::Instance().DeleteTable(uuid);
}

VPINBALLAPI VPINBALL_STATUS VPinballImportTable(const char* pSourceFile)
{
   if (!pSourceFile) {
      PLOGE.printf("VPinballImportTable: Null source file parameter");
      return VPINBALL_STATUS_FAILURE;
   }
   string sourceFile(pSourceFile);
   return VPinballLib::VPinballLib::Instance().ImportTable(sourceFile);
}

VPINBALLAPI VPINBALL_STATUS VPinballSetTableImage(const char* pUuid, const char* pImagePath)
{
   if (!pUuid) {
      PLOGE.printf("VPinballSetTableImage: Null UUID parameter");
      return VPINBALL_STATUS_FAILURE;
   }
   string uuid(pUuid);
   string imagePath(pImagePath ? pImagePath : "");
   return VPinballLib::VPinballLib::Instance().SetTableImage(uuid, imagePath);
}

VPINBALLAPI const char* VPinballExportTable(const char* pUuid)
{
   if (!pUuid) {
      PLOGE.printf("VPinballExportTable: Null UUID parameter");
      return nullptr;
   }
   string uuid(pUuid);
   thread_local string exportPath;
   exportPath = VPinballLib::VPinballLib::Instance().ExportTable(uuid);
   return !exportPath.empty() ? exportPath.c_str() : nullptr;
}
