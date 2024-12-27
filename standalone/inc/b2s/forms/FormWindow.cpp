#include "core/stdafx.h"

#include "FormWindow.h"
#include "Form.h"

FormWindow::FormWindow(Form* pForm, const string& szTitle, int z, int x, int y, int w, int h)
    : VP::Window(szTitle, z, x, y, w, h)
{
   m_destRect = { 0.0f, 0.0f, (float)w, (float)h };
   m_angle = 0;
   m_pForm = pForm;
   m_pGraphics = nullptr;
   m_pSDLTexture = nullptr;
   m_pTexture = nullptr;
}

bool FormWindow::Init()
{
   if (!VP::Window::Init())
      return false;

   m_pGraphics = new VP::RendererGraphics(m_pRenderer);
   m_pForm->SetGraphics(m_pGraphics);

   return true;
}

FormWindow::~FormWindow()
{
   delete m_pGraphics;

   if (m_pSDLTexture)
      SDL_DestroyTexture(m_pSDLTexture);

   if (m_pTexture)
      delete m_pTexture;
}

void FormWindow::Render()
{
   if (!m_pSDLTexture) {
      m_pSDLTexture = SDL_CreateTexture(m_pRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, GetWidth(), GetHeight());
      if (!m_pSDLTexture)
         return;
   }

   if (!m_pTexture) {
      m_pTexture = new BaseTexture(GetWidth(), GetHeight(), BaseTexture::RGBA);
      if (!m_pTexture)
         return;
   }

   SDL_SetRenderTarget(m_pRenderer, m_pSDLTexture);

   bool update = m_pForm->Render();
   if (update) {
      SDL_Surface* pSurface = SDL_RenderReadPixels(m_pRenderer, nullptr);
      if (pSurface) {
         memcpy(m_pTexture->data(), pSurface->pixels, pSurface->pitch * pSurface->h);
         g_pplayer->m_renderer->m_renderDevice->m_texMan.SetDirty(m_pTexture);
         SDL_DestroySurface(pSurface);
      }
   }

   SDL_SetRenderTarget(m_pRenderer, NULL);

   if (m_pRenderOutput->GetMode() == VPX::RenderOutput::OM_WINDOW) {
      RenderTarget *scenePass = g_pplayer->m_renderer->m_renderDevice->GetCurrentRenderTarget();
      g_pplayer->m_renderer->RenderSprite(m_pTexture, m_pWindow->GetBackBuffer(),
         0, 0, m_pWindow->GetBackBuffer()->GetWidth(), m_pWindow->GetBackBuffer()->GetHeight());
      g_pplayer->m_renderer->m_renderDevice->AddRenderTargetDependency(scenePass, false);
   }
   else if (m_pRenderOutput->GetMode() == VPX::RenderOutput::OM_EMBEDDED) {
      int x, y;
      m_pRenderOutput->GetEmbeddedWindow()->GetPos(x, y);
      g_pplayer->m_renderer->RenderSprite(m_pTexture, g_pplayer->m_playfieldWnd->GetBackBuffer(),
         x, g_pplayer->m_playfieldWnd->GetBackBuffer()->GetHeight() - y - m_pRenderOutput->GetEmbeddedWindow()->GetHeight(),
         m_pRenderOutput->GetEmbeddedWindow()->GetWidth(), m_pRenderOutput->GetEmbeddedWindow()->GetHeight());
   }
}
