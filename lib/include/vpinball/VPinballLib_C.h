#pragma once

#ifdef _MSC_VER
#define VPINBALLAPI extern "C" __declspec(dllexport)
#define VPINBALLCALLBACK __stdcall
#else
#define VPINBALLAPI extern "C" __attribute__((visibility("default")))
#define VPINBALLCALLBACK
#endif

// Enums

typedef enum {
   VPINBALL_LOG_LEVEL_DEBUG,
   VPINBALL_LOG_LEVEL_INFO,
   VPINBALL_LOG_LEVEL_WARN,
   VPINBALL_LOG_LEVEL_ERROR
} VPINBALL_LOG_LEVEL;

typedef enum {
   VPINBALL_STATUS_SUCCESS,
   VPINBALL_STATUS_FAILURE
} VPINBALL_STATUS;

typedef enum {
   VPINBALL_SCRIPT_ERROR_TYPE_COMPILE,
   VPINBALL_SCRIPT_ERROR_TYPE_RUNTIME
} VPINBALL_SCRIPT_ERROR_TYPE;

typedef enum {
   VPINBALL_EVENT_ARCHIVE_UNCOMPRESSING,
   VPINBALL_EVENT_ARCHIVE_COMPRESSING,
   VPINBALL_EVENT_LOADING_ITEMS,
   VPINBALL_EVENT_LOADING_SOUNDS,
   VPINBALL_EVENT_LOADING_IMAGES,
   VPINBALL_EVENT_LOADING_FONTS,
   VPINBALL_EVENT_LOADING_COLLECTIONS,
   VPINBALL_EVENT_PRERENDERING,
   VPINBALL_EVENT_PLAYER_STARTED,
   VPINBALL_EVENT_RUMBLE,
   VPINBALL_EVENT_SCRIPT_ERROR,
   VPINBALL_EVENT_PLAYER_CLOSED,
   VPINBALL_EVENT_WEB_SERVER,
   VPINBALL_EVENT_TABLE_LIST_UPDATED,
   VPINBALL_EVENT_COPYING_TABLE
} VPINBALL_EVENT;

// Callbacks

typedef void (*VPinballEventCallback)(VPINBALL_EVENT, const char*);

// Functions

VPINBALLAPI const char* VPinballGetVersionStringFull();
VPINBALLAPI void VPinballInit(VPinballEventCallback callback);
VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* message);
VPINBALLAPI void VPinballResetLog();

// Settings

VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue);
VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue);
VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue);
VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value);
VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value);
VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue);
VPINBALLAPI VPINBALL_STATUS VPinballResetIni();

// Web Server

VPINBALLAPI void VPinballUpdateWebServer();

// Table Management

VPINBALLAPI const char* VPinballGetTables();
VPINBALLAPI const char* VPinballGetTablesPath();
VPINBALLAPI VPINBALL_STATUS VPinballRefreshTables();
VPINBALLAPI VPINBALL_STATUS VPinballImportTable(const char* pSourceFile);
VPINBALLAPI const char* VPinballExportTable(const char* pUuid);
VPINBALLAPI VPINBALL_STATUS VPinballRenameTable(const char* pUuid, const char* pNewName);
VPINBALLAPI VPINBALL_STATUS VPinballSetTableImage(const char* pUuid, const char* pImagePath);
VPINBALLAPI VPINBALL_STATUS VPinballDeleteTable(const char* pUuid);

// Player

VPINBALLAPI VPINBALL_STATUS VPinballLoadTable(const char* pUuid);
VPINBALLAPI VPINBALL_STATUS VPinballExtractTableScript();
VPINBALLAPI VPINBALL_STATUS VPinballPlay();
VPINBALLAPI VPINBALL_STATUS VPinballStop();

// File Management

VPINBALLAPI bool VPinballFileExists(const char* pPath);
VPINBALLAPI bool VPinballDeleteFile(const char* pPath);
VPINBALLAPI bool VPinballCopyFile(const char* pSourcePath, const char* pDestPath);
VPINBALLAPI const char* VPinballStageFile(const char* pPath);