// license:GPLv3+

#include "Machines.h"

#include "nlohmann/json.hpp"

#include <fstream>

using json = nlohmann::json;

namespace Scorbit
{

bool LookupMachine(const string& gameId, const string& romId, MachineInfo& info)
{
   const std::filesystem::path file = GetPluginPath() / "assets"sv / "scorbit_machines.json"sv;

   std::ifstream f(file);
   if (!f.is_open())
   {
      LOGI("No machines file at "s + file.string() + " - expected JSON like { \"" + romId + "\": { \"machineId\": 1582, \"uuid\": \"...\" } }");
      return false;
   }

   try
   {
      json machines;
      f >> machines;
      if (!machines.is_object())
         return false;
      for (const string& key : { gameId, romId })
      {
         if (machines.contains(key) && machines[key].is_object())
         {
            info.machineId = machines[key].value("machineId", 0);
            info.uuid = machines[key].value("uuid", "");
            return info.machineId != 0;
         }
      }
   }
   catch (const json::exception& e)
   {
      LOGE("Failed to parse "s + file.string() + ": " + e.what());
   }
   return false;
}

}
