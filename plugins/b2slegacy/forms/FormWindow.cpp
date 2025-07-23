#include "../common.h"

#include "FormWindow.h"
#include "Form.h"


namespace B2SLegacy {

int FormWindow::Render(VPXRenderContext2D* const renderCtx, void* context)
{
   return static_cast<FormWindow*>(context)->Render(renderCtx);
}

void FormWindow::OnGetRenderer(const unsigned int eventId, void* context, void* msgData)
{
   FormWindow* me = static_cast<FormWindow*>(context);
   GetAnciliaryRendererMsg* msg = static_cast<GetAnciliaryRendererMsg*>(msgData);
   if (msg->window == me->m_anciliaryWindow) {
      if (msg->count < msg->maxEntryCount) {
         msg->entries[msg->count].id = me->m_szTitle.c_str();
         msg->entries[msg->count].name = me->m_szTitle.c_str();
         msg->entries[msg->count].description = me->m_szDescription.c_str();
         msg->entries[msg->count].context = me;
         msg->entries[msg->count].Render = &FormWindow::Render;
      }
      msg->count++;
   }
}

FormWindow::FormWindow(const string& szTitle, Form* pForm, VPXAnciliaryWindow anciliaryWindow)
{
   m_szTitle = szTitle;
   m_pForm = pForm;
   m_anciliaryWindow = anciliaryWindow;

   m_szDescription = "Renderer for B2S: " + szTitle;

   // Simplified for plugin context - these would need to be set by the plugin manager
   m_pMsgApi = nullptr;
   m_pVpxApi = nullptr;
   m_endpointId = 0;
   m_getAuxRendererId = 0;
   m_onAuxRendererChgId = 0;
   m_pSurface = nullptr;
   m_pRenderer = nullptr;
}

FormWindow::~FormWindow()
{
   Hide();
   
   if (m_pRenderer)
      SDL_DestroyRenderer(m_pRenderer);
   if (m_pSurface)
      SDL_DestroySurface(m_pSurface);
}

int FormWindow::Render(VPXRenderContext2D* const renderCtx)
{
   // Stub implementation - would render the form content
   return 0;
}

void FormWindow::Show()
{
   // Stub implementation
}

void FormWindow::Hide()
{
   // Stub implementation
}

} // namespace B2SLegacy