#pragma once

#include <cassert>

#include <string>
using std::string;
using namespace std::string_literals;

#include <vector>
using std::vector;

#include <sstream>
#include <iomanip>

#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <cstdlib>
#include <memory>
#include <functional>

// Shared logging
#include "LoggingPlugin.h"

// Scriptable API
#include "ScriptablePlugin.h"

// VPX main API
#include "VPXPlugin.h"

namespace B2SLegacy
{

// Extracted enums from b2s_i.h that we need for the plugin
typedef enum {
    eDualMode_Both = 0,
    eDualMode_Authentic = 1,
    eDualMode_Fantasy = 2
} eDualMode;

typedef enum {
    eDualMode_2_NotSet = 0,
    eDualMode_2_Authentic = 1,
    eDualMode_2_Fantasy = 2
} eDualMode_2;

typedef enum {
    eRomIDType_NotDefined = 0,
    eRomIDType_Lamp = 1,
    eRomIDType_Solenoid = 2,
    eRomIDType_GIString = 3,
    eRomIDType_Mech = 4
} eRomIDType;

typedef enum {
    eDMDTypes_Standard = 0,
    eDMDTypes_TwoMonitorSetup = 1,
    eDMDTypes_ThreeMonitorSetup = 2,
    eDMDTypes_Hidden = 3,
    eDMDType_B2SAlwaysOnSecondMonitor = 1,
    eDMDType_B2SAlwaysOnThirdMonitor = 2,
    eDMDType_B2SOnSecondOrThirdMonitor = 3
} eDMDTypes;

typedef enum {
    eLEDTypes_Undefined = 0,
    eLEDTypes_Rendered = 1,
    eLEDTypes_Dream7 = 2
} eLEDTypes;

typedef enum {
    B2SSettingsCheckedState_Unchecked = 0,
    B2SSettingsCheckedState_Checked = 1,
    B2SSettingsCheckedState_Indeterminate = 2
} B2SSettingsCheckedState;

typedef enum {
    eType_Undefined = 0,
    eType_ImageCollectionAtForm = 1,
    eType_ImageCollectionAtPictureBox = 2,
    eType_PictureBoxCollection = 3
} eType;

typedef enum {
    eType_2_NotDefined = 0,
    eType_2_Undefined = 0,
    eType_2_ImageCollectionAtForm = 1,
    eType_2_ImageCollectionAtPictureBox = 2,
    eType_2_PictureBoxCollection = 3,
    eType_2_OnBackglass = 4,
    eType_2_OnDMD = 5
} eType_2;

typedef enum {
    eScoreType_Standard = 0,
    eScoreType_Player = 1,
    eScoreType_Credits = 2,
    eScoreType_NotUsed = 3
} eScoreType;

typedef enum {
    eLEDType_Undefined = 0,
    eLEDType_Standard = 1,
    eLEDType_Segment = 2,
    eLEDType_LED8 = 3,
    eLEDType_LED10 = 4,
    eLEDType_LED14 = 5,
    eLEDType_LED16 = 6
} eLEDType;

typedef enum {
    eSnippitRotationStopBehaviour_Undefined = 0,
    eSnippitRotationStopBehaviour_StopImmediately = 1,
    eSnippitRotationStopBehaviour_RunToEnd = 2,
    eSnippitRotationStopBehaviour_SpinOff = 3,
    eSnippitRotationStopBehaviour_RunAnimationTillEnd = 2,
    eSnippitRotationStopBehaviour_RunAnimationToFirstStep = 3
} eSnippitRotationStopBehaviour;

typedef enum {
    SegmentNumberType_7Seg = 0,
    SegmentNumberType_10Seg = 1,
    SegmentNumberType_14Seg = 2,
    SegmentNumberType_16Seg = 3,
    SegmentNumberType_SevenSegment = 0,
    SegmentNumberType_TenSegment = 1,
    SegmentNumberType_FourteenSegment = 2,
    SegmentNumberType_SixteenSegment = 3
} SegmentNumberType;

typedef enum {
    ScaleMode_Uniform = 0,
    ScaleMode_Stretch = 1,
    ScaleMode_Manual = 2,
    ScaleMode_Zoom = 3
} ScaleMode;

typedef enum {
    eDMDViewMode_NotDefined = 0,
    eDMDViewMode_NoDMD = 1,
    eDMDViewMode_Standard = 2,
    eDMDViewMode_Large = 3,
    eDMDViewMode_ShowDMD = 4,
    eDMDViewMode_ShowDMDOnlyAtDefaultLocation = 5,
    eDMDViewMode_DoNotShowDMDAtDefaultLocation = 6
} eDMDViewMode;

typedef enum {
    eControlType_Undefined = 0,
    eControlType_LED = 1,
    eControlType_Reel = 2,
    eControlType_Display = 3,
    eControlType_Dream7LEDDisplay = 4,
    eControlType_LEDDisplay = 5,
    eControlType_ReelDisplay = 6,
    eControlType_LEDBox = 7,
    eControlType_ReelBox = 8
} eControlType;

typedef enum {
    eLightsStateAtAnimationStart_Undefined = 0,
    eLightsStateAtAnimationStart_InvolvedLightsOff = 1,
    eLightsStateAtAnimationStart_InvolvedLightsOn = 2,
    eLightsStateAtAnimationStart_LightsOff = 3,
    eLightsStateAtAnimationStart_NoChange = 4
} eLightsStateAtAnimationStart;

typedef enum {
    eLightsStateAtAnimationEnd_Undefined = 0,
    eLightsStateAtAnimationEnd_InvolvedLightsOff = 1,
    eLightsStateAtAnimationEnd_InvolvedLightsOn = 2,
    eLightsStateAtAnimationEnd_LightsReseted = 3,
    eLightsStateAtAnimationEnd_NoChange = 4
} eLightsStateAtAnimationEnd;

typedef enum {
    eAnimationStopBehaviour_Undefined = 0,
    eAnimationStopBehaviour_StopImmediatelly = 1,
    eAnimationStopBehaviour_RunAnimationTillEnd = 2,
    eAnimationStopBehaviour_RunAnimationToFirstStep = 3
} eAnimationStopBehaviour;

typedef enum {
    ePictureBoxType_StandardImage = 0,
    ePictureBoxType_SelfRotatingImage = 1,
    ePictureBoxType_MechRotatingImage = 2
} ePictureBoxType;

typedef enum {
    eSnippitRotationDirection_Clockwise = 0,
    eSnippitRotationDirection_AntiClockwise = 1
} eSnippitRotationDirection;

#define LOGD LPI_LOGD
#define LOGI LPI_LOGI
#define LOGE LPI_LOGE

PSC_USE_ERROR();

#ifdef _MSC_VER
#define PATH_SEPARATOR_CHAR '\\'
#else
#define PATH_SEPARATOR_CHAR '/'
#endif

// Rendering provided through plugin messages
extern void SetVPXAPI(VPXPluginAPI* api);
extern VPXTexture CreateTexture(uint8_t *rawData, int size);
extern VPXTextureInfo* GetTextureInfo(VPXTexture texture);
extern void DeleteTexture(VPXTexture texture);

// SDL includes
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

// Forward declarations for standalone dependencies
typedef uint32_t OLE_COLOR;
typedef uint8_t UINT8;

// Windows color function replacements for cross-platform compatibility
#define RGB(r,g,b) ((OLE_COLOR)(((uint8_t)(r)|((uint16_t)((uint8_t)(g))<<8))|(((uint32_t)(uint8_t)(b))<<16)))
#define GetRValue(rgb) ((uint8_t)(rgb))
#define GetGValue(rgb) ((uint8_t)(((uint16_t)(rgb)) >> 8))
#define GetBValue(rgb) ((uint8_t)((rgb)>>16))

// Windows VARIANT constants
#define VARIANT_TRUE (-1)
#define VARIANT_FALSE (0)

// B2S version constants from B2SVersionInfo.h
#include "classes/B2SVersionInfo.h"

// PSC handles arrays properly - no need for COM stubs

// Forward declarations for B2S classes
class B2SData;
class B2SAnimation;
class B2SReelDisplay;
class B2SBaseBox;
class Dream7Display;
class LEDDisplayDigitLocation;
class B2SLEDBox;
class B2SPictureBox;
class B2SSettings;
class B2SScreen;
class FormDMD;
class Settings;

// Timer forward declaration - real implementation in Timer.h
class Timer;

// Output stub classes for compatibility with g_pplayer references
class BackglassOutput {
public:
   int GetWidth() const { return 1920; }  // Default size
   int GetHeight() const { return 1080; }
   void GetPos(int& x, int& y) const { x = 0; y = 0; }
};

class ScoreviewOutput {
public:
   int GetWidth() const { return 512; }   // Default DMD size
   int GetHeight() const { return 128; }
   void GetPos(int& x, int& y) const { x = 0; y = 0; }
};

// Settings compatibility class
class Settings {
public:
   enum SettingsType { Standalone = 0 };
   template<typename T>
   T LoadValueWithDefault(SettingsType type, const string& key, T defaultValue) const {
      return defaultValue;  // Return default for plugin context
   }
};

// Table stub class
class Table {
public:
   Settings m_settings;
};

// Player stub class to replace g_pplayer references
class Player {
public:
   BackglassOutput m_backglassOutput;
   ScoreviewOutput m_scoreviewOutput;
   Table* m_ptable;
   
   Player() {
      m_ptable = new Table();
   }
   ~Player() {
      delete m_ptable;
   }
};

// Global player instance for plugin context
extern Player* g_pplayer;

// Logging stub for PLOGI - remove if not needed
class LoggerStub {
public:
   void printf(const char* fmt, ...) {}
   LoggerStub& operator<<(const char* msg) { return *this; }
};
extern LoggerStub PLOGI;
extern LoggerStub PLOGW;
extern LoggerStub PLOGE;

// VPinMAME stub class that forwards to PinMAME plugin
class VPinMAMEController {
public:
   VPinMAMEController() {}
   ~VPinMAMEController() {}
   
   // Stub methods - in a real implementation these would forward to PinMAME plugin
   void Stop() {}
   void put_Switch(int switchNumber, int value) {}
   // Add other methods as needed when we encounter compilation errors
};

// Helper functions for B2SLegacy plugin
string find_case_insensitive_file_path(const string &szPath);
vector<unsigned char> base64_decode(const string &encoded_string);
bool string_starts_with_case_insensitive(const string& str, const string& prefix);
int string_to_int(const string& str, int defaultValue);
string TitleAndPathFromFilename(const string& filename);
bool is_string_numeric(const string& str);

// Min/max functions for compatibility
template<typename T>
inline T min(T a, T b) { return (a < b) ? a : b; }
template<typename T>
inline T max(T a, T b) { return (a > b) ? a : b; }

}