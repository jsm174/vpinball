// license:GPLv3+

#pragma once

#include <string>

namespace VPinballLib {

bool InitGpuDriver(const std::string& hookLibDir, const std::string& driverDir, const std::string& driverLibName);
std::string GetGpuDriverInfo();

}
