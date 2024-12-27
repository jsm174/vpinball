/*
 * Portions of this code was derived from SDL and FFMPEG:
 *
 * https://github.com/libsdl-org/SDL/blob/ed6381b68d4053aba65c08857d62b852f0db6832/test/testffmpeg.c
 * https://github.com/FFmpeg/FFmpeg/blob/e38092ef9395d7049f871ef4d5411eb410e283e0/fftools/ffplay.c
 */

#include "core/stdafx.h"

#include "PUPWindow.h"
#include "PUPScreen.h"

class PUPScreen;

PUPWindow::PUPWindow(PUPScreen* pScreen, const string& szTitle, int x, int y, int w, int h, int z, int rotation)
    : VP::Window(szTitle, x, y, w, h, z, rotation)
{
   m_pScreen = pScreen;
   m_pScreen->SetSize(w, h);
   m_pSDLTexture = nullptr;
   m_pTexture = nullptr;
}

PUPWindow::~PUPWindow()
{
}

bool PUPWindow::Init()
{
   if (!VP::Window::Init())
      return false;

   if (m_pScreen)
      m_pScreen->Init(m_pRenderer);

   return true;
}

void PUPWindow::Render()
{
   if (!m_visible)
      return;

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

   if (m_pScreen)
      m_pScreen->Render();

   SDL_Surface* pSurface = SDL_RenderReadPixels(m_pRenderer, nullptr);
   if (pSurface) {
      memcpy(m_pTexture->data(), pSurface->pixels, pSurface->pitch * pSurface->h);
      g_pplayer->m_renderer->m_renderDevice->m_texMan.SetDirty(m_pTexture);
      SDL_DestroySurface(pSurface);
   }

   SDL_SetRenderTarget(m_pRenderer, NULL);

   if (m_pRenderOutput->GetMode() == VPX::RenderOutput::OM_WINDOW) {
      RenderTarget *scenePass = g_pplayer->m_renderer->m_renderDevice->GetCurrentRenderTarget();
      m_pRenderOutput->GetWindow()->Show();
      g_pplayer->m_renderer->RenderSprite(m_pTexture, m_pRenderOutput->GetWindow()->GetBackBuffer(),
               0, 0, m_pRenderOutput->GetWindow()->GetBackBuffer()->GetWidth(), m_pRenderOutput->GetWindow()->GetBackBuffer()->GetHeight());
      g_pplayer->m_renderer->m_renderDevice->AddRenderTargetDependency(scenePass, false);
   }
   else if (m_pRenderOutput->GetMode() == VPX::RenderOutput::OM_EMBEDDED) {
      int x, y;
      m_pRenderOutput->GetEmbeddedWindow()->GetPos(x, y);
      g_pplayer->m_renderer->RenderSprite(m_pTexture,  g_pplayer->m_playfieldWnd->GetBackBuffer(),
         x, g_pplayer->m_playfieldWnd->GetBackBuffer()->GetHeight() - y - m_pRenderOutput->GetEmbeddedWindow()->GetHeight(), 
         m_pRenderOutput->GetEmbeddedWindow()->GetWidth(), m_pRenderOutput->GetEmbeddedWindow()->GetHeight());
   }
}