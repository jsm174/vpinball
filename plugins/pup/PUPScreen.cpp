#include "PUPScreen.h"
#include "PUPCustomPos.h"
#include "PUPTrigger.h"
#include "PUPManager.h"
#include "PUPPlaylist.h"
#include "PUPLabel.h"
#include "PUPMediaManager.h"

#include <SDL3_image/SDL_image.h>

namespace PUP {
  
const char* PUP_SCREEN_MODE_STRINGS[] = {
   "PUP_SCREEN_MODE_OFF",
   "PUP_SCREEN_MODE_SHOW",
   "PUP_SCREEN_MODE_FORCE_ON",
   "PUP_SCREEN_MODE_FORCE_POP",
   "PUP_SCREEN_MODE_FORCE_BACK",
   "PUP_SCREEN_MODE_FORCE_POP_BACK",
   "PUP_SCREEN_MODE_MUSIC_ONLY"
};

const char* PUP_SCREEN_MODE_TO_STRING(PUP_SCREEN_MODE value)
{
   if ((int)value < 0 || (size_t)value >= std::size(PUP_SCREEN_MODE_STRINGS))
      return "UNKNOWN";
   return PUP_SCREEN_MODE_STRINGS[value];
}

const char* PUP_PINDISPLAY_REQUEST_TYPE_STRINGS[] = {
   "PUP_PINDISPLAY_REQUEST_TYPE_NORMAL",
   "PUP_PINDISPLAY_REQUEST_TYPE_LOOP",
   "PUP_PINDISPLAY_REQUEST_TYPE_SET_BG",
   "PUP_PINDISPLAY_REQUEST_TYPE_STOP"
};

const char* PUP_PINDISPLAY_REQUEST_TYPE_TO_STRING(PUP_PINDISPLAY_REQUEST_TYPE value)
{
   if ((int)value < 0 || (size_t)value >= std::size(PUP_PINDISPLAY_REQUEST_TYPE_STRINGS))
      return "UNKNOWN";
   return PUP_PINDISPLAY_REQUEST_TYPE_STRINGS[value];
}

/*
   screens.pup: ScreenNum,ScreenDes,PlayList,PlayFile,Loopit,Active,Priority,CustomPos
   PuP Pack Editor: Mode,ScreenNum,ScreenDes,Background Playlist,Background Filename,Transparent,CustomPos,Volume %

   mappings:

     ScreenNum = ScreenNum
     ScreenDes = ScreenDes
     PlayList = Background Playlist
     PlayFile = Background Filename
     Loopit = Transparent
     Active = Mode
     Priority = Volume %
     CustomPos = CustomPos
*/

PUPScreen::PUPScreen(PUPManager* manager, PUP_SCREEN_MODE mode, int screenNum, const string& szScreenDes, const string& szBackgroundPlaylist, const string& szBackgroundFilename, bool transparent, float volume, PUPCustomPos* pCustomPos, const std::vector<PUPPlaylist*>& playlists)
   : m_pManager(manager)
   , m_screenNum(screenNum)
{
   m_mode = mode;
   m_screenDes = szScreenDes;
   m_backgroundPlaylist = szBackgroundPlaylist;
   m_backgroundFilename = szBackgroundFilename;
   m_transparent = transparent;
   m_volume = volume;
   m_pCustomPos = pCustomPos;
   memset(&m_background, 0, sizeof(m_background));
   memset(&m_overlay, 0, sizeof(m_overlay));
   m_pMediaPlayerManager = std::make_unique<PUPMediaManager>(this);
   m_labelInit = false;
   m_pagenum = 0;
   m_defaultPagenum = 0;
   m_pParent = nullptr;
   m_isRunning = false;

   for (const PUPPlaylist* pPlaylist : playlists) {
      // make a copy of the playlist
      PUPPlaylist *pPlaylistCopy = new PUPPlaylist(*pPlaylist);
      AddPlaylist(pPlaylistCopy);
   }

   LoadTriggers();

   if (!m_backgroundPlaylist.empty()) {
      QueuePlay(m_backgroundPlaylist, m_backgroundFilename, m_volume, -1);
      QueueBG(true);
   }
}

PUPScreen::~PUPScreen()
{
   {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_isRunning = false;
   }
   m_queueCondVar.notify_all();
   if (m_thread.joinable())
      m_thread.join();

   delete m_pCustomPos;
   FreeRenderable(&m_background);
   FreeRenderable(&m_overlay);
   if (m_pageTimer)
      SDL_RemoveTimer(m_pageTimer);

   for (auto& [key, pPlaylist] : m_playlistMap)
      delete pPlaylist;

   for (auto& [key, pTriggers] : m_triggerMap) {
      for (PUPTrigger* pTrigger : pTriggers)
         delete pTrigger;
   }

   for (PUPLabel* pLabel : m_labels)
      delete pLabel;

   for (auto pChildren : { &m_defaultChildren, &m_backChildren, &m_topChildren })
      pChildren->clear();
}

PUPScreen* PUPScreen::CreateFromCSV(PUPManager* manager, const string& line, const std::vector<PUPPlaylist*>& playlists)
{
   vector<string> parts = parse_csv_line(line);
   if (parts.size() != 8) {
      LOGE("Failed to parse screen line, expected 8 columns but got %d: %s", parts.size(), line.c_str());
      return nullptr;
   }

   PUP_SCREEN_MODE mode;
   if (StrCompareNoCase(parts[5], "Show"s))
      mode = PUP_SCREEN_MODE_SHOW;
   else if (StrCompareNoCase(parts[5], "ForceON"s))
      mode = PUP_SCREEN_MODE_FORCE_ON;
   else if (StrCompareNoCase(parts[5], "ForcePoP"s))
      mode = PUP_SCREEN_MODE_FORCE_POP;
   else if (StrCompareNoCase(parts[5], "ForceBack"s))
      mode = PUP_SCREEN_MODE_FORCE_BACK;
   else if (StrCompareNoCase(parts[5], "ForcePopBack"s))
      mode = PUP_SCREEN_MODE_FORCE_POP_BACK;
   else if (StrCompareNoCase(parts[5], "MusicOnly"s))
      mode = PUP_SCREEN_MODE_MUSIC_ONLY;
   else if (StrCompareNoCase(parts[5], "Off"s))
      mode = PUP_SCREEN_MODE_OFF;
   else {
      LOGE("Invalid screen mode: %s", parts[5].c_str());
      mode = PUP_SCREEN_MODE_OFF;
   }

   return new PUPScreen(
      manager,
      mode,
      string_to_int(parts[0], 0), // screenNum
      parts[1], // screenDes
      parts[2], // background Playlist
      parts[3], // background PlayFile
      parts[4] == "1", // transparent
      string_to_float(parts[6], 100.0f), // volume
      PUPCustomPos::CreateFromCSV(parts[7]), playlists);
}

PUPScreen* PUPScreen::CreateDefault(PUPManager* manager, int screenNum, const std::vector<PUPPlaylist*>& playlists)
{
   if (manager->HasScreen(screenNum)) {
      LOGE("Screen already exists: screenNum=%d", screenNum);
      return nullptr;
   }

   PUPScreen* pScreen = nullptr;
   switch(screenNum) {
      case PUP_SCREEN_TOPPER: pScreen = new PUPScreen(manager, PUP_SCREEN_MODE_SHOW, PUP_SCREEN_TOPPER, "Topper"s, ""s, ""s, false, 100.0f, nullptr, playlists);
      case PUP_SCREEN_DMD: pScreen = new PUPScreen(manager, PUP_SCREEN_MODE_SHOW, PUP_SCREEN_DMD, "DMD"s, ""s, ""s, false, 100.0f, nullptr, playlists);
      case PUP_SCREEN_BACKGLASS: pScreen = new PUPScreen(manager, PUP_SCREEN_MODE_SHOW, PUP_SCREEN_BACKGLASS, "Backglass"s, ""s, ""s, false, 100.0f, nullptr, playlists);
      case PUP_SCREEN_PLAYFIELD: pScreen = new PUPScreen(manager, PUP_SCREEN_MODE_SHOW, PUP_SCREEN_PLAYFIELD, "Playfield"s, ""s, ""s, false, 100.0f, nullptr, playlists);
      default: pScreen = new PUPScreen(manager, PUP_SCREEN_MODE_SHOW, screenNum, "Unknown"s, ""s, ""s, false, 100.0f, nullptr, playlists);
   }
   return pScreen;
}

void PUPScreen::LoadTriggers()
{
   string szPlaylistsPath = find_case_insensitive_file_path(m_pManager->GetPath() + "triggers.pup");
   std::ifstream triggersFile;
   triggersFile.open(szPlaylistsPath, std::ifstream::in);
   if (triggersFile.is_open())
   {
      string line;
      int i = 0;
      while (std::getline(triggersFile, line))
      {
         if (++i == 1)
            continue;
         AddTrigger(PUPTrigger::CreateFromCSV(this, line));
      }
   }
}

void PUPScreen::AddChild(PUPScreen* pScreen)
{
   switch (pScreen->GetMode()) {
      case PUP_SCREEN_MODE_FORCE_ON:
      case PUP_SCREEN_MODE_FORCE_POP:
         m_topChildren.push_back(pScreen);
         break;
      case PUP_SCREEN_MODE_FORCE_BACK:
      case PUP_SCREEN_MODE_FORCE_POP_BACK:
         m_backChildren.push_back(pScreen);
         break;
      default:
          m_defaultChildren.push_back(pScreen);
   }
   pScreen->SetParent(this);
}

void PUPScreen::SendToFront()
{
   if (m_pParent) {
      if (m_mode == PUP_SCREEN_MODE_FORCE_ON || m_mode == PUP_SCREEN_MODE_FORCE_POP) {
         auto it = std::find(m_pParent->m_topChildren.begin(), m_pParent->m_topChildren.end(), this);
         if (it != m_pParent->m_topChildren.end())
            std::rotate(it, it + 1, m_pParent->m_topChildren.end());
      }
      else if (m_mode == PUP_SCREEN_MODE_FORCE_BACK || m_mode == PUP_SCREEN_MODE_FORCE_POP_BACK) {
         auto it = std::find(m_pParent->m_backChildren.begin(), m_pParent->m_backChildren.end(), this);
         if (it != m_pParent->m_backChildren.end())
            std::rotate(it, it + 1, m_pParent->m_backChildren.end());
      }
   }
}

void PUPScreen::AddPlaylist(PUPPlaylist* pPlaylist)
{
   if (!pPlaylist)
      return;

   m_playlistMap[lowerCase(pPlaylist->GetFolder())] = pPlaylist;
}

PUPPlaylist* PUPScreen::GetPlaylist(const string& szFolder)
{
   ankerl::unordered_dense::map<string, PUPPlaylist*>::const_iterator it = m_playlistMap.find(lowerCase(szFolder));
   return it != m_playlistMap.end() ? it->second : nullptr;
}

void PUPScreen::AddTrigger(PUPTrigger* pTrigger)
{
   if (!pTrigger)
      return;

   m_triggerMap[pTrigger->GetTrigger()].push_back(pTrigger);
}

vector<PUPTrigger*>* PUPScreen::GetTriggers(const string& szTrigger)
{
   ankerl::unordered_dense::map<string, vector<PUPTrigger*>>::iterator it = m_triggerMap.find(szTrigger);
   return it != m_triggerMap.end() ? &it->second : nullptr;
}

void PUPScreen::AddLabel(PUPLabel* pLabel)
{
   if (GetLabel(pLabel->GetName())) {
      LOGE("Duplicate label: screen={%s}, label=%s", ToString(false).c_str(), pLabel->ToString().c_str());
      delete pLabel;
      return;
   }

   pLabel->SetScreen(this);
   m_labelMap[lowerCase(pLabel->GetName())] = pLabel;
   m_labels.push_back(pLabel);
}

PUPLabel* PUPScreen::GetLabel(const string& szLabelName)
{
   auto it = m_labelMap.find(lowerCase(szLabelName));
   return it != m_labelMap.end() ? it->second : nullptr;
}

void PUPScreen::SendLabelToBack(PUPLabel* pLabel)
{
   auto it = std::find(m_labels.begin(), m_labels.end(), pLabel);
   if (it != m_labels.end())
      std::rotate(m_labels.begin(), it, it + 1);
}

void PUPScreen::SendLabelToFront(PUPLabel* pLabel)
{
   auto it = std::find(m_labels.begin(), m_labels.end(), pLabel);
   if (it != m_labels.end())
      std::rotate(it, it + 1, m_labels.end());
}

void PUPScreen::SetPage(int pagenum, int seconds)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   if (m_pageTimer)
      SDL_RemoveTimer(m_pageTimer);
   m_pageTimer = 0;
   m_pagenum = pagenum;

   if (seconds == 0)
      m_defaultPagenum = pagenum;
   else
      m_pageTimer = SDL_AddTimer(seconds * 1000, PageTimerElapsed, this);
}

uint32_t PUPScreen::PageTimerElapsed(void* param, SDL_TimerID timerID, uint32_t interval)
{
   PUPScreen* me = static_cast<PUPScreen*>(param);
   std::lock_guard<std::mutex> lock(me->m_renderMutex);
   SDL_RemoveTimer(me->m_pageTimer);
   me->m_pageTimer = 0;
   me->m_pagenum = me->m_defaultPagenum;
   return interval;
}

void PUPScreen::SetSize(int w, int h)
{
   if (m_pCustomPos)
      m_rect = m_pCustomPos->ScaledRect(w, h);
   else
      m_rect = { 0, 0, w, h };

   m_pMediaPlayerManager->SetBounds(m_rect);

   for (auto pChildren : { &m_defaultChildren, &m_backChildren, &m_topChildren }) {
      for (PUPScreen* pScreen : *pChildren)
          pScreen->SetSize(w, h);
   }
}

void PUPScreen::SetBackground(PUPPlaylist* pPlaylist, const std::string& szPlayFile)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   LoadRenderable(&m_background, pPlaylist->GetPlayFilePath(szPlayFile));
}

void PUPScreen::SetCustomPos(const string& szCustomPos)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   delete m_pCustomPos;
   m_pCustomPos = PUPCustomPos::CreateFromCSV(szCustomPos);
}

void PUPScreen::SetOverlay(PUPPlaylist* pPlaylist, const std::string& szPlayFile)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   LoadRenderable(&m_overlay, pPlaylist->GetPlayFilePath(szPlayFile));
}

void PUPScreen::SetMedia(PUPPlaylist* pPlaylist, const std::string& szPlayFile, float volume, int priority, bool skipSamePriority, int length)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   m_pMediaPlayerManager->Play(pPlaylist, szPlayFile, m_pParent ? (volume / 100.0f) * m_pParent->GetVolume() : volume, priority, skipSamePriority, length);
}

void PUPScreen::StopMedia()
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   m_pMediaPlayerManager->Stop();
}

void PUPScreen::StopMedia(int priority)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   m_pMediaPlayerManager->Stop(priority);
}

void PUPScreen::StopMedia(PUPPlaylist* pPlaylist, const std::string& szPlayFile)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   m_pMediaPlayerManager->Stop(pPlaylist, szPlayFile);
}

void PUPScreen::SetLoop(int state)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   m_pMediaPlayerManager->SetLoop(state != 0);
}

void PUPScreen::SetBG(int mode)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);
   m_pMediaPlayerManager->SetBG(mode != 0);
}

void PUPScreen::QueuePlay(const string& szPlaylist, const string& szPlayFile, float volume, int priority)
{
   PUPPlaylist* pPlaylist = GetPlaylist(szPlaylist);
   if (!pPlaylist) {
      LOGE("Playlist not found: screen={%s}, playlist=%s", ToString(false).c_str(), szPlaylist.c_str());
      return;
   }

   LOGD("queueing play, screen={%s}, playlist={%s}, playFile=%s, volume=%.f, priority=%d",
      ToString(false).c_str(), pPlaylist->ToString().c_str(), szPlayFile.c_str(), volume, priority);

   PUPPinDisplayRequest* pRequest = new PUPPinDisplayRequest();
   pRequest->type = PUP_PINDISPLAY_REQUEST_TYPE_NORMAL;
   pRequest->pPlaylist = pPlaylist;
   pRequest->szPlayFile = szPlayFile;
   pRequest->volume = volume;
   pRequest->priority = priority;

   {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_queue.push(pRequest);
   }
   m_queueCondVar.notify_one();
}

void PUPScreen::QueueStop()
{
   LOGD("queueing stop, screen={%s}", ToString(false).c_str());

   PUPPinDisplayRequest* pRequest = new PUPPinDisplayRequest();
   pRequest->type = PUP_PINDISPLAY_REQUEST_TYPE_STOP;

   {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_queue.push(pRequest);
   }
   m_queueCondVar.notify_one();
}

void PUPScreen::QueueLoop(int state)
{
   LOGD("queueing loop, screen={%s}, state=%d", ToString(false).c_str(), state);

   PUPPinDisplayRequest* pRequest = new PUPPinDisplayRequest();
   pRequest->type = PUP_PINDISPLAY_REQUEST_TYPE_LOOP;
   pRequest->value = state;

   {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_queue.push(pRequest);
   }
   m_queueCondVar.notify_one();
}

void PUPScreen::QueueBG(int mode)
{
   LOGD("queueing bg, screen={%s}, mode=%d", ToString(false).c_str(), mode);

   PUPPinDisplayRequest* pRequest = new PUPPinDisplayRequest();
   pRequest->type = PUP_PINDISPLAY_REQUEST_TYPE_SET_BG;
   pRequest->value = mode;

   {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_queue.push(pRequest);
   }
   m_queueCondVar.notify_one();
}

void PUPScreen::QueueTriggerRequest(PUPTriggerRequest* pRequest)
{
   {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_queue.push(pRequest);
   }
   m_queueCondVar.notify_one();
}

void PUPScreen::Start()
{
   LOGD("Starting: screen={%s}", ToString(false).c_str());

   m_isRunning = true;
   m_thread = std::thread(&PUPScreen::ProcessQueue, this);
}

void PUPScreen::Init()
{
   LOGD("Initializing: screen={%s}", ToString(false).c_str());
   for (auto pChildren : { &m_defaultChildren, &m_backChildren, &m_topChildren }) {
      for (PUPScreen* pScreen : *pChildren)
         pScreen->Init();
   }
}

void PUPScreen::ProcessQueue()
{
   SetThreadName("PUPScreen#"s.append(std::to_string(m_screenNum)).append(".ProcessQueue"));
   while (true)
   {
      std::unique_lock<std::mutex> lock(m_queueMutex);
      m_queueCondVar.wait(lock, [this] { return !m_queue.empty() || !m_isRunning; });

      if (!m_isRunning) {
         while (!m_queue.empty()) {
            PUPScreenRequest* pRequest = m_queue.front();
            m_queue.pop();
            delete pRequest;
         }
         break;
      }

      PUPScreenRequest* pRequest = m_queue.front();
      m_queue.pop();
      lock.unlock();

      if (dynamic_cast<PUPPinDisplayRequest*>(pRequest))
         ProcessPinDisplayRequest((PUPPinDisplayRequest*)pRequest);
      else if (dynamic_cast<PUPTriggerRequest*>(pRequest))
         ProcessTriggerRequest((PUPTriggerRequest*)pRequest);

      delete pRequest;
   }
}

void PUPScreen::ProcessPinDisplayRequest(PUPPinDisplayRequest* pRequest)
{
   LOGD("processing pin display request: screen={%s}, type=%s, playlist={%s}, playFile=%s, volume=%.1f, priority=%d, value=%d",
      ToString(false).c_str(),
      PUP_PINDISPLAY_REQUEST_TYPE_TO_STRING(pRequest->type),
      pRequest->pPlaylist ? pRequest->pPlaylist->ToString().c_str() : "",
      pRequest->szPlayFile.c_str(),
      pRequest->volume,
      pRequest->priority,
      pRequest->value);

   switch (pRequest->type) {
      case PUP_PINDISPLAY_REQUEST_TYPE_NORMAL:
         switch (pRequest->pPlaylist->GetFunction()) {
            case PUP_PLAYLIST_FUNCTION_DEFAULT:
               SetMedia(pRequest->pPlaylist, pRequest->szPlayFile, pRequest->volume, pRequest->priority, false, 0);
               break;
            case PUP_PLAYLIST_FUNCTION_FRAMES:
               StopMedia();
               SetBackground(pRequest->pPlaylist, pRequest->szPlayFile);
               break;
            case PUP_PLAYLIST_FUNCTION_OVERLAYS:
            case PUP_PLAYLIST_FUNCTION_ALPHAS:
               StopMedia();
               SetOverlay(pRequest->pPlaylist, pRequest->szPlayFile);
               break;
            default:
               LOGE("Playlist function not implemented: %s", PUP_PLAYLIST_FUNCTION_TO_STRING(pRequest->pPlaylist->GetFunction()));
               break;
         }
         break;
      case PUP_PINDISPLAY_REQUEST_TYPE_LOOP:
         SetLoop(pRequest->value);
         break;
      case PUP_PINDISPLAY_REQUEST_TYPE_SET_BG:
         SetBG(pRequest->value);
         break;
      case PUP_PINDISPLAY_REQUEST_TYPE_STOP:
         StopMedia();
         break;
      default:
         LOGE("request type not implemented: %s", PUP_PINDISPLAY_REQUEST_TYPE_TO_STRING(pRequest->type));
         break;
   }
}

void PUPScreen::ProcessTriggerRequest(PUPTriggerRequest* pRequest)
{
   PUPTrigger* pTrigger = pRequest->pTrigger;
   if (pTrigger->IsResting()) {
      LOGE("skipping resting trigger: trigger={%s}", pTrigger->ToString().c_str());
      return;
   }
   pTrigger->SetTriggered();

   LOGD("processing trigger: trigger={%s}", pTrigger->ToString().c_str());

   switch(pTrigger->GetPlayAction()) {
      case PUP_TRIGGER_PLAY_ACTION_NORMAL:
         switch (pTrigger->GetPlaylist()->GetFunction()) {
            case PUP_PLAYLIST_FUNCTION_DEFAULT:
               SetMedia(pTrigger->GetPlaylist(), pTrigger->GetPlayFile(), pTrigger->GetVolume(), pTrigger->GetPriority(), false, pTrigger->GetLength());
               break;
            case PUP_PLAYLIST_FUNCTION_FRAMES:
               StopMedia();
               SetBackground(pTrigger->GetPlaylist(), pTrigger->GetPlayFile());
               break;
            case PUP_PLAYLIST_FUNCTION_OVERLAYS:
            case PUP_PLAYLIST_FUNCTION_ALPHAS:
               StopMedia();
               SetOverlay(pTrigger->GetPlaylist(), pTrigger->GetPlayFile());
               break;
            default:
               LOGE("Playlist function not implemented: %s", PUP_PLAYLIST_FUNCTION_TO_STRING(pTrigger->GetPlaylist()->GetFunction()));
               break;
         }
         break;
      case PUP_TRIGGER_PLAY_ACTION_LOOP:
         SetMedia(pTrigger->GetPlaylist(), pTrigger->GetPlayFile(), pTrigger->GetVolume(), pTrigger->GetPriority(), false, pTrigger->GetLength());
         SetLoop(1);
         break;
      case PUP_TRIGGER_PLAY_ACTION_SET_BG:
         SetMedia(pTrigger->GetPlaylist(), pTrigger->GetPlayFile(), pTrigger->GetVolume(), pTrigger->GetPriority(), false, pTrigger->GetLength());
         SetBG(1);
         break;
      case PUP_TRIGGER_PLAY_ACTION_SKIP_SAME_PRTY:
         SetMedia(pTrigger->GetPlaylist(), pTrigger->GetPlayFile(), pTrigger->GetVolume(), pTrigger->GetPriority(), true, pTrigger->GetLength());
         break;
      case PUP_TRIGGER_PLAY_ACTION_STOP_PLAYER:
         StopMedia(pTrigger->GetPriority());
         break;
      case PUP_TRIGGER_PLAY_ACTION_STOP_FILE:
         StopMedia(pTrigger->GetPlaylist(), pTrigger->GetPlayFile());
         break;
      default:
         LOGE("Play action not implemented: %s", PUP_TRIGGER_PLAY_ACTION_TO_STRING(pTrigger->GetPlayAction()));
         break;
   }
}

void PUPScreen::Render(VPXRenderContext2D* const ctx)
{
   std::lock_guard<std::mutex> lock(m_renderMutex);

   Render(ctx, &m_background);

   m_pMediaPlayerManager->Render(ctx);

   for (auto pChildren : { &m_defaultChildren, &m_backChildren, &m_topChildren }) {
      for (PUPScreen* pScreen : *pChildren)
         pScreen->Render(ctx);
   }

   Render(ctx, &m_overlay);

   // FIXME port SDL_SetRenderClipRect(m_pRenderer, &m_rect);

   for (PUPLabel* pLabel : m_labels)
      pLabel->Render(ctx, m_rect, m_pagenum);

   // FIXME port SDL_SetRenderClipRect(m_pRenderer, NULL);
}

void PUPScreen::LoadRenderable(PUPScreenRenderable* pRenderable, const string& szFile)
{
   if (pRenderable->pSurface) {
      SDL_DestroySurface(pRenderable->pSurface);
      pRenderable->pSurface = nullptr;
   }

   SDL_Surface* pSurface = IMG_Load(szFile.c_str());
   if (pSurface) {
      if (pSurface->format == SDL_PIXELFORMAT_RGBA32)
         pRenderable->pSurface = pSurface;
      else {
         pRenderable->pSurface = SDL_ConvertSurface(pSurface, SDL_PIXELFORMAT_RGBA32);
         SDL_DestroySurface(pSurface);
      }
   }
   pRenderable->dirty = true;
}

void PUPScreen::Render(VPXRenderContext2D* const ctx, PUPScreenRenderable* pRenderable)
{
   // Update texture
   if (pRenderable->dirty)
   {
      if (pRenderable->pTexture) {
         DeleteTexture(pRenderable->pTexture);
         pRenderable->pTexture = NULL;
      }
      if (pRenderable->pSurface) {
         pRenderable->pTexture = CreateTexture(pRenderable->pSurface);
         SDL_DestroySurface(pRenderable->pSurface);
         pRenderable->pSurface = NULL;
      }
      pRenderable->dirty = false;
   }

   // Render image
   if (pRenderable->pTexture)
   {
      VPXTextureInfo* texInfo = GetTextureInfo(pRenderable->pTexture);
      ctx->DrawImage(ctx, pRenderable->pTexture, 1.f, 1.f, 1.f, 1.f,
         0.f, 0.f, static_cast<float>(texInfo->width), static_cast<float>(texInfo->height),
         0.f, 0.f, 0.f, 
         static_cast<float>(m_rect.x), static_cast<float>(m_rect.y), static_cast<float>(m_rect.w), static_cast<float>(m_rect.h));
   }
}

void PUPScreen::FreeRenderable(PUPScreenRenderable* pRenderable)
{
   if (pRenderable->pSurface)
      SDL_DestroySurface(pRenderable->pSurface);

   if (pRenderable->pTexture)
      DeleteTexture(pRenderable->pTexture);
}

string PUPScreen::ToString(bool full) const
{
   if (full) {
      return "mode=" + string(PUP_SCREEN_MODE_TO_STRING(m_mode)) +
         ", screenNum=" + std::to_string(m_screenNum) +
         ", screenDes=" + m_screenDes +
         ", backgroundPlaylist=" + m_backgroundPlaylist +
         ", backgroundFilename=" + m_backgroundFilename +
         ", transparent=" + (m_transparent ? "true" : "false") +
         ", volume=" + std::to_string(m_volume) +
         ", m_customPos={" + (m_pCustomPos ? m_pCustomPos->ToString() : ""s) + '}';
   }

   return "screenNum=" + std::to_string(m_screenNum) +
      ", screenDes=" + m_screenDes +
      ", mode=" + string(PUP_SCREEN_MODE_TO_STRING(m_mode));
}

}