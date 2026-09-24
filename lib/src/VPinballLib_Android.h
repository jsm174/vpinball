// license:GPLv3+

#pragma once

#include <string>

namespace VPinballLib {

bool InitGpuDriver(const std::string& hookLibDir, const std::string& driverDir, const std::string& driverLibName, const std::string& environment);
std::string GetGpuDriverInfo();

}
