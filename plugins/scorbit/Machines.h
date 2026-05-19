// license:GPLv3+

#pragma once

#include "common.h"

namespace Scorbit
{

struct MachineInfo
{
   int machineId = 0;
   string uuid;
};

// Machines are keyed by the game name the table asked for (which may be an alias) and fall
// back to the resolved ROM, so a rom-alias table can either carry its own Scorbit machine or
// share the one of the ROM it aliases
bool LookupMachine(const string& gameId, const string& romId, MachineInfo& info);

}
