// license:GPLv3+

#pragma once

// Disable Warning C4635: XML document comment target: badly-formed XML
#pragma warning(disable : 4635)

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED // https://wiki.libsdl.org/SDL3/SDL_SetMainReady#remarks
#endif

//#define DISABLE_FORCE_NVIDIA_OPTIMUS // do not enable NVIDIA Optimus cards (on Laptops, etc) by default

//#define DISABLE_FORCE_AMD_HIGHPERF // do not enable AMD high performance device (on Laptops, etc) by default

#if defined(ENABLE_OPENGL) || defined(__STANDALONE__)
#define DISABLE_FORCE_NVIDIA_OPTIMUS
#endif

//#define TWOSIDED_TRANSPARENCY // transparent hit targets are rendered backsided first, then frontsided

// Needed by ImPlot when using ImGUI
#define IMGUI_DEFINE_MATH_OPERATORS

// Needed to avoid duplicate definitions between ImGui and GLAD
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM


#define COMPRESS_MESHES // uses miniz for compressing the meshes

#ifndef __STANDALONE__
#define CRASH_HANDLER
#endif

//#define _CRTDBG_MAP_ALLOC

//#define DEBUG_NUDGE // debug new nudge code

//#define DEBUG_NO_SOUND
//#define DEBUG_REFCOUNT_TRIGGER

//#define ENABLE_TRACE // enables all TRACE_FUNCTION() calls to use D3DPERF_Begin/EndEvent

//#define DEBUG_XXX // helps to detect out-of-bounds access, needs to link dbghelp.lib then

#define EDITOR_BG_WIDTH    1000
#define EDITOR_BG_HEIGHT   750

#define MAIN_WINDOW_WIDTH  1280
#define MAIN_WINDOW_HEIGHT (720-50)

#define BASEDEPTHBIAS 5e-5f

#define THREADS_PAUSE 1000 // msecs/time to wait for threads to finish up

#include "physics/physconst.h"

//

#define MAX_BALL_TRAIL_POS 10 // fake/artistic ball motion trail

#define MAX_REELS          32

#define LIGHTSEQGRIDSCALE  20
#define	LIGHTSEQGRIDWIDTH  (EDITOR_BG_WIDTH/LIGHTSEQGRIDSCALE)
#define	LIGHTSEQGRIDHEIGHT ((2*EDITOR_BG_WIDTH)/LIGHTSEQGRIDSCALE)

#define LIGHTSEQQUEUESIZE  100

#define MAX_LIGHT_SOURCES  2
#define MAX_BALL_LIGHT_SOURCES  8

//

#define ADAPT_VSYNC_FACTOR 0.95 // safety factor where vsync is turned off (f.e. drops below 60fps * 0.95 = 57fps)

#define ACCURATETIMERS          // if undefd, timers will only be triggered as often as frames are rendered (e.g. they can fall behind)
#define MAX_TIMER_MSEC_INTERVAL 1 // amount of msecs to wait (at least) until same timer can be triggered again (e.g. they can fall behind, if set to > 1, as update cycle is 1000Hz)
#define MAX_TIMERS_MSEC_OVERALL 5 // amount of msecs that all timers combined can take per frame (e.g. they can fall behind, if set to < somelargevalue)

//#define PLAYBACK              // bitrotted, also how to record the playback to c:\badlog.txt ?? via LOG ??
//#define LOG                   // bitrotted, will record stuff into c:\log.txt

//#define DEBUGPHYSICS          // enables detailed physics/collision handling output for the 'F11' stats/debug texts

#define DEBUG_BALL_SPIN         // enables dots glued to balls if in 'F11' mode

//

#define LAST_OPENED_TABLE_COUNT 8

#define MAX_CUSTOM_PARAM_INDEX  9

#define MAX_OPEN_TABLES         9

#define AUTOSAVE_DEFAULT_TIME   10

#define DEFAULT_SECURITY_LEVEL  0

#define NUM_ASSIGN_LAYERS       20

//VR Support

// No VR support for DX9, BGFX is in progress, Metal VR support under BGFX is still to be implemented
//#if !defined(__STANDALONE__) && (defined(ENABLE_OPENGL) || defined(ENABLE_BGFX)) && !defined(__APPLE__)
#if !defined(__STANDALONE__) && defined(ENABLE_OPENGL)
#define ENABLE_VR
#endif
#if !defined(__STANDALONE__) && defined(ENABLE_BGFX)
#define ENABLE_XR
#endif

//

#if !defined(AFX_STDAFX_H__35BEBBA5_0A4C_4321_A65C_AFFE89589F15__INCLUDED_)
#define AFX_STDAFX_H__35BEBBA5_0A4C_4321_A65C_AFFE89589F15__INCLUDED_

#define _WINSOCKAPI_ // workaround some issue where windows.h is included before winsock2.h in some of the various includes

// Attempt to speed up STL which is very CPU costly, maybe we should look into using EASTL instead? http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2271.html https://github.com/electronicarts/EASTL
// Disabled as this causes inconsistencies when linking but still STL debug is very CPU heavy
//#if !defined(ENABLE_BGFX) || !defined(_DEBUG)
//#define _SECURE_SCL 0
//#define _HAS_ITERATOR_DEBUGGING 0
//#endif

#define STRICT

#ifndef _WIN32_WINNT
  /*#if defined(ENABLE_DX9) // XP-compatibility / old SDK/toolset
    #if defined(_WIN64) && defined(CRASH_HANDLER)
      // Windows XP _WIN32_WINNT_WINXP
      #define _WIN32_WINNT 0x0501
    #else
      // Windows 2000 _WIN32_WINNT_WIN2K
      #define _WIN32_WINNT 0x0500
    #endif
  #else*/
    // Windows Vista _WIN32_WINNT_VISTA
    #define _WIN32_WINNT 0x0600
  //#endif

  #define WINVER _WIN32_WINNT
#endif


#define _ATL_APARTMENT_THREADED

#ifndef __STANDALONE__
#ifndef APPX_E_BLOCK_HASH_INVALID
#define APPX_E_BLOCK_HASH_INVALID _HRESULT_TYPEDEF_(0x80080207L)
#endif
#ifndef APPX_E_CORRUPT_CONTENT
#define APPX_E_CORRUPT_CONTENT _HRESULT_TYPEDEF_(0x80080206L)
#endif
#endif

//#include <vld.h>
#ifdef _CRTDBG_MAP_ALLOC
 #include <crtdbg.h>
#endif

#include "main.h"

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__35BEBBA5_0A4C_4321_A65C_AFFE89589F15__INCLUDED)
