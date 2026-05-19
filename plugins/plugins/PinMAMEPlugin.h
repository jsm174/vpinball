// license:GPLv3+

#pragma once

#include <stdint.h>

#define PINMAMEPI_NAMESPACE "PinMAME"
#define PINMAMEPI_GET_NVRAM_MSG "GetNVRAM"

typedef struct PinMAMEGetNvramMsg
{
   uint32_t size;
   const uint8_t* data;
} PinMAMEGetNvramMsg;
