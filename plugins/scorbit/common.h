#pragma once

#include <string>
#include <sstream>

#include "plugins/LoggingPlugin.h"

using namespace std::string_literals;

namespace Scorbit {

LPI_USE_CPP();
#define LOGD LPI_LOGD_CPP
#define LOGI LPI_LOGI_CPP
#define LOGW LPI_LOGW_CPP
#define LOGE LPI_LOGE_CPP

}
