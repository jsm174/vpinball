// license:GPLv3+

#include <cassert>
#include <cstdlib>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "MsgPlugin.h"
#include "CorePlugin.h"
#include "PinMamePlugin.h"
#include "common.h"

#ifndef _MSC_VER
 #define strcpy_s(A, B, C) strncpy(A, C, B)
#endif

///////////////////////////////////////////////////////////////////////////////
// DMDUtil Colorization plugin
//
// This plugin only rely on the generic message plugin API and the following
// messages:
// - PinMame/onGameStart: msgData is PinMame game identifier (rom name)
// - PinMame/onGameEnd
// - Controller/GetDMD: msgData is a request/response struct

static MsgPluginAPI* msgApi = nullptr;
static uint32_t endpointId;

static unsigned int onGameStartId;
static unsigned int onGameEndId;
static unsigned int onDmdSrcChangedId;
static unsigned int getDmdRenderId;

static CtlResId dmdId;
static bool dmdSelected = false;


void onGameStart(const unsigned int msgId, void* userData, void* msgData)
{
}

void onGameEnd(const unsigned int msgId, void* userData, void* msgData)
{
}

void onDmdSrcChanged(const unsigned int msgId, void* userData, void* msgData)
{
}

void getDMDRender(const unsigned int msgId, void* userData, void* msgData)
{
   GetDmdMsg& getDmdMsg = *static_cast<GetDmdMsg*>(msgData);
}

MSGPI_EXPORT void MSGPIAPI PluginLoad(const uint32_t sessionId, MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;
   
   msgApi->SubscribeMsg(endpointId, onGameStartId = msgApi->GetMsgID(PMPI_NAMESPACE, PMPI_EVT_ON_GAME_START), onGameStart, nullptr);
   msgApi->SubscribeMsg(endpointId, onGameEndId = msgApi->GetMsgID(PMPI_NAMESPACE, PMPI_EVT_ON_GAME_END), onGameEnd, nullptr);
   msgApi->SubscribeMsg(endpointId, onDmdSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_ONDMD_SRC_CHG_MSG), onDmdSrcChanged, nullptr);
   msgApi->SubscribeMsg(endpointId, getDmdRenderId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_GETDMD_RENDER_MSG), getDMDRender, nullptr);
}

MSGPI_EXPORT void MSGPIAPI PluginUnload()
{
   msgApi->UnsubscribeMsg(onGameStartId, onGameStart);
   msgApi->UnsubscribeMsg(onGameEndId, onGameEnd);
   msgApi->UnsubscribeMsg(onDmdSrcChangedId, onDmdSrcChanged);
   msgApi->UnsubscribeMsg(getDmdRenderId, getDMDRender);

   msgApi->ReleaseMsgID(onGameStartId);
   msgApi->ReleaseMsgID(onGameEndId);
   msgApi->ReleaseMsgID(onDmdSrcChangedId);
   msgApi->ReleaseMsgID(getDmdRenderId);

   msgApi = nullptr;
}
