#include "core/stdafx.h"

#include "Window.h"
#include "WindowManager.h"

namespace VP {

Window::Window(const string& szTitle, int z, int x, int y, int w, int h)
{
   m_szTitle = szTitle;
   m_z = z;

#ifdef __LIBVPINBALL__
   VPX::RenderOutput::OutputMode mode = VPX::RenderOutput::OutputMode::OM_EMBEDDED;
#else
   VPX::RenderOutput::OutputMode mode = VPX::RenderOutput::OutputMode::OM_WINDOW;
#endif

   m_pRenderOutput = new VPX::RenderOutput(m_szTitle, g_pplayer->m_ptable->m_settings, Settings::Standalone, m_szTitle, mode, x, y, w, h);

   m_pWindow = nullptr;
   m_pEmbeddedWindow = nullptr;

   m_pRenderer = NULL;
   m_pSurface = NULL;
   m_visible = false;
   m_init = false;

   VP::WindowManager::GetInstance()->RegisterWindow(this);
}

bool Window::Init()
{
   if (!m_pRenderOutput) {
      PLOGE.printf("Failed to create render output: title=%s", m_szTitle.c_str());
      return false;
   }

   VPX::RenderOutput::OutputMode mode = m_pRenderOutput->GetMode();

   int x = 0;
   int y = 0;
   int width = 0;
   int height = 0;

   switch (mode) {
      case VPX::RenderOutput::OutputMode::OM_DISABLED:
         PLOGE.printf("Render output disabled: title=%s", m_szTitle.c_str());
         return false;
      case VPX::RenderOutput::OutputMode::OM_EMBEDDED:
         m_pEmbeddedWindow = m_pRenderOutput->GetEmbeddedWindow();
         m_pEmbeddedWindow->GetPos(x, y);
         width = m_pEmbeddedWindow->GetWidth();
         height = m_pEmbeddedWindow->GetHeight();
         break;
      case VPX::RenderOutput::OutputMode::OM_WINDOW:
         m_pWindow = m_pRenderOutput->GetWindow();
         g_pplayer->m_renderer->m_renderDevice->AddWindow(m_pWindow);
         m_pWindow->Show(m_visible);
         m_pWindow->GetPos(x, y);
         width = m_pWindow->GetWidth();
         height = m_pWindow->GetHeight();
         break;
   }

   m_pRenderer = SDL_GetRenderer(g_pplayer->m_playfieldWnd->GetCore());

   if (!m_pRenderer)
      m_pRenderer = SDL_CreateRenderer(g_pplayer->m_playfieldWnd->GetCore(), NULL);

   if (!m_pRenderer) {
      m_pSurface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ABGR8888);
      m_pRenderer = SDL_CreateSoftwareRenderer(m_pSurface);
   }

   if (!m_pRenderer) {
      PLOGE.printf("Failed to create renderer: title=%s", m_szTitle.c_str());
      return false;
   }

   const char* pRendererName = SDL_GetRendererName(m_pRenderer);

   m_init = true;

   PLOGI.printf("Window initialized: title=%s, z=%d, visible=%d, mode=%d, size=%dx%d, pos=%d,%d, renderer=%s", m_szTitle.c_str(), m_z, m_visible, mode, width, height, x, y, pRendererName);

   return true;
}

Window::~Window()
{
   VP::WindowManager::GetInstance()->UnregisterWindow(this);

   if (m_pRenderOutput)
      delete m_pRenderOutput;

   if (m_pSurface)
      SDL_DestroySurface(m_pSurface);

   if (m_pRenderer && m_pRenderer != SDL_GetRenderer(g_pplayer->m_playfieldWnd->GetCore()))
      SDL_DestroyRenderer(m_pRenderer);
}

int Window::GetWidth()
{
   return m_pWindow ? m_pWindow->GetWidth() : m_pEmbeddedWindow ? m_pEmbeddedWindow->GetWidth() : 0;
}

int Window::GetHeight()
{
   return m_pWindow ? m_pWindow->GetHeight() : m_pEmbeddedWindow ? m_pEmbeddedWindow->GetHeight() : 0;
}

void Window::Show()
{
   if (m_visible)
      return;

   m_visible = true;

   if (m_init) {
      if (m_pWindow)
         m_pWindow->Show(true);

      PLOGI.printf("Window updated: title=%s, z=%d, visible=%d", m_szTitle.c_str(), m_z, m_visible);
   }
}

void Window::Hide()
{
   if (!m_visible)
      return;

   m_visible = false;

   if (m_pWindow)
      m_pWindow->Show(false);

   PLOGI.printf("Window updated: title=%s, z=%d, visible=%d", m_szTitle.c_str(), m_z, m_visible);
}

void Window::OnRender()
{
   if (m_init && m_visible)
      Render();
}

}
