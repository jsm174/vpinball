// license:GPLv3+

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cassert>
#include <cstdint>
#include <tchar.h>
#include <string>
#include <chrono>

#include "MsgPlugin.h"
#include "ControllerPlugin.h"

///////////////////////////////////////////////////////////////////////////////
// PinUp Player DMD events plugin
//
// Forward DMD changes to dmddevicePUP64/dmddevicePUP.dll (which is closed source,
// windows only).
//
// This plugin requires dmddevicePUP64/dmddevicePUP.dll to be placed in its
// directory. Since these dll are closed source, they are not distributed
// with the plugin and must be obtained from PinUp distribution separately.
//
// For some strange reason, PinUp does not do its matching on the PinMame DMD but on a 
// stretched to fit 128x32 frame, shaded using 0xFF4500 color (C# Colors.OrangeRed), 
// with non linear shading for 2 bitplane frames.

namespace PinUpEvents {

MsgPluginAPI* msgApi = nullptr;
uint32_t endpointId;
static unsigned int onGameStartId, onGameEndId, onSerumTriggerId, getDmdSrcId;

static HMODULE dmdDevicePupDll = nullptr;
static unsigned int lastFrameId = 0;
static DisplaySrcId dmdId;

typedef int (*pup_open)();
typedef void (*pup_setGameName)(const char* cName, int len);
typedef void (*pup_trigger)(unsigned short triggerId);
typedef void (*pup_renderRGB24)(unsigned short width, unsigned short height, uint8_t* frame);
typedef int (*pup_close)();

static pup_open pupOpen;
static pup_setGameName pupSetGameName;
static pup_trigger pupTrigger;
static pup_renderRGB24 pupRender_RGB24;
static pup_close pupClose;

static uint8_t rgbFrame[128 * 32 * 3];
static uint8_t palette4[4 * 3];
static uint8_t palette16[16 * 3];

// Introduced in Windows 7 (not supported in Windows XP)
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif

// Introduced in Windows 7 (not supported in Windows XP)
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif

// Called every 1/60s to process at least one frame every 16ms
void onUpdateDMD(void* userData)
{
   if (dmdDevicePupDll == nullptr || dmdId.id.id == 0)
      return;

   uint8_t* palette;
   if (dmdId.identifyFormat == CTLPI_DISPLAY_ID_FORMAT_BITPLANE2)
      palette = palette4;
   else if (dmdId.identifyFormat == CTLPI_DISPLAY_ID_FORMAT_BITPLANE4)
      palette = palette16;
   else
      return;

   msgApi->RunOnMainThread(1. / 60., onUpdateDMD, nullptr);
   
   DisplayFrame frame = dmdId.GetIdentifyFrame(dmdId.id);

   if (frame.frameId == lastFrameId)
      return;

   lastFrameId = frame.frameId;
   if (dmdId.width == 128 && dmdId.height == 32)
   {
      for (unsigned int i = 0; i < 128 * 32; i++)
         memcpy(&rgbFrame[i * 3], &palette[frame.frame[i] * 3], 3);
   }
   else if (dmdId.width == 128 && dmdId.height < 32)
   {
      const unsigned int ofsY = ((32 - dmdId.height) / 2) * 128;
      for (unsigned int i = 0; i < 128 * 16; i++)
         memcpy(&rgbFrame[(ofsY + i) * 3], &palette[frame.frame[i] * 3], 3);
   }
   else if (dmdId.width <= 256 && dmdId.height == 64)
   {
      // Resize with a triangle filter to mimic what original implementation in Freezy's DmdExt (https://github.com/freezy/dmd-extensions)
      // does, that is to say:
      // - convert from luminance to RGB (with hue = 0, sat = 1)
      // - resize using Windows 8.1 API which in turn uses IWICBitmapScaler with Fant interpolation mode (hence the triangle filter)
      // - convert back from RGB to HSL and send luminance to PinUp
      //
      // Some references regarding Fant scaling:
      // - https://github.com/sarnold/urt/blob/master/tools/fant.c
      // - https://photosauce.net/blog/post/examining-iwicbitmapscaler-and-the-wicbitmapinterpolationmode-values
      //
      // The Baywatch Pup pack was used to validate this (the filter is still a guess since Windows code is not available)
      const unsigned int ofsX = (128 - (dmdId.width / 2)) / 2;
      for (unsigned int y = 0; y < 32; y++)
      {
         for (unsigned int x = 0; x < dmdId.width / 2; x++)
         {
            float lum = 0., sum = 0.;
            constexpr int radius = 1;
            for (int dx = 1 - radius; dx <= radius; dx++)
            {
               for (int dy = 1 - radius; dy <= radius; dy++)
               {
                  const int px = x * 2 + dx;
                  const int py = y * 2 + dy;
                  const float weight = radius * radius - fabsf(dx - 0.5f) * fabsf(dy - 0.5f);
                  if (/*px >= 0 &&*/ static_cast<unsigned int>(px) < dmdId.width
                     && /*py >= 0 &&*/ static_cast<unsigned int>(py) < dmdId.height) // unsigned int tests include the >= 0 ones
                     lum += frame.frame[py * dmdId.width + px] * weight;
                  sum += weight;
               }
            }
            const int l = (int)roundf(lum / sum);
            memcpy(&rgbFrame[(y * 128 + ofsX + x) * 3], &palette[l * 3], 3);
         }
      }
   }
   else
   {
      // Unsupported DMD format (would need to implement a dedicated stretch fit, matching what is used elsewhere)
      return;
   }

   pupRender_RGB24(128, 32, rgbFrame);
}

// Broadcasted by Serum plugin when frame triggers are identified
void onSerumTrigger(const unsigned int eventId, void* userData, void* eventData)
{
   unsigned int* trigger = static_cast<unsigned int*>(eventData);
   if (dmdDevicePupDll && pupTrigger)
      pupTrigger(*trigger);
}

void onGameStart(const unsigned int eventId, void* userData, void* eventData)
{
   // Select main DMD from the list of DMD sources (this is not fully clean as this rely on the fact that PinMAME sends the game start event after declaring its displays, we should listen for display source changes)
   bool mainDMDFound = false;
   GetDisplaySrcMsg getSrcMsg = { 1024, 0, new DisplaySrcId[1024] };
   msgApi->BroadcastMsg(endpointId, getDmdSrcId, &getSrcMsg);
   for (unsigned int i = 0; !mainDMDFound && (i < getSrcMsg.count); i++)
   {
      if (getSrcMsg.entries[i].width >= 128) // First DMD with a width of 128 or more is considered as the main one
      {
         dmdId = getSrcMsg.entries[i];
         mainDMDFound = true;
      }
   }
   delete[] getSrcMsg.entries;
   if (!mainDMDFound)
      return;

   // Find PUP interface dll and set it up
   const CtlOnGameStartMsg* msg = static_cast<const CtlOnGameStartMsg*>(eventData);
   assert(msg != nullptr && msg->gameId != nullptr);
   HMODULE hm = nullptr;
   if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, _T("PluginLoad"), &hm) == 0)
      return;
   TCHAR path[MAX_PATH];
   if (GetModuleFileName(hm, path, MAX_PATH) == 0)
      return;
   #ifdef _UNICODE
      std::wstring fullpath(path);
   #else
      std::string fullpath(path);
   #endif
   fullpath = fullpath.substr(0, fullpath.find_last_of(_T("\\/"))) + _T('\\');
   #if (INTPTR_MAX == INT32_MAX)
      fullpath += _T("dmddevicePUP.DLL");
   #else
      fullpath += _T("dmddevicePUP64.DLL");
   #endif
   dmdDevicePupDll = LoadLibraryEx(fullpath.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
   if (dmdDevicePupDll == nullptr)
      return;
   pupOpen = reinterpret_cast<pup_open>(GetProcAddress(dmdDevicePupDll, "Open"));
   pupSetGameName = reinterpret_cast<pup_setGameName>(GetProcAddress(dmdDevicePupDll, "SetGameName"));
   pupTrigger = reinterpret_cast<pup_trigger>(GetProcAddress(dmdDevicePupDll, "PuP_Trigger"));
   pupRender_RGB24 = reinterpret_cast<pup_renderRGB24>(GetProcAddress(dmdDevicePupDll, "Render_RGB24"));
   pupClose = reinterpret_cast<pup_close>(GetProcAddress(dmdDevicePupDll, "Close"));
   if ((pupOpen == nullptr) || (pupSetGameName == nullptr) || (pupRender_RGB24 == nullptr) || (pupClose == nullptr))
   {
      FreeLibrary(dmdDevicePupDll);
      dmdDevicePupDll = nullptr;
      return;
   }
   pupOpen();
   pupSetGameName(msg->gameId, static_cast<int>(strlen(msg->gameId)));
   lastFrameId = 123456;
   memset(rgbFrame, 0, sizeof(rgbFrame));
   onUpdateDMD(nullptr);
}

void onGameEnd(const unsigned int eventId, void* userData, void* eventData)
{
   dmdId = { 0 };
   if (dmdDevicePupDll)
   {
      pupClose();
      FreeLibrary(dmdDevicePupDll);
      dmdDevicePupDll = nullptr;
   }
}

}

using namespace PinUpEvents;

MSGPI_EXPORT void MSGPIAPI PinUpEventsPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = const_cast<MsgPluginAPI*>(api);
   endpointId = sessionId; 
   getDmdSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG);
   msgApi->SubscribeMsg(sessionId, onGameStartId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_START), onGameStart, nullptr);
   msgApi->SubscribeMsg(sessionId, onGameEndId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_END), onGameEnd, nullptr);
   msgApi->SubscribeMsg(sessionId, onSerumTriggerId = msgApi->GetMsgID("Serum", "OnDmdTrigger"), onSerumTrigger, nullptr);
   dmdDevicePupDll = nullptr;
   constexpr unsigned int mapping4[] = { 0, 1, 4, 15 };
   for (int i = 0; i < 4; i++)
   {
      palette4[i * 3 + 0] = (mapping4[i] * 0xFF) / 0xF; // R
      palette4[i * 3 + 1] = (mapping4[i] * 0x45) / 0xF; // G
      palette4[i * 3 + 2] = (mapping4[i] * 0x00) / 0xF; // B
   }
   for (int i = 0; i < 16; i++)
   {
      palette16[i * 3 + 0] = (i * 0xFF) / 0xF; // R
      palette16[i * 3 + 1] = (i * 0x45) / 0xF; // G
      palette16[i * 3 + 2] = (i * 0x00) / 0xF; // B
   }
}

MSGPI_EXPORT void MSGPIAPI PinUpEventsPluginUnload()
{
   msgApi->UnsubscribeMsg(onGameStartId, onGameStart);
   msgApi->UnsubscribeMsg(onGameEndId, onGameEnd);
   msgApi->ReleaseMsgID(onGameStartId);
   msgApi->ReleaseMsgID(onGameEndId);
   msgApi->ReleaseMsgID(onSerumTriggerId);
   msgApi->ReleaseMsgID(getDmdSrcId);
   msgApi = nullptr;
}
