#include "core/stdafx.h"
#include "core/vpversion.h"

#include "libvpinball.h"

#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <string>

#include "miniz/miniz.h"

#include <SDL2/SDL.h>

#include "standalone/inc/webserver/WebServer.h"

void* g_pMetalLayer = nullptr;
VPinballStateCallback g_stateCallback = nullptr;

bool g_shouldExit = false;
extern std::function<void()> g_gameLoop;

WebServer* g_pWebServer = nullptr;

VPINBALLAPI void VPinballInit()
{
   EditableRegistry::RegisterEditable<Ball>();
   EditableRegistry::RegisterEditable<Bumper>();
   EditableRegistry::RegisterEditable<Decal>();
   EditableRegistry::RegisterEditable<DispReel>();
   EditableRegistry::RegisterEditable<Flasher>();
   EditableRegistry::RegisterEditable<Flipper>();
   EditableRegistry::RegisterEditable<Gate>();
   EditableRegistry::RegisterEditable<Kicker>();
   EditableRegistry::RegisterEditable<Light>();
   EditableRegistry::RegisterEditable<LightSeq>();
   EditableRegistry::RegisterEditable<Plunger>();
   EditableRegistry::RegisterEditable<Primitive>();
   EditableRegistry::RegisterEditable<Ramp>();
   EditableRegistry::RegisterEditable<Rubber>();
   EditableRegistry::RegisterEditable<Spinner>();
   EditableRegistry::RegisterEditable<Surface>();
   EditableRegistry::RegisterEditable<Textbox>();
   EditableRegistry::RegisterEditable<Timer>();
   EditableRegistry::RegisterEditable<Trigger>();
   EditableRegistry::RegisterEditable<HitTarget>();

   SDL_SetMainReady();
   SDL_iPhoneSetEventPump(SDL_TRUE);

   SDL_InitSubSystem(SDL_INIT_VIDEO);
   SDL_InitSubSystem(SDL_INIT_JOYSTICK);
   SDL_InitSubSystem(SDL_INIT_TIMER);

   g_pvp = new VPinball();
   g_pvp->m_logicalNumberOfProcessors = SDL_GetCPUCount();
   g_pvp->m_settings.LoadFromFile(g_pvp->m_szMyPrefPath + "VPinballX.ini", true);

   Logger::GetInstance()->Init();
   Logger::GetInstance()->SetupLogger(true);

   PLOGI << "Starting VPX - " << VP_VERSION_STRING_FULL_LITERAL;
   PLOGI << "m_logicalNumberOfProcessors=" << g_pvp->m_logicalNumberOfProcessors;
   PLOGI << "m_szMyPath=" << g_pvp->m_szMyPath;
   PLOGI << "m_szMyPrefPath=" << g_pvp->m_szMyPrefPath;

   VPinballResetWebServer();
}

VPINBALLAPI void VPinballResetWebServer()
{
   delete g_pWebServer;
   g_pWebServer = new WebServer();

   if (g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "WebServer"s, false))
      g_pWebServer->Start();
}

VPINBALLAPI VPINBALL_STATUS VPinballResetIni() 
{
   string szIniFilePath = g_pvp->m_szMyPrefPath + "VPinballX.ini";
   if (std::remove(szIniFilePath.c_str()))
      return VPINBALL_STATUS_ERROR;

   g_pvp->m_settings.LoadFromFile(szIniFilePath, true);
   return VPINBALL_STATUS_OK;
}

VPINBALLAPI int VPinballLoadValueInt(VPINBALL_SETTINGS_SECTION section, const char* pKey, int defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault((Settings::Section)section, string(pKey), defaultValue);
}

VPINBALLAPI int VPinballLoadValueFloat(VPINBALL_SETTINGS_SECTION section, const char* pKey, float defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault((Settings::Section)section, string(pKey), defaultValue);
}

VPINBALLAPI const char* VPinballLoadValueString(VPINBALL_SETTINGS_SECTION section, const char* pKey, const char* pDefaultValue)
{
   static string value;
   value = g_pvp->m_settings.LoadValueWithDefault((Settings::Section)section, string(pKey), string(pDefaultValue));
   return value.c_str();
}

VPINBALLAPI void VPinballSaveValueInt(VPINBALL_SETTINGS_SECTION section, const char* pKey, int value)
{
   g_pvp->m_settings.SaveValue((Settings::Section)section, pKey, value);
   g_pvp->m_settings.Save();
}

VPINBALLAPI void VPinballSaveValueFloat(VPINBALL_SETTINGS_SECTION section, const char* pKey, float value)
{
   g_pvp->m_settings.SaveValue((Settings::Section)section, pKey, value);
   g_pvp->m_settings.Save();
}

VPINBALLAPI void VPinballSaveValueString(VPINBALL_SETTINGS_SECTION section, const char* pKey, const char* pValue)
{
   g_pvp->m_settings.SaveValue((Settings::Section)section, pKey, pValue);
   g_pvp->m_settings.Save();
}

VPINBALLAPI void VPinballSetStateCallback(VPinballStateCallback callback)
{
   g_stateCallback = callback;
}

const char* VPinballGetVersionStringFull()
{
   return VP_VERSION_STRING_FULL_LITERAL;
}

VPINBALLAPI VPINBALL_STATUS VPinballExtract(const char* pSource)
{
   PLOGI.printf("Extracting: pSource=%s", pSource);

   mz_zip_archive zip_archive;
   memset(&zip_archive, 0, sizeof(zip_archive));

   mz_bool status = mz_zip_reader_init_file(&zip_archive, pSource, 0);
   if (!status) {
      return VPINBALL_STATUS_ERROR;
   }

   bool success = true;

   int file_count = (int)mz_zip_reader_get_num_files(&zip_archive);

   for (int i = 0; i < file_count; i++) {     
      mz_zip_archive_file_stat file_stat;
      if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat))
         success = false;

      string filename = file_stat.m_filename;
      if (filename.starts_with("__MACOSX"))
         continue;

      string path = std::filesystem::path(string(pSource)).parent_path().append(filename);
      if (mz_zip_reader_is_file_a_directory(&zip_archive, i))
         std::filesystem::create_directories(path);
      else {
         if (!mz_zip_reader_extract_to_file(&zip_archive, i, path.c_str(), 0)) {
            success = false;
         }
      }

      int progress = (i * 100) / file_count;
      VPinballSetState(VPINBALL_STATE_EXTRACTING, &progress);
   }

   mz_zip_reader_end(&zip_archive);

   return success ? VPINBALL_STATUS_OK : VPINBALL_STATUS_ERROR;
}

VPINBALLAPI VPINBALL_STATUS VPinballCompress(const char* pSource, const char* pDestination)
{
   PLOGI.printf("Compressing: pSource=%s, pDestination=%s", pSource, pDestination);

   mz_zip_archive zip_archive;
   memset(&zip_archive, 0, sizeof(zip_archive));

   mz_bool status = mz_zip_writer_init_file(&zip_archive, pDestination, 0);
   if (!status)
      return VPINBALL_STATUS_ERROR;

   bool success = true;

   std::filesystem::path sourcePath(pSource);
   std::filesystem::path destinationPath(pDestination);
   auto sourcePathLength = sourcePath.string().length();

   size_t totalFiles = 0;
   for (auto& p : std::filesystem::recursive_directory_iterator(pSource)) {
      if (!std::filesystem::is_directory(p.path()))
         totalFiles++;
   }

   size_t processedFiles = 0;

   auto add_to_zip = [&zip_archive, &sourcePathLength, &processedFiles, totalFiles](const std::filesystem::path& path) {
      if (std::filesystem::is_directory(path)) {
         string dir_in_zip = path.string().substr(sourcePathLength + 1) + "/";
         if (!mz_zip_writer_add_mem(&zip_archive, dir_in_zip.c_str(), nullptr, 0, MZ_NO_COMPRESSION))
            return false;
      }
      else {
         std::ifstream input(path, std::ios::binary);
         std::vector<char> buffer((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
         string file_in_zip = path.string().substr(sourcePathLength + 1);
         if (!mz_zip_writer_add_mem(&zip_archive, file_in_zip.c_str(), buffer.data(), buffer.size(), MZ_NO_COMPRESSION))
            return false;
         processedFiles++;

         int progress = (processedFiles * 100) / totalFiles;
         VPinballSetState(VPINBALL_STATE_COMPRESSING, &progress);
      }
      return true;
   };

   for (auto& p : std::filesystem::recursive_directory_iterator(pSource)) {
      if (!add_to_zip(p.path())) {
         success = false;
         break;
      }
   }

   mz_zip_writer_finalize_archive(&zip_archive);
   mz_zip_writer_end(&zip_archive);

   return success ? VPINBALL_STATUS_OK : VPINBALL_STATUS_ERROR;
}

VPINBALLAPI void VPinballSetMetalLayer(void* pMetalLayer)
{
   g_pMetalLayer = pMetalLayer;
}

void VPinballSetState(VPINBALL_STATE state, void* data)
{
   if (g_stateCallback)
      g_stateCallback(state, data);
}

void VPinballGameLoop(void* ptr)
{
   if (g_gameLoop) {
       g_gameLoop();

       if (g_shouldExit) {
           PLOGI.printf("Game Loop stopping");

           delete g_pplayer;

           g_pvp->CloseTable(g_pvp->GetActiveTable());

           delete g_pvp; // FIXME: without it, uses last table

           g_pvp = new VPinball();
           g_pvp->m_settings.LoadFromFile(g_pvp->m_szMyPrefPath + "VPinballX.ini", true);
           g_pvp->m_logicalNumberOfProcessors = SDL_GetCPUCount();

           VPinballSetState(VPINBALL_STATE_STOPPED, nullptr);

           g_gameLoop = nullptr;
       }
   }
}

VPINBALLAPI VPINBALL_STATUS VPinballPlay(const char* pPath)
{
   PLOGI.printf("Playing: pPath=%s", pPath);

   g_gameLoop = nullptr;
   g_shouldExit = false;

   if (!DirExists(PATH_USER))
      std::filesystem::create_directory(PATH_USER);

   g_pvp->LoadFileName(pPath, true);
   g_pvp->DoPlay(false);

   SDL_iPhoneSetAnimationCallback(g_pplayer->m_playfieldWnd->GetCore(), 1, VPinballGameLoop, NULL);

   return VPINBALL_STATUS_OK;
}

VPINBALLAPI VPINBALL_STATUS VPinballStop()
{
   PLOGI.printf("Stopping");

   g_pvp->GetActiveTable()->QuitPlayer(Player::CS_CLOSE_APP);

   g_shouldExit = true;

   return VPINBALL_STATUS_OK;
}
