// license:GPLv3+

#include "core/stdafx.h"
#include "VPinballLib_Android.h"

#include <dlfcn.h>
#include <mutex>
#include <cstring>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <nlohmann/json.hpp>

#if defined(__aarch64__)
#include <adrenotools/driver.h>
#endif

extern "C" void* __real_dlopen(const char* filename, int flags);
extern "C" int __real_dlclose(void* handle);

namespace {

std::mutex g_gpuDriverMutex;
void* g_gpuDriverLibrary = nullptr;
std::string g_gpuDriverName;

std::string VulkanVersionString(uint32_t version)
{
   return std::to_string(VK_API_VERSION_MAJOR(version)) + '.' + std::to_string(VK_API_VERSION_MINOR(version)) + '.' + std::to_string(VK_API_VERSION_PATCH(version));
}

}

extern "C" void* __wrap_dlopen(const char* filename, int flags)
{
   if (filename && strcmp(filename, "libvulkan.so") == 0) {
      std::lock_guard<std::mutex> lock(g_gpuDriverMutex);
      if (g_gpuDriverLibrary)
         return g_gpuDriverLibrary;
   }
   return __real_dlopen(filename, flags);
}

extern "C" int __wrap_dlclose(void* handle)
{
   {
      std::lock_guard<std::mutex> lock(g_gpuDriverMutex);
      if (handle && handle == g_gpuDriverLibrary)
         return 0;
   }
   return __real_dlclose(handle);
}

namespace VPinballLib {

bool InitGpuDriver(const std::string& hookLibDir, const std::string& driverDir, const std::string& driverLibName)
{
   std::lock_guard<std::mutex> lock(g_gpuDriverMutex);

   if (driverLibName.empty()) {
      g_gpuDriverLibrary = nullptr;
      g_gpuDriverName.clear();
      PLOGI << "GPU driver: using system Vulkan driver";
      return true;
   }

#if defined(__aarch64__)
   void* handle = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM, nullptr, hookLibDir.c_str(), driverDir.c_str(), driverLibName.c_str(), nullptr, nullptr);
   if (!handle) {
      const char* error = dlerror();
      PLOGE << "GPU driver: failed to load custom Vulkan driver " << driverDir << driverLibName << " (" << (error ? error : "unknown error") << ')';
      g_gpuDriverLibrary = nullptr;
      g_gpuDriverName.clear();
      return false;
   }

   g_gpuDriverLibrary = handle;
   g_gpuDriverName = driverLibName;
   PLOGI << "GPU driver: loaded custom Vulkan driver " << driverDir << driverLibName;
   return true;
#else
   PLOGW << "GPU driver: custom Vulkan drivers are only supported on arm64";
   return false;
#endif
}

std::string GetGpuDriverInfo()
{
   nlohmann::json j;
   void* lib = nullptr;

   {
      std::lock_guard<std::mutex> lock(g_gpuDriverMutex);
      lib = g_gpuDriverLibrary;
      j["custom"] = lib != nullptr;
      j["library"] = g_gpuDriverName;
   }

   const bool opened = lib == nullptr;
   if (opened)
      lib = __real_dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);

   if (!lib) {
      j["error"] = "libvulkan.so not available";
      return j.dump();
   }

   const auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
   const auto createInstance = getInstanceProcAddr ? reinterpret_cast<PFN_vkCreateInstance>(getInstanceProcAddr(nullptr, "vkCreateInstance")) : nullptr;

   if (!createInstance) {
      j["error"] = "vkCreateInstance not available";
      if (opened)
         __real_dlclose(lib);
      return j.dump();
   }

   VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
   appInfo.pApplicationName = "Visual Pinball";
   appInfo.apiVersion = VK_API_VERSION_1_1;

   VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
   createInfo.pApplicationInfo = &appInfo;

   VkInstance instance = VK_NULL_HANDLE;
   if (createInstance(&createInfo, nullptr, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
      j["error"] = "vkCreateInstance failed";
      if (opened)
         __real_dlclose(lib);
      return j.dump();
   }

   const auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getInstanceProcAddr(instance, "vkDestroyInstance"));
   const auto enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
   const auto getPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));

   uint32_t deviceCount = 0;
   if (enumeratePhysicalDevices)
      enumeratePhysicalDevices(instance, &deviceCount, nullptr);

   if (deviceCount == 0 || !getPhysicalDeviceProperties2) {
      j["error"] = "no Vulkan devices";
   }
   else {
      VkPhysicalDevice device = VK_NULL_HANDLE;
      deviceCount = 1;
      enumeratePhysicalDevices(instance, &deviceCount, &device);

      VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
      getPhysicalDeviceProperties2(device, &props);

      j["deviceName"] = props.properties.deviceName;
      j["vendorId"] = props.properties.vendorID;
      j["deviceId"] = props.properties.deviceID;
      j["apiVersion"] = VulkanVersionString(props.properties.apiVersion);
      j["driverVersion"] = props.properties.driverVersion;

      if (props.properties.apiVersion >= VK_API_VERSION_1_2) {
         VkPhysicalDeviceDriverProperties driverProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
         props.pNext = &driverProps;
         getPhysicalDeviceProperties2(device, &props);
         j["driverId"] = static_cast<int>(driverProps.driverID);
         j["driverName"] = driverProps.driverName;
         j["driverInfo"] = driverProps.driverInfo;
      }
   }

   if (destroyInstance)
      destroyInstance(instance, nullptr);

   if (opened)
      __real_dlclose(lib);

   return j.dump();
}

}
