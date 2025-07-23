// license:GPLv3+

#include "common.h"
#include "ScriptablePlugin.h"
#include "LoggingPlugin.h"
#include "VPXPlugin.h"

#include "B2SServer.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin interface

static MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;
static ScriptablePluginAPI* scriptApi = nullptr;
static uint32_t endpointId = 0;

// PinMAME forwarding mechanism (similar to B2S plugin)
static ScriptClassDef* pinmameClassDef = nullptr;
static void* pinmameInstance = nullptr;  
static int pinmameMemberStartIndex = 0;

namespace B2SLegacy {

void MSGPIAPI ForwardPinMAMECall(void* me, int memberIndex, ScriptVariant* pArgs, ScriptVariant* pRet)
{
   assert(pinmameClassDef);
   if (pinmameClassDef == nullptr)
      return;
   if (pinmameInstance == nullptr)
      pinmameInstance = pinmameClassDef->CreateObject();
   const int index = memberIndex - pinmameMemberStartIndex;
   pinmameClassDef->members[index].Call(pinmameInstance, index, pArgs, pRet);
}

PSC_ERROR_IMPLEMENT(scriptApi);

LPI_IMPLEMENT

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Script interface

using namespace B2SLegacy;

PSC_CLASS_START(B2SServer)
      PSC_FUNCTION0(B2SServer, void, Dispose)
      PSC_PROP_R(B2SServer, string, B2SServerVersion)
      PSC_PROP_R(B2SServer, double, B2SBuildVersion)
      PSC_PROP_R(B2SServer, string, B2SServerDirectory)
      PSC_PROP_RW(B2SServer, string, GameName)
      PSC_PROP_R(B2SServer, string, ROMName)
      PSC_PROP_RW(B2SServer, string, B2SName)
      PSC_PROP_RW(B2SServer, string, TableName)
      PSC_PROP_W(B2SServer, string, WorkingDir)
      PSC_FUNCTION1(B2SServer, void, SetPath, string)
      PSC_FUNCTION1(B2SServer, void, GetGames, string)
      PSC_FUNCTION0(B2SServer, void, GetSettings)
      PSC_PROP_R(B2SServer, bool, Running)
      PSC_FUNCTION1(B2SServer, void, SetTimeFence, double)
      PSC_PROP_RW(B2SServer, bool, Pause)
      PSC_PROP_R(B2SServer, string, Version)
      PSC_PROP_R(B2SServer, string, VPMBuildVersion)
      PSC_FUNCTION1(B2SServer, void, Run, int)
      PSC_FUNCTION0(B2SServer, void, Stop)
      PSC_PROP_RW(B2SServer, bool, LaunchBackglass)
      PSC_PROP_RW(B2SServer, string, SplashInfoLine)
      PSC_PROP_RW(B2SServer, bool, ShowFrame)
      PSC_PROP_RW(B2SServer, bool, ShowTitle)
      PSC_PROP_RW(B2SServer, bool, ShowDMDOnly)
      PSC_PROP_RW(B2SServer, bool, ShowPinDMD)
      PSC_PROP_RW(B2SServer, bool, LockDisplay)
      PSC_PROP_RW(B2SServer, bool, DoubleSize)
      PSC_PROP_RW(B2SServer, bool, Hidden)
      PSC_FUNCTION3(B2SServer, void, SetDisplayPosition, int, int, int)
      PSC_FUNCTION1(B2SServer, void, ShowOptsDialog, int)
      PSC_FUNCTION1(B2SServer, void, ShowPathesDialog, int)
      PSC_FUNCTION1(B2SServer, void, ShowAboutDialog, int)
      PSC_FUNCTION2(B2SServer, void, CheckROMS, bool, int)
      PSC_PROP_RW(B2SServer, bool, PuPHide)
      PSC_PROP_RW(B2SServer, bool, HandleKeyboard)
      PSC_PROP_RW(B2SServer, int16, HandleMechanics)
      PSC_FUNCTION0(B2SServer, void, GetChangedLamps)
      PSC_FUNCTION0(B2SServer, void, GetChangedSolenoids)
      PSC_FUNCTION0(B2SServer, void, GetChangedGIStrings)
      PSC_FUNCTION4(B2SServer, void, GetChangedLEDs, int, int, int, int)
      PSC_FUNCTION0(B2SServer, void, GetNewSoundCommands)
      PSC_PROP_R_ARRAY1(B2SServer, bool, Lamp, int)
      PSC_PROP_R_ARRAY1(B2SServer, bool, Solenoid, int)
      PSC_PROP_R_ARRAY1(B2SServer, bool, GIString, int)
      PSC_PROP_RW_ARRAY1(B2SServer, bool, Switch, int)
      PSC_PROP_RW_ARRAY1(B2SServer, int, Mech, int)
      PSC_FUNCTION1(B2SServer, void, GetGetMech, int)
      PSC_PROP_RW_ARRAY1(B2SServer, int, Dip, int)
      PSC_PROP_RW_ARRAY1(B2SServer, int, SolMask, int)
      PSC_PROP_R(B2SServer, int, RawDmdWidth)
      PSC_PROP_R(B2SServer, int, RawDmdHeight)
      PSC_FUNCTION0(B2SServer, void, GetRawDmdPixels)
      PSC_FUNCTION0(B2SServer, void, GetRawDmdColoredPixels)
      PSC_FUNCTION0(B2SServer, void, GetChangedNVRAM)
      PSC_FUNCTION0(B2SServer, void, GetNVRAM)
      PSC_PROP_RW(B2SServer, int, SoundMode)
      PSC_FUNCTION2(B2SServer, void, B2SSetData, int, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetData, string, int)
      PSC_FUNCTION1(B2SServer, void, B2SPulseData, int)
      PSC_FUNCTION1(B2SServer, void, B2SPulseData, string)
      PSC_FUNCTION3(B2SServer, void, B2SSetPos, int, int, int)
      PSC_FUNCTION3(B2SServer, void, B2SSetPos, string, int, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetIllumination, string, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetLED, int, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetLED, int, string)
      PSC_FUNCTION2(B2SServer, void, B2SSetLEDDisplay, int, string)
      PSC_FUNCTION2(B2SServer, void, B2SSetReel, int, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetScore, int, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetScorePlayer, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScorePlayer1, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScorePlayer2, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScorePlayer3, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScorePlayer4, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScorePlayer5, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScorePlayer6, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetScoreDigit, int, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetScoreRollover, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScoreRolloverPlayer1, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScoreRolloverPlayer2, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScoreRolloverPlayer3, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetScoreRolloverPlayer4, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetCredits, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetCredits, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetPlayerUp, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetPlayerUp, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetCanPlay, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetCanPlay, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetBallInPlay, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetBallInPlay, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetTilt, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetTilt, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetMatch, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetMatch, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetGameOver, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetGameOver, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SSetShootAgain, int)
      PSC_FUNCTION2(B2SServer, void, B2SSetShootAgain, int, int)
      PSC_FUNCTION1(B2SServer, void, B2SStartAnimation, string)
      PSC_FUNCTION2(B2SServer, void, B2SStartAnimation, string, bool)
      PSC_FUNCTION1(B2SServer, void, B2SStartAnimationReverse, string)
      PSC_FUNCTION1(B2SServer, void, B2SStopAnimation, string)
      PSC_FUNCTION0(B2SServer, void, B2SStopAllAnimations)
      PSC_PROP_R_ARRAY1(B2SServer, bool, B2SIsAnimationRunning, string)
      PSC_FUNCTION1(B2SServer, void, StartAnimation, string)
      PSC_FUNCTION2(B2SServer, void, StartAnimation, string, bool)
      PSC_FUNCTION1(B2SServer, void, StopAnimation, string)
      PSC_FUNCTION0(B2SServer, void, B2SStartRotation)
      PSC_FUNCTION0(B2SServer, void, B2SStopRotation)
      PSC_FUNCTION0(B2SServer, void, B2SShowScoreDisplays)
      PSC_FUNCTION0(B2SServer, void, B2SHideScoreDisplays)
      PSC_FUNCTION1(B2SServer, void, B2SStartSound, string)
      PSC_FUNCTION1(B2SServer, void, B2SPlaySound, string)
      PSC_FUNCTION1(B2SServer, void, B2SStopSound, string)
      PSC_FUNCTION2(B2SServer, void, B2SMapSound, int, string)

      // PinMAME API mirroring inside Server (similar to B2S plugin)  
      if (pinmameClassDef)
      {
         pinmameMemberStartIndex = static_cast<int>(members.size());
         for (unsigned int i = 0; i < pinmameClassDef->nMembers; i++)
         {
            ScriptClassMemberDef member = pinmameClassDef->members[i];
            member.Call = B2SLegacy::ForwardPinMAMECall;
            members.push_back(member);
         }
      }
PSC_CLASS_END(B2SServer)

///////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin lifecycle

MSGPI_EXPORT void MSGPIAPI B2SLegacyPluginLoad(const uint32_t sessionId, MsgPluginAPI* api)
{
   endpointId = sessionId;
   msgApi = api;

   LPISetup(endpointId, msgApi);

   const unsigned int getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
   msgApi->BroadcastMsg(endpointId, getVpxApiId, &vpxApi);
   msgApi->ReleaseMsgID(getVpxApiId);
   
   // Set VPX API for texture functions
   SetVPXAPI(vpxApi);
   
   // Initialize global player instance for compatibility
   if (g_pplayer == nullptr) {
      g_pplayer = new Player();
   }
   
   // VPX API provides path functionality - no need for g_pvp stub

   const unsigned int getScriptApiId = msgApi->GetMsgID(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
   msgApi->BroadcastMsg(endpointId, getScriptApiId, &scriptApi);
   msgApi->ReleaseMsgID(getScriptApiId);

   if (scriptApi == nullptr) {
      LOGE("Failed to get script API");
      return;
   }

   // Get PinMAME class definition for forwarding
   pinmameClassDef = scriptApi->GetClassDef("Controller");

   auto regLambda = [&](ScriptClassDef* scd) { scriptApi->RegisterScriptClass(scd); };
   RegisterB2SServerSCD(regLambda);
   B2SServer_SCD->CreateObject = []()
   {
      B2SServer* server = new B2SServer();
      return static_cast<void*>(server);
   };
   scriptApi->SubmitTypeLibrary();
   scriptApi->SetCOMObjectOverride("B2SLegacy.Server", B2SServer_SCD);
   
   LOGI("B2SLegacy Plugin loaded successfully");
}

MSGPI_EXPORT void MSGPIAPI B2SLegacyPluginUnload()
{
   // Unregister COM object override
   if (scriptApi) {
      scriptApi->SetCOMObjectOverride("B2SLegacy.Server", nullptr);
   }
   
   // Clear VPX API
   SetVPXAPI(nullptr);
   
   // Clean up global player instance
   delete g_pplayer;
   g_pplayer = nullptr;
   
   // VPX API cleanup handled by system
   
   vpxApi = nullptr;
   scriptApi = nullptr;
   msgApi = nullptr;

   LOGI("B2SLegacy Plugin unloaded");
}