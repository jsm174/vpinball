#pragma once

#ifdef _MSC_VER
#define VPINBALLAPI extern "C" __declspec(dllexport)
#define VPINBALLCALLBACK __stdcall
#else
#define VPINBALLAPI extern "C" __attribute__((visibility("default")))
#define VPINBALLCALLBACK
#endif

// VPINBALL ENUM
typedef enum {
	VPINBALL_LOG_LEVEL_DEBUG = 0,
	VPINBALL_LOG_LEVEL_INFO = 1,
	VPINBALL_LOG_LEVEL_ERROR = 2
} VPINBALL_LOG_LEVEL;

typedef enum {
	VPINBALL_STATUS_OK = 0,
	VPINBALL_STATUS_ERROR = 1
} VPINBALL_STATUS;

typedef enum {
	VPINBALL_STATE_IDLE = 0,
	VPINBALL_STATE_EXTRACTING = 1,
	VPINBALL_STATE_COMPRESSING = 2,
	VPINBALL_STATE_LOADING_ITEMS = 3,
	VPINBALL_STATE_LOADING_SOUNDS = 4,
	VPINBALL_STATE_LOADING_IMAGES = 5,
	VPINBALL_STATE_LOADING_FONTS = 6,
	VPINBALL_STATE_LOADING_COLLECTIONS = 7,
	VPINBALL_STATE_PRERENDERING_STATIC_PARTS = 8,
	VPINBALL_STATE_TABLE_WINDOW_CREATED = 9,
	VPINBALL_STATE_WEBSERVER_STARTED = 10,
	VPINBALL_STATE_WEBSERVER_STOPPED = 11,
	VPINBALL_STATE_STARTUP_DONE = 12,
	VPINBALL_STATE_RUMBLE = 13,
	VPINBALL_STATE_STOPPED = 14,
} VPINBALL_STATE;

typedef enum {
	VPINBALL_SETTINGS_SECTION_STANDALONE = 2,
	VPINBALL_SETTINGS_SECTION_PLAYER = 3
} VPINBALL_SETTINGS_SECTION;

typedef struct {
    Uint16 low_frequency_rumble;
    Uint16 high_frequency_rumble;
    Uint32 duration_ms;
} VPinballRumbleData;

extern "C" void* g_pMetalLayer;

// Internal Functions

void VPinballSetState(VPINBALL_STATE state, void* data);

// Callbacks

typedef void (*VPinballStateCallback)(VPINBALL_STATE, void*);

// Functions

VPINBALLAPI void VPinballInit();
VPINBALLAPI void VPinballResetWebServer();
VPINBALLAPI VPINBALL_STATUS VPinballResetIni();
VPINBALLAPI int VPinballLoadValueInt(VPINBALL_SETTINGS_SECTION section, const char* pKey, int defaultValue);
VPINBALLAPI int VPinballLoadValueFloat(VPINBALL_SETTINGS_SECTION section, const char* pKey, float defaultValue);
VPINBALLAPI const char* VPinballLoadValueString(VPINBALL_SETTINGS_SECTION section, const char* pKey, const char* pDefaultValue);
VPINBALLAPI void VPinballSaveValueInt(VPINBALL_SETTINGS_SECTION section, const char* pKey, int value);
VPINBALLAPI void VPinballSaveValueFloat(VPINBALL_SETTINGS_SECTION section, const char* pKey, float value);
VPINBALLAPI void VPinballSaveValueString(VPINBALL_SETTINGS_SECTION section, const char* pKey, const char* pValue);
VPINBALLAPI void VPinballSetMetalLayer(void* pMetalLayer);
VPINBALLAPI void VPinballSetStateCallback(VPinballStateCallback callback);
VPINBALLAPI const char* VPinballGetVersionStringFull();
VPINBALLAPI VPINBALL_STATUS VPinballExtract(const char* pSource);
VPINBALLAPI VPINBALL_STATUS VPinballCompress(const char* pSource, const char* pDestination);
VPINBALLAPI VPINBALL_STATUS VPinballPlay(const char* pTablePath);
VPINBALLAPI VPINBALL_STATUS VPinballStop();
