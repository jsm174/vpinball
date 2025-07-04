// This is some very ancient (and, if we're being frank, very
// awful) code dedicated to the Ultracade Ushock device, which 
// (one gathers from reading the code) was an early, primitive
// predecessor of HID output controller devices like the LedWiz.
// The Ushock has a single output for a pinball knocker.  This
// code implements bespoke Win32 HID API access to the Ushock
// specifically.  ("PBW" is another name seen a lot here, which
// I believe stands for PinballBallWizard, which as I recall is
// another product from the same vendor.  Or maybe the same one.
// Who knows.)
// 
// The entire concept of this module has long since been
// superseded by DOF (DirectOutput Framework), which is why
// you don't see a bunch of similar modules for LedWiz's,
// PacLed's, and a dozen other devices, thank goodness.
//
// This code was formerly named with the extremely presumptuous
// prefix "hid_" for all of its public functions.  It is most
// certainly not "hid" code generically; it happens to *use*
// HID to do its one extremely narrow job, but naming everything
// here "hid_xxx" makes about as much sense as naming it "c_xxx" 
// because it's written in C.  So, as of 9/2024, it has been
// renamed to more properly indicate its function.  (This wasn't 
// just because its old naming was so piquing, but rather because
// the old naming was creating a collision with the hidapi library,
// which also uses hid_xxx as its naming convention.  Of the two,
// I think hidapi has the far better claim on the name.)

#include "core/stdafx.h"

// This code should be understandable using
// the following URL:
// http://www.edn.com/article/CA243218.html
// 
// [which is, naturally, a dead link; but the code is just basic
// Win32 HID enumeration and access code, and there are gazillions
// of rote examples on stackoverflow that do basically the same
// things]
#ifndef __STANDALONE__
extern "C" {
#include "setupapi.h"
#include "hidsdi.h"
}
#endif

static HANDLE connectToIthUSBHIDDevice(DWORD deviceIndex)
{
#ifndef __STANDALONE__
   GUID hidGUID;
   SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
   DWORD requiredSize;

   //Get the HID GUID value - used as mask to get list of devices
   HidD_GetHidGuid(&hidGUID);

   //Get a list of devices matching the criteria (hid interface, present)
   HDEVINFO hardwareDeviceInfoSet = SetupDiGetClassDevs(&hidGUID,
      nullptr, // Define no enumerator (global)
      nullptr, // Define no
      (DIGCF_PRESENT | // Only Devices present
      DIGCF_DEVICEINTERFACE)); // Function class devices.

   deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

   //Go through the list and get the interface data
   DWORD result = SetupDiEnumDeviceInterfaces(hardwareDeviceInfoSet,
      nullptr,  //infoData,
      &hidGUID, //interfaceClassGuid,
      deviceIndex,
      &deviceInterfaceData);

   /* Failed to get a device - possibly the index is larger than the number of devices */
   if (!result)
   {
      SetupDiDestroyDeviceInfoList(hardwareDeviceInfoSet);
      return INVALID_HANDLE_VALUE;
   }

   //Get the details with null values to get the required size of the buffer
   SetupDiGetDeviceInterfaceDetail(hardwareDeviceInfoSet,
      &deviceInterfaceData,
      nullptr, //interfaceDetail,
      0, //interfaceDetailSize,
      &requiredSize,
      0); //infoData))

   //Allocate the buffer
   PSP_INTERFACE_DEVICE_DETAIL_DATA deviceDetail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)calloc(requiredSize, 1);
   deviceDetail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);

   DWORD newRequiredSize;

   //Fill the buffer with the device details
   if (!SetupDiGetDeviceInterfaceDetail(hardwareDeviceInfoSet,
      &deviceInterfaceData,
      deviceDetail,
      requiredSize,
      &newRequiredSize,
      nullptr))
   {
      SetupDiDestroyDeviceInfoList(hardwareDeviceInfoSet);
      free(deviceDetail);
      return INVALID_HANDLE_VALUE;
   }

   //Open file on the device
   const HANDLE deviceHandle = CreateFile(deviceDetail->DevicePath,
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,       // no SECURITY_ATTRIBUTES structure
      OPEN_EXISTING, // No special create flags
      FILE_FLAG_OVERLAPPED,
      nullptr);      // No template file

   SetupDiDestroyDeviceInfoList(hardwareDeviceInfoSet);
   free(deviceDetail);
   return deviceHandle;
#else 
   return 0L;
#endif
}

static HANDLE hid_connect(uint32_t vendorID, uint32_t productID, uint32_t * const versionNumber = nullptr)
{
#ifndef __STANDALONE__
   DWORD index = 0;
   HIDD_ATTRIBUTES deviceAttributes;

   while (index < 10)
   {
      HANDLE deviceHandle;
      if ((deviceHandle = connectToIthUSBHIDDevice(index)) == INVALID_HANDLE_VALUE)
      {
         index++;
         continue;
      }

      if (!HidD_GetAttributes(deviceHandle, &deviceAttributes))
      {
         CloseHandle(deviceHandle);
         return INVALID_HANDLE_VALUE;
      }
      if ((vendorID == 0 || deviceAttributes.VendorID == vendorID) &&
         (productID == 0 || deviceAttributes.ProductID == productID) &&
         (versionNumber == nullptr || deviceAttributes.VersionNumber == *versionNumber))
      {
         return deviceHandle; /* matched */
      }

      CloseHandle(deviceHandle); /* not a match - close and try again */

      index++;
   }
#endif

   return INVALID_HANDLE_VALUE;
}

static HANDLE hnd = hid_connect(0x04b4, 0x6470);
#ifndef __STANDALONE__
static OVERLAPPED ol;
static PHIDP_PREPARSED_DATA HidParsedData;
static HIDP_CAPS Capabilities;
#endif

void ushock_output_init()
{
#ifndef __STANDALONE__
   // 0x4b4 == Ultracade, 0x6470 == Ushock

   if (hnd != INVALID_HANDLE_VALUE)
   {
      printf("Connected to PBW controller\n");
      unsigned char buffer[1024] = { 0 };
      unsigned char inbuffer[1024] = { 0 };

      HidD_GetPreparsedData(hnd, &HidParsedData);

      if (!HidParsedData) // if uShock is unplugged the HidD_FreePreparsedData() crashes
      {
         printf("ushock_output_init: Could not connect or find the PBW controller\n");
         return;
      }

      HidP_GetCaps(HidParsedData, &Capabilities);
      assert(Capabilities.InputReportByteLength <= sizeof(inbuffer));

      if (HidParsedData) HidD_FreePreparsedData(HidParsedData); //make sure not null, otherwise crash		

      HANDLE sReportEvent = CreateEvent(nullptr, 1, 0, nullptr);

      ol.hEvent = sReportEvent;
      ol.Offset = 0;
      ol.OffsetHigh = 0;

      buffer[0] = 0;
      buffer[1] = 0x00;

      DWORD written;
      WriteFile(hnd, buffer, Capabilities.OutputReportByteLength, &written, &ol);
      WaitForSingleObject(sReportEvent, 200);
      CloseHandle(sReportEvent);

      printf("%lu bytes written\n", written);

      DWORD bytes_read;
      ReadFile(hnd,
         inbuffer,
         Capabilities.InputReportByteLength,
         &bytes_read,
         &ol);

      printf("%lu bytes read: ", bytes_read);

      for (DWORD i = 0; i < bytes_read; i++)
      {
         printf("%02x ", inbuffer[i]);
      }
      printf("\n");
   }
   else
   {
      printf("ushock_output_init: Could not connect or find the PBW controller\n");
   }
#endif
}


static uint32_t sMask = 0;


// This is the main interface to turn output on and off.
// Once set, the value will remain set until another set call is made.
// The output_mask parameter uses any combination of HID_OUTPUT enum.
void ushock_output_set(const uint8_t output_mask, const bool on)
{
   // Check if the outputs are being turned on.
   if (on)
   {
      sMask |= output_mask;
   }
   else
   {
      sMask &= ~output_mask;
   }
}


#define KNOCK_PERIOD_ON  50
#define KNOCK_PERIOD_OFF 500

static int sKnock;
static int sKnockState;
static uint32_t sKnockStamp;


void ushock_output_knock(const int count)
{
   if (count)
   {
      sKnock = count;
      sKnockStamp = g_pplayer->m_time_msec;
      sKnockState = 1;
   }
}


void ushock_output_update(const uint32_t cur_time_msec)
{
#ifndef __STANDALONE__
   uint8_t mask = (uint8_t)(sMask & 0xff);

   if (sKnockStamp)
   {
      if (cur_time_msec - sKnockStamp < (uint32_t)(sKnockState ? KNOCK_PERIOD_ON : KNOCK_PERIOD_OFF))
      {
         mask |= sKnockState ? (uint8_t)HID_OUTPUT_KNOCKER : (uint8_t)0x00;
      }
      else
      {
         if (sKnockState)
         {
            sKnockState = 0;
            --sKnock;
            if (sKnock == 0)
            {
               sKnockStamp = 0;
            }
            else sKnockStamp = cur_time_msec;
         }
         else
         {
            sKnockState = 1;
            sKnockStamp = cur_time_msec;
         }
      }
   }

   if (hnd != INVALID_HANDLE_VALUE)
   {
      //printf( "outputting 0x%x\n", mask );

      static uint32_t last_written;

      // This really needs serious optimization by putting in a separate thread or something - AMH
      if (mask != last_written)
      {
         unsigned char buffer[1024] = { 0 };

         HANDLE sReportEvent = CreateEvent(nullptr, 1, 0, nullptr);

         ol.hEvent = sReportEvent;
         ol.Offset = 0;
         ol.OffsetHigh = 0;

         buffer[0] = 0;
         buffer[1] = mask;

         last_written = mask;

         DWORD written;
         WriteFile(hnd, buffer, Capabilities.OutputReportByteLength, &written, &ol);
         WaitForSingleObject(sReportEvent, 200);
         CloseHandle(sReportEvent);
      }
   }
   else
   {
      static int printed;

      if (!printed)
      {
         printf("ERROR: Could not connect or find the PBW controller\n");
         printed = 1;
      }
   }
#endif
}

void ushock_output_shutdown()
{
   if (hnd != INVALID_HANDLE_VALUE)
   {
      hnd = INVALID_HANDLE_VALUE;
#ifndef __STANDALONE__
      CloseHandle(hnd);
#endif
   }
}
