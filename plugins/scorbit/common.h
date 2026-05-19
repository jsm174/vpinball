// license:GPLv3+

#pragma once

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <tchar.h>
#else
#include <dlfcn.h>
#include <climits>
#endif

using namespace std::string_literals;
using namespace std::string_view_literals;
using std::string;
using std::vector;

// Shared logging
#include "plugins/LoggingPlugin.h"

// VPX main API
#include "plugins/VPXPlugin.h"

namespace Scorbit
{

LPI_USE_CPP();
#define LOGD LPI_LOGD_CPP
#define LOGI LPI_LOGI_CPP
#define LOGW LPI_LOGW_CPP
#define LOGE LPI_LOGE_CPP

std::filesystem::path GetPluginPath();

}
