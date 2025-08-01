#pragma once

#include <future>
#include <chrono>

#include "common.h"
#include "B2SDataModel.h"
#include "ControllerPlugin.h"

namespace B2S {
   
class B2SRenderer final
{
public:
   B2SRenderer(MsgPluginAPI* const msgApi, const unsigned int endpointId, std::shared_ptr<B2STable> b2s);
   ~B2SRenderer();

   bool IsPinMAMEDriven() const;

   bool Render(VPXRenderContext2D* context);

private:
   std::function<void()> ResolveRomPropUpdater(float* value, const B2SRomIDType romIdType, const int romId, const bool romInverted = false) const;
   bool RenderBackglass(VPXRenderContext2D* context);
   bool RenderScoreview(VPXRenderContext2D* context);

   std::shared_ptr<B2STable> m_b2s;

   MsgPluginAPI* const m_msgApi;
   const unsigned int m_endpointId;
   unsigned int m_getDevSrcMsgId = 0, m_onDevChangedMsgId = 0;
   static void OnDevSrcChanged(const unsigned int msgId, void* userData, void* msgData);
   DevSrcId m_deviceStateSrc { 0 };
   unsigned int m_nSolenoids = 0;
   int m_GIIndex = -1;
   unsigned int m_nGIs = 0;
   int m_lampIndex = -1;
   unsigned int m_nLamps = 0;
   int m_mechIndex = -1;
   unsigned int m_nMechs = 0;

   std::chrono::time_point<std::chrono::high_resolution_clock> m_lastBackglassRenderTick;
   std::chrono::time_point<std::chrono::high_resolution_clock> m_lastDmdRenderTick;

   float m_b2sWidth = 0.f;
   float m_b2sHeight = 0.f;
   float m_dmdWidth = 0.f;
   float m_dmdHeight = 0.f;
   float m_grillCut = 0.f;
};

}