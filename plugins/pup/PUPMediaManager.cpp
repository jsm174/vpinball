#include "PUPMediaManager.h"
#include "PUPScreen.h"

namespace PUP {

PUPMediaManager::PUPMediaManager(PUPScreen* pScreen)
   : m_player1("PUP.Screen #"s.append(std::to_string(pScreen->GetScreenNum())).append(" Player #1"))
   , m_player2("PUP.Screen #"s.append(std::to_string(pScreen->GetScreenNum())).append(" Player #2"))
{
   m_pMainPlayer = &m_player1;
   m_pBackgroundPlayer = nullptr;
   m_pScreen = pScreen;
   m_pop = (pScreen->GetMode() == PUPScreen::Mode::ForcePopBack || pScreen->GetMode() == PUPScreen::Mode::ForcePop);
}

void PUPMediaManager::Play(PUPPlaylist* pPlaylist, const string& szPlayFile, float volume, int priority, bool skipSamePriority, int length)
{
   if (skipSamePriority && priority == m_pMainPlayer->priority) {
      LOGE("skipping same priority, screen={%s}, playlist={%s}, playFile=%s, priority=%d",
         m_pScreen->ToString(false).c_str(), pPlaylist->ToString().c_str(), szPlayFile.c_str(), priority);
      return;
   }

   string szPath = pPlaylist->GetPlayFilePath(szPlayFile);
   if (szPath.empty()) {
      LOGE("PlayFile not found: screen={%s}, playlist={%s}, playFile=%s",
         m_pScreen->ToString(false).c_str(), pPlaylist->ToString().c_str(), szPlayFile.c_str());
      return;
   }

   LOGD("screen={%s}, playlist={%s}, playFile=%s, path=%s, volume=%.1f, priority=%d, length=%d", m_pScreen->ToString(false).c_str(), pPlaylist->ToString().c_str(),
      szPlayFile.c_str(), szPath.c_str(), volume, priority, length);

   m_pMainPlayer->player.Play(szPath);
   m_pMainPlayer->player.SetVolume(volume);
   m_pMainPlayer->player.SetLength(length);
   m_pMainPlayer->szPath = szPath;
   m_pMainPlayer->volume = volume;
   m_pMainPlayer->priority = priority;
}

void PUPMediaManager::Pause()
{
   m_player1.player.Pause(true);
   m_player2.player.Pause(true);
}

void PUPMediaManager::Resume()
{
   m_player1.player.Pause(false);
   m_player2.player.Pause(false);
}

void PUPMediaManager::SetBG(bool isBackground)
{
   if (isBackground) {
      if (m_pBackgroundPlayer) {
         LOGD("Stopping background player, screen={%s}", m_pScreen->ToString(false).c_str());
         m_pBackgroundPlayer->player.Stop();
      }
      LOGD("Transferring main player to background, screen={%s}", m_pScreen->ToString(false).c_str());
      m_pBackgroundPlayer = m_pMainPlayer;
      m_pBackgroundPlayer->player.SetLoop(true);
      m_pMainPlayer = (m_pMainPlayer == &m_player1) ? &m_player2 : &m_player1;
   }
   else {
      if (m_pBackgroundPlayer) {
         LOGD("Removing looping from background player, screen={%s}", m_pScreen->ToString(false).c_str());
         m_pBackgroundPlayer->player.SetLoop(false);
      }
   }
}

void PUPMediaManager::SetLoop(bool isLoop)
{
   m_pMainPlayer->player.SetLoop(isLoop);
}

void PUPMediaManager::Stop()
{
   m_pMainPlayer->player.Stop();
}

void PUPMediaManager::Stop(int priority)
{
   if (priority > m_pMainPlayer->priority) {
      LOGD("Priority > main player priority: screen={%s}, priority=%d", m_pScreen->ToString(false).c_str(), priority);
      m_pMainPlayer->player.Stop();
   }
   else {
      LOGD("Priority <= main player priority: screen={%s}, priority=%d", m_pScreen->ToString(false).c_str(), priority);
   }
}

void PUPMediaManager::Stop(PUPPlaylist* pPlaylist, const string& szPlayFile)
{
   string szPath = pPlaylist->GetPlayFilePath(szPlayFile);
   if (!szPath.empty() && szPath == m_pMainPlayer->szPath) {
      LOGD("Main player stopping playback: screen={%s}, path=%s", m_pScreen->ToString(false).c_str(), szPath.c_str());
      m_pMainPlayer->player.Stop();
   }
   else {
      LOGD("Main player playback stop requested but currently not playing: screen={%s}, path=%s", m_pScreen->ToString(false).c_str(), szPath.c_str());
   }
}

void PUPMediaManager::SetBounds(const SDL_Rect& rect)
{
   m_bounds = rect;
   m_player1.player.SetBounds(rect);
   m_player2.player.SetBounds(rect);
}

void PUPMediaManager::SetMask(std::shared_ptr<SDL_Surface> mask)
{
   m_player1.player.SetMask(mask);
   m_player2.player.SetMask(mask);
}

void PUPMediaManager::Render(VPXRenderContext2D* const ctx)
{
   bool mainPlayerPlaying = m_pMainPlayer->player.IsPlaying();
   bool backgroundPlaying = false;

   if (m_pBackgroundPlayer) {
      backgroundPlaying = m_pBackgroundPlayer->player.IsPlaying();
      if (backgroundPlaying || (!m_pop && !mainPlayerPlaying))
         m_pBackgroundPlayer->player.Render(ctx, m_bounds);
   }

   if (mainPlayerPlaying || (!m_pop && !backgroundPlaying)) {
      m_pMainPlayer->player.Render(ctx, m_bounds);
   }

   if (m_pBackgroundPlayer)
      m_pBackgroundPlayer->player.SetVolume(mainPlayerPlaying ? 0.0f : m_pBackgroundPlayer->volume);
}

}