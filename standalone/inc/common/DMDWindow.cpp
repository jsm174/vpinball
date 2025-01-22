#include "core/stdafx.h"

#include "DMDWindow.h"

namespace VP {

DMDWindow::DMDWindow(const string& szTitle, int z, int x, int y, int w, int h)
    : VP::Window(szTitle, z, x, y, w, h)
{
   m_pDMD = nullptr;
   m_pRGB24DMD = nullptr;
   m_size = 0;
   m_pTexture = nullptr;
   m_attached = false;
}

DMDWindow::~DMDWindow()
{
   if (m_pDMD) {
      PLOGE.printf("Destructor called without first detaching DMD.");
   }
}

bool DMDWindow::Init()
{
   if (!VP::Window::Init())
      return false;

   return true;
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
      m_size = m_pRGB24DMD->GetWidth() * m_pRGB24DMD->GetHeight();
      m_pDMD = pDMD;
      m_attached = true;
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

   if (m_pRGB24DMD) {
      PLOGI.printf("Detaching DMD");
      m_pDMD->DestroyRGB24DMD(m_pRGB24DMD);
      m_pRGB24DMD = nullptr;
   }

   if (m_pTexture) {
      delete m_pTexture;
      m_pTexture = nullptr;
   }

   m_pDMD = nullptr;
}

void DMDWindow::Render()
{
   if (!m_attached)
      return;

   if (!m_pTexture) {
      if (m_pRGB24DMD)
         m_pTexture = new BaseTexture(m_pRGB24DMD->GetWidth(), m_pRGB24DMD->GetHeight(), BaseTexture::RGBA);
         memset(m_pTexture->data(), 0, m_pRGB24DMD->GetWidth() * m_pRGB24DMD->GetHeight() * 4);
      if (!m_pTexture)
         return;
   } 

   const UINT8* pRGB24Data = m_pRGB24DMD->GetData();   
   if (pRGB24Data) {
      DWORD *const data = (DWORD *)m_pTexture->data();
      for (int ofs = 0; ofs < m_size; ofs++)
            data[ofs] = 0xFF000000 | (pRGB24Data[ofs * 3 + 2] << 16) | (pRGB24Data[ofs * 3 + 1] << 8) | pRGB24Data[ofs * 3];
      g_pplayer->m_renderer->m_renderDevice->m_texMan.SetDirty(m_pTexture);
   }

   vec4 dmdTint = vec4(1.f, 1.f, 1.f, 1.f);
   const int dmdProfile = g_pplayer->m_ptable->m_settings.LoadValueInt(Settings::DMD, "DefaultProfile"s);

   if (m_pWindow) {
      RenderTarget *scenePass = g_pplayer->m_renderer->m_renderDevice->GetCurrentRenderTarget();
      g_pplayer->m_renderer->RenderDMD(dmdProfile, dmdTint, m_pTexture, m_pWindow->GetBackBuffer(),
         0, 0, m_pWindow->GetBackBuffer()->GetWidth(), m_pWindow->GetBackBuffer()->GetHeight());
      g_pplayer->m_renderer->m_renderDevice->AddRenderTargetDependency(scenePass, false);
   }
   else if (m_pEmbeddedWindow) {
      int x, y;
      m_pEmbeddedWindow->GetPos(x, y);
      g_pplayer->m_renderer->RenderDMD(dmdProfile, dmdTint, m_pTexture, g_pplayer->m_playfieldWnd->GetBackBuffer(),
         x, g_pplayer->m_playfieldWnd->GetBackBuffer()->GetHeight() - y - m_pEmbeddedWindow->GetHeight(),
         m_pEmbeddedWindow->GetWidth(), m_pEmbeddedWindow->GetHeight());
   }
}

}
