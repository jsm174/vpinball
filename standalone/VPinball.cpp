#include <core/stdafx.h>

#include "VPinball.h"
#include "VPinballLib.h"

static VPinballLib::VPinball& s_vpinstance = VPinballLib::VPinball::GetInstance();

VPINBALLAPI const char* VPinballGetVersionStringFull()
{
   thread_local string version;
   version = s_vpinstance.GetVersionStringFull();
   return version.c_str();
}

VPINBALLAPI void VPinballInit(VPinballEventCallback callback)
{
   auto eventCallback = [callback](VPinballLib::Event event, void* data) -> void* {
      return callback((VPINBALL_EVENT)event, data);
   };

   s_vpinstance.Init(eventCallback);
}

VPINBALLAPI void VPinballLog(VPINBALL_LOG_LEVEL level, const char* pMessage)
{
   s_vpinstance.Log((VPinballLib::LogLevel)level, pMessage);
}

VPINBALLAPI void VPinballResetLog()
{
   s_vpinstance.ResetLog();
}

VPINBALLAPI int VPinballLoadValueInt(const char* pSectionName, const char* pKey, int defaultValue)
{
   return s_vpinstance.LoadValueInt(pSectionName, pKey, defaultValue);
}

VPINBALLAPI float VPinballLoadValueFloat(const char* pSectionName, const char* pKey, float defaultValue)
{
   return s_vpinstance.LoadValueFloat(pSectionName, pKey, defaultValue);
}

VPINBALLAPI const char* VPinballLoadValueString(const char* pSectionName, const char* pKey, const char* pDefaultValue)
{
   thread_local string value;
   value = s_vpinstance.LoadValueString(pSectionName, pKey, pDefaultValue);
   return value.c_str();
}

VPINBALLAPI void VPinballSaveValueInt(const char* pSectionName, const char* pKey, int value)
{
   s_vpinstance.SaveValueInt(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueFloat(const char* pSectionName, const char* pKey, float value)
{
   s_vpinstance.SaveValueFloat(pSectionName, pKey, value);
}

VPINBALLAPI void VPinballSaveValueString(const char* pSectionName, const char* pKey, const char* pValue)
{
   s_vpinstance.SaveValueString(pSectionName, pKey, pValue);
}

VPINBALLAPI VPINBALL_STATUS VPinballUncompress(const char* pSource)
{
   return (VPINBALL_STATUS)s_vpinstance.Uncompress(pSource);
}

VPINBALLAPI VPINBALL_STATUS VPinballCompress(const char* pSource, const char* pDestination)
{
   return (VPINBALL_STATUS)s_vpinstance.Compress(pSource, pDestination);
}

VPINBALLAPI void VPinballUpdateWebServer()
{
   s_vpinstance.UpdateWebServer();
}

VPINBALLAPI void VPinballSetWebServerUpdated()
{
   s_vpinstance.SetWebServerUpdated();
}

VPINBALLAPI VPINBALL_STATUS VPinballResetIni()
{
   return (VPINBALL_STATUS)s_vpinstance.ResetIni();
}

VPINBALLAPI VPINBALL_STATUS VPinballLoad(const char* pSource)
{
   return (VPINBALL_STATUS)s_vpinstance.Load(pSource);
}

VPINBALLAPI VPINBALL_STATUS VPinballExtractScript(const char* pSource)
{
   return (VPINBALL_STATUS)s_vpinstance.ExtractScript(pSource);
}

VPINBALLAPI VPINBALL_STATUS VPinballPlay()
{
   return (VPINBALL_STATUS)s_vpinstance.Play();
}

VPINBALLAPI VPINBALL_STATUS VPinballStop()
{
   return (VPINBALL_STATUS)s_vpinstance.Stop();
}

VPINBALLAPI void VPinballSetPlayState(int enable)
{
   s_vpinstance.SetPlayState(enable);
}

VPINBALLAPI void VPinballToggleFPS()
{
   s_vpinstance.ToggleFPS();
}

VPINBALLAPI void VPinballGetTableOptions(VPinballTableOptions* pTableOptions)
{
   if (!pTableOptions)
      return;

   s_vpinstance.GetTableOptions(*pTableOptions);
}

VPINBALLAPI void VPinballSetTableOptions(VPinballTableOptions* pTableOptions)
{
   if (!pTableOptions)
      return;

   s_vpinstance.SetTableOptions(*pTableOptions);
}

VPINBALLAPI void VPinballSetDefaultTableOptions()
{
   return s_vpinstance.SetDefaultTableOptions();
}

VPINBALLAPI void VPinballResetTableOptions()
{
   return s_vpinstance.ResetTableOptions();
}

VPINBALLAPI void VPinballSaveTableOptions()
{
   return s_vpinstance.SaveTableOptions();
}

VPINBALLAPI int VPinballGetCustomTableOptionsCount()
{
   return s_vpinstance.GetCustomTableOptionsCount();
}

VPINBALLAPI void VPinballGetCustomTableOption(int index, VPinballCustomTableOption* pCustomTableOption)
{
   if (!pCustomTableOption)
      return;

   s_vpinstance.GetCustomTableOption(index, *pCustomTableOption);
}

VPINBALLAPI void VPinballSetCustomTableOption(VPinballCustomTableOption* pCustomTableOption)
{
   if (!pCustomTableOption)
      return;

   s_vpinstance.SetCustomTableOption(*pCustomTableOption);
}

VPINBALLAPI void VPinballSetDefaultCustomTableOptions()
{
   return s_vpinstance.SetDefaultCustomTableOptions();
}

VPINBALLAPI void VPinballResetCustomTableOptions()
{
   return s_vpinstance.ResetCustomTableOptions();
}

VPINBALLAPI void VPinballSaveCustomTableOptions()
{
   return s_vpinstance.SaveCustomTableOptions();
}

VPINBALLAPI void VPinballGetViewSetup(VPinballViewSetup* pViewSetup)
{
   if (!pViewSetup)
      return;

   s_vpinstance.GetViewSetup(*pViewSetup);
}

VPINBALLAPI void VPinballSetViewSetup(VPinballViewSetup* pViewSetup)
{
   if (!pViewSetup)
      return;

   s_vpinstance.SetViewSetup(*pViewSetup);
}

VPINBALLAPI void VPinballSetDefaultViewSetup()
{
   return s_vpinstance.SetDefaultViewSetup();
}

VPINBALLAPI void VPinballResetViewSetup()
{
   return s_vpinstance.ResetViewSetup();
}

VPINBALLAPI void VPinballSaveViewSetup()
{
   return s_vpinstance.SaveViewSetup();
}

VPINBALLAPI void VPinballCaptureScreenshot(const char* pFilename)
{
   return s_vpinstance.CaptureScreenshot(pFilename);
}


