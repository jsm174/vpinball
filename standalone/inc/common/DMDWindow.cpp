#include "core/stdafx.h"

#include "DMDWindow.h"

namespace VP {

int DMDWindow::s_instanceId = 0;

void DMDWindow::onGetIdentifyDMD(const unsigned int eventId, void* userData, void* msgData)
{
}

void DMDWindow::onGetRenderDMDSrc(const unsigned int eventId, void* userData, void* msgData)
{
   DMDWindow* pDMDWindow = (DMDWindow*)userData;

   if (!pDMDWindow->m_attached)
      return;

   GetDmdSrcMsg& msg = *static_cast<GetDmdSrcMsg*>(msgData);

   msg.entries[msg.count].id = pDMDWindow->m_dmdId;
   msg.entries[msg.count].format = CTLPI_GETDMD_FORMAT_SRGB888;
   msg.entries[msg.count].width = pDMDWindow->m_pRGB24DMD->GetWidth();
   msg.entries[msg.count].height = pDMDWindow->m_pRGB24DMD->GetHeight();
   msg.count++;
}

void DMDWindow::onGetRenderDMD(const unsigned int eventId, void* userData, void* msgData)
{
   DMDWindow* pDMDWindow = (DMDWindow*)userData;

   if (!pDMDWindow->m_attached)
      return;

   const UINT8* pRGB24Data = pDMDWindow->m_pRGB24DMD->GetData();
   if (pRGB24Data) {
      GetDmdMsg& getDmdMsg = *static_cast<GetDmdMsg*>(msgData);

      getDmdMsg.frameId = pDMDWindow->m_frameId++;
      getDmdMsg.frame = (unsigned char*)pRGB24Data;
   }
}

DMDWindow::DMDWindow(const string& szTitle)
{
   m_pDMD = nullptr;
   m_pRGB24DMD = nullptr;
   m_attached = false;
   m_frameId = 0;

   m_szTitle = "DMDWindow_" + szTitle + "_" + std::to_string(s_instanceId++);
   m_plugin = MsgPluginManager::GetInstance().RegisterPlugin(m_szTitle.c_str(), "VPX", "Visual Pinball X", "", "", "https://github.com/vpinball/vpinball",
      [](const uint32_t pluginId, const MsgPluginAPI* api) {},
      []() {});
   m_plugin->Load(&MsgPluginManager::GetInstance().GetMsgAPI());

   m_dmdId = { m_plugin->m_endpointId, 1 };

   const auto& msgApi = MsgPluginManager::GetInstance().GetMsgAPI();

   m_getDmdSrcId = msgApi.GetMsgID(CTLPI_NAMESPACE, CTLPI_GETDMD_SRC_MSG);
   m_getRenderDmdId = msgApi.GetMsgID(CTLPI_NAMESPACE, CTLPI_GETDMD_RENDER_MSG);
   m_getIdentifyDmdId = msgApi.GetMsgID(CTLPI_NAMESPACE, CTLPI_GETDMD_IDENTIFY_MSG);
   m_onDmdSrcChangedId = msgApi.GetMsgID(CTLPI_NAMESPACE, CTLPI_ONDMD_SRC_CHG_MSG);

   msgApi.SubscribeMsg(m_plugin->m_endpointId, m_getDmdSrcId, onGetRenderDMDSrc, this);
   msgApi.SubscribeMsg(m_plugin->m_endpointId, m_getRenderDmdId, onGetRenderDMD, this);
   msgApi.SubscribeMsg(m_plugin->m_endpointId, m_getIdentifyDmdId, onGetIdentifyDMD, this);
}

DMDWindow::~DMDWindow()
{
   if (m_pDMD) {
      PLOGE.printf("Destructor called without first detaching DMD.");
   }

   const auto& msgApi = MsgPluginManager::GetInstance().GetMsgAPI();

   msgApi.UnsubscribeMsg(m_getDmdSrcId, onGetRenderDMDSrc);
   msgApi.UnsubscribeMsg(m_getRenderDmdId, onGetRenderDMD);
   msgApi.UnsubscribeMsg(m_getIdentifyDmdId, onGetIdentifyDMD);

   msgApi.ReleaseMsgID(m_getDmdSrcId);
   msgApi.ReleaseMsgID(m_getRenderDmdId);
   msgApi.ReleaseMsgID(m_getIdentifyDmdId);
   msgApi.ReleaseMsgID(m_onDmdSrcChangedId);

   m_plugin->Unload();
}

void DMDWindow::AttachDMD(DMDUtil::DMD* pDMD, int width, int height)
{
   if (m_pDMD) {
      PLOGE.printf("Unable to attach DMD: message=Detach existing DMD first.");
      return;
   }

   if (!pDMD) {
      PLOGE.printf("Unable to attach DMD: message=DMD is null.");
      return;
   }

   PLOGI.printf("Attaching DMD: width=%d, height=%d", width, height);

   m_pRGB24DMD = pDMD->CreateRGB24DMD(width, height);

   if (m_pRGB24DMD) {
      m_pDMD = pDMD;
      m_attached = true;

      const auto& msgApi = MsgPluginManager::GetInstance().GetMsgAPI();
      msgApi.BroadcastMsg(m_plugin->m_endpointId, m_onDmdSrcChangedId, nullptr);
   }
   else {
      PLOGE.printf("Failed to attach DMD: message=Failed to create RGB24DMD.");
   }
}

void DMDWindow::DetachDMD()
{
   if (!m_pDMD) {
      PLOGE.printf("Unable to detach DMD: message=No DMD attached.");
      return;
   }

   m_attached = false;
   m_frameId = 0;

   if (m_pRGB24DMD) {
      PLOGI.printf("Detaching DMD");
      m_pDMD->DestroyRGB24DMD(m_pRGB24DMD);
      m_pRGB24DMD = nullptr;
   }

   m_pDMD = nullptr;

   const auto& msgApi = MsgPluginManager::GetInstance().GetMsgAPI();
   msgApi.BroadcastMsg(m_plugin->m_endpointId, m_onDmdSrcChangedId, nullptr);
}

void DMDWindow::Show()
{
   PLOGW.printf("DMDWindow::Show() not implemented: title=%s", m_szTitle.c_str());
}

void DMDWindow::Hide()
{
   PLOGW.printf("DMDWindow::Hide() not implemented: title=%s", m_szTitle.c_str());
}

}