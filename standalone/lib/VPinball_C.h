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
   VPINBALL_EVENT_PLAY,
   VPINBALL_EVENT_CREATING_PLAYER,
   VPINBALL_EVENT_PRERENDERING,
   VPINBALL_EVENT_PLAYER_STARTED,
   VPINBALL_EVENT_RUMBLE,
   VPINBALL_EVENT_SCRIPT_ERROR,
   VPINBALL_EVENT_PLAYER_CLOSING,
   VPINBALL_EVENT_PLAYER_CLOSED,
   VPINBALL_EVENT_STOPPED,
   VPINBALL_EVENT_WEB_SERVER,
   VPINBALL_EVENT_CAPTURE_SCREENSHOT,
   VPINBALL_EVENT_TABLE_LIST,
   VPINBALL_EVENT_TABLE_IMPORT,
   VPINBALL_EVENT_TABLE_RENAME,
   VPINBALL_EVENT_TABLE_DELETE,
   VPINBALL_EVENT_TABLE_SCAN,
   VPINBALL_EVENT_TABLE_SCAN_COMPLETE,
   VPINBALL_EVENT_TABLE_ADDED,
   VPINBALL_EVENT_TABLE_UPDATED,
   VPINBALL_EVENT_TABLE_REMOVED,
   VPINBALL_EVENT_TABLES_JSON_GENERATED
} VPINBALL_EVENT;

// Callbacks

typedef void* (*VPinballEventCallback)(VPINBALL_EVENT, const char*, void*);

// Functions

VPINBALLAPI const char* VPinballGetVersionStringFull();
VPINBALLAPI void VPinballInit(VPinballEventCallback callback);
VPINBALLAPI void VPinballSetupEventCallback();
VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* pMessage);
VPINBALLAPI void VPinballResetLog();
VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue);
VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue);
VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue);
VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value);
VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value);
VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue);
VPINBALLAPI void VPinballUpdateWebServer();
VPINBALLAPI void VPinballSetWebServerUpdated();
VPINBALLAPI VPINBALL_STATUS VPinballResetIni();
VPINBALLAPI VPINBALL_STATUS VPinballLoad(const char* pSource);
VPINBALLAPI VPINBALL_STATUS VPinballExtractScript(const char* pSource);
VPINBALLAPI VPINBALL_STATUS VPinballPlay();
VPINBALLAPI VPINBALL_STATUS VPinballStop();
VPINBALLAPI void VPinballToggleFPS();
VPINBALLAPI void VPinballCaptureScreenshot(const char* pFilename);

VPINBALLAPI VPINBALL_STATUS VPinballRefreshTables();
VPINBALLAPI char* VPinballGetVPXTables();
VPINBALLAPI char* VPinballGetVPXTable(const char* uuid);
VPINBALLAPI VPINBALL_STATUS VPinballAddVPXTable(const char* pFilePath);
VPINBALLAPI VPINBALL_STATUS VPinballRemoveVPXTable(const char* pUuid);
VPINBALLAPI VPINBALL_STATUS VPinballRenameVPXTable(const char* pUuid, const char* pNewName);
VPINBALLAPI VPINBALL_STATUS VPinballImportTableFile(const char* pSourceFile);
VPINBALLAPI VPINBALL_STATUS VPinballSetTableArtwork(const char* pUuid, const char* pArtworkPath);
VPINBALLAPI const char* VPinballGetTablesPath();
VPINBALLAPI const char* VPinballExportTable(const char* pUuid);
VPINBALLAPI void VPinballFreeString(char* jsonString);