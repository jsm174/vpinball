// license:GPLv3+

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#if defined(ENABLE_OPENGL) && !defined(__STANDALONE__)
// Needed for external capture for VR on Windows
#include <d3d11_1.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

#if defined(ENABLE_BGFX) && !defined(__STANDALONE__)
#include <d3d11_1.h>
#include <dxgi1_6.h>
#endif

#ifdef __STANDALONE__
#define __null 0
#endif

#include <windows.h>

#ifdef USE_DINPUT8
 #define DIRECTINPUT_VERSION 0x0800
#else
 #define DIRECTINPUT_VERSION 0x0700
#endif

#ifdef __STANDALONE__
#define RPC_NO_WINDOWS_H
#endif
#include <dinput.h>

#if defined(ENABLE_DX9)
 #ifdef _DEBUG
  #define D3D_DEBUG_INFO
 #endif
 #include "minid3d9.h"
#endif

#include <mmsystem.h>

#ifndef __STANDALONE__
#include <atlbase.h>
#include <atlctl.h>
#else
#undef PlaySound
extern "C" {
   #include <atlbase.h>
}
#undef strncpy
#define __STDC_WANT_LIB_EXT1__ 1
#include <wchar.h>
#if !defined(_MSC_VER) && !defined(__STDC_LIB_EXT1__)
inline /*errno_t*/int wcscpy_s(wchar_t* __restrict dest, const size_t destsz, const wchar_t* __restrict src) { wcscpy(dest,src); return 0; }
inline /*errno_t*/int wcscpy_s(wchar_t* __restrict dest, const wchar_t* __restrict src) { wcscpy(dest,src); return 0; }
inline /*errno_t*/int wcscat_s(wchar_t* __restrict dest, const size_t destsz, const wchar_t* __restrict src) { wcscat(dest,src); return 0; }
inline /*errno_t*/int wcscat_s(wchar_t* __restrict dest, const wchar_t* __restrict src) { wcscat(dest,src); return 0; }
#endif
#endif

#ifdef _MSC_VER
#define PATH_SEPARATOR_CHAR '\\'
#define PATH_SEPARATOR_WCHAR L'\\'
#else
#define PATH_SEPARATOR_CHAR '/'
#define PATH_SEPARATOR_WCHAR L'/'
#endif
#define PATH_TABLES  (g_pvp->m_myPrefPath + "tables"  + PATH_SEPARATOR_CHAR)
#define PATH_SCRIPTS (g_pvp->m_myPrefPath + "scripts" + PATH_SEPARATOR_CHAR)
#define PATH_MUSIC   (g_pvp->m_myPrefPath + "music"   + PATH_SEPARATOR_CHAR)
#define PATH_USER    (g_pvp->m_myPrefPath + "user"    + PATH_SEPARATOR_CHAR)

#include <oleauto.h>

#include <wincrypt.h>

#ifndef __STANDALONE__
#include <intrin.h>
#endif
#if defined(_M_IX86) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || defined(__i386__) || defined(__i386) || defined(__i486__) || defined(__i486) || defined(i386) || defined(__ia64__) || defined(__x86_64__)
 #define ENABLE_SSE_OPTIMIZATIONS
 #include <xmmintrin.h>
#elif (defined(_M_ARM) || defined(_M_ARM64) || defined(__arm__) || defined(__arm64__) || defined(__aarch64__)) && (!defined(__ARM_ARCH) || __ARM_ARCH >= 7) && (!defined(_MSC_VER) || defined(__clang__)) //!! disable sse2neon if MSVC&non-clang
 #define ENABLE_SSE_OPTIMIZATIONS
 #include "sse2neon.h"
#endif

#ifdef __SSSE3__
 #include <tmmintrin.h>
#endif

#include <vector>
#include <string>
#include <algorithm>
#include <commdlg.h>
#include <dlgs.h>
#include <cderr.h>

using namespace std::string_literals;
using std::string;
using std::wstring;
using std::vector;

// try to load the file from the current directory
// if that fails, try the User, Scripts and Tables sub-directories under where VP was loaded from
// if that also fails, try the standard installation path
static string defaultFileNameSearch[] = { string(), string(), string(), string(), string(), string(), string() };
static const string defaultPathSearch[] = { string(), "user"s +PATH_SEPARATOR_CHAR, "scripts"s +PATH_SEPARATOR_CHAR, "tables"s +PATH_SEPARATOR_CHAR, string(), string(), string() };

#ifndef __STANDALONE__

#ifndef USER_DEFAULT_SCREEN_DPI
  #define USER_DEFAULT_SCREEN_DPI 96 //!! ??
#endif

#ifndef WM_THEMECHANGED
  #define WM_THEMECHANGED            0x031A
#endif

#include <wxx_appcore.h>		// Add CCriticalSection, CObject, CWinThread, CWinApp
//#include <wxx_archive.h>		// Add CArchive
#include <wxx_commondlg.h>		// Add CCommonDialog, CColorDialog, CFileDialog, CFindReplace, CFontDialog 
#include <wxx_scrollview.h>
#include <wxx_controls.h>		// Add CAnimation, CComboBox, CComboBoxEx, CDateTime, CHeader, CHotKey, CIPAddress, CProgressBar, CSpinButton, CScrollBar, CSlider, CToolTip
//#include <wxx_cstring.h>		// Add CString, CStringA, CStringW
//#include <wxx_ddx.h>			// Add CDataExchange
//#include <wxx_dialog.h>			// Add CDialog, CResizer
//#include <wxx_dockframe.h>		// Add CDockFrame, CMDIDockFrame
//#include <wxx_docking.h>		// Add CDocker, CDockContainer
//#include <wxx_exception.h>		// Add CException, CFileException, CNotSupportedException, CResourceException, CUserException, CWinException
//#include <wxx_file.h>			// Add CFile
//#include <wxx_frame.h>			// Add CFrame
//#include <wxx_gdi.h>			// Add CDC, CGDIObject, CBitmap, CBrush, CFont, CPalette, CPen, CRgn
//#include <wxx_imagelist.h>		// Add CImageList
#include <wxx_listview.h>		// Add CListView
//#include <wxx_mdi.h>			// Add CMDIChild, CMDIFrame, CDockMDIFrame
//#include <wxx_printdialogs.h>	// Add CPageSetupDialog, CPrintSetupDialog
//#include <wxx_propertysheet.h>	// Add CPropertyPage, CPropertySheet
//#include <wxx_rebar.h>			// Add CRebar
//#include <wxx_regkey.h>			// Add CRegKey
//#include <wxx_ribbon.h>		// Add CRibbon, CRibbonFrame
//#include <wxx_richedit.h>		// Add CRichEdit
//#include <wxx_socket.h>			// Add CSocket
//#include <wxx_statusbar.h>		// Add CStatusBar
#include <wxx_stdcontrols.h>	// Add CButton, CEdit, CListBox
//#include <wxx_tab.h>			// Add CTab, CTabbedMDI
//#include <wxx_taskdialog.h>	// Add CTaskDialog
//#include <wxx_time.h>			// Add CTime
//#include <wxx_toolbar.h>		// Add CToolBar
#include <wxx_treeview.h>		// Add CTreeView
//#include <wxx_webbrowser.h>		// Add CAXWindow, CWebBrowser
//#include <wxx_wincore.h>
#endif

#ifdef __STANDALONE__
#define fopen_s(pFile, filename, mode) (((*(pFile)) = fopen((filename), (mode))) == nullptr)
#define fprintf_s fprintf
#define fread_s(buffer, bufferSize, elementSize, count, stream) fread(buffer, bufferSize, count, stream)
#define fscanf_s fscanf

#define sscanf_s sscanf

#define localtime_s(x, y) localtime_r(y, x)
#define gmtime_s(x, y) gmtime_r(y, x)

#define _aligned_malloc(size, align) aligned_alloc(align, size)
#define _aligned_free free

#undef lstrcmpi
#define lstrcmpi lstrcmpiA

#undef lstrcmp
#define lstrcmp lstrcmpA

#define strcpy_s(A, B, C) strncpy(A, C, B)
#define strnlen_s strnlen
#define sprintf_s snprintf
#define _snprintf_s snprintf
#define swprintf_s swprintf
#define StrStrI strcasestr

#define STRNCPY_S3(a, b, c) strncpy(a, b, c)
#define STRNCPY_S4(a, b, c, d) strncpy(a, c, d)
#define GET_MACRO(_1, _2, _3, _4, NAME, ...) NAME
#define strncpy_s(...) \
  GET_MACRO(__VA_ARGS__, STRNCPY_S4, STRNCPY_S3)(__VA_ARGS__)

#define _T(x) (x)
#define AtoT(x) (x)
#define _ASSERTE(expr) ((void)0)

#undef GetCurrentDirectory
#define GetCurrentDirectory GetCurrentDirectoryA

#undef SetCurrentDirectory
#define SetCurrentDirectory SetCurrentDirectoryA

#undef GetModuleFileName
#define GetModuleFileName GetModuleFileNameA

#undef MessageBox
#define MessageBox MessageBoxA

#undef OutputDebugString
#define OutputDebugString OutputDebugStringA

#undef CharLowerBuff
#define CharLowerBuff CharLowerBuffA

#define FINDREPLACE FINDREPLACEA
#define CREATESTRUCT CREATESTRUCTA
#define WNDCLASS WNDCLASSA
#define LOGFONT LOGFONTA
#define MONITORINFOEX MONITORINFOEXA

typedef LPSTR LPTSTR;
typedef LPCSTR LPCTSTR;

class SearchSelectDialog final { };
class LayersListDialog final { };
class ImageDialog final { };
class SoundDialog final { };
class AudioOptionsDialog final { };
class VideoOptionsDialog final { };
class VROptionsDialog final { };
class EditorOptionsDialog final { };
class CollectionManagerDialog final { };
class PhysicsOptionsDialog final { };
class TableInfoDialog final { };
class DimensionDialog final { };
class RenderProbeDialog final { };
class MaterialDialog final { };
class AboutDialog final { };
class ToolbarDialog final { };
class NotesDialog final { };
class PropertyDialog final { };
class ColorButton final { };
class SCNotification final { };
#endif

#include "utils/Logger.h"

#ifdef __STANDALONE__
#include "standalone/inc/atl/atldef.h"
#include "standalone/inc/atl/atlbase.h"
#include "standalone/inc/atl/atlcom.h"
#include "standalone/inc/atl/atlcomcli.h"
#include "standalone/inc/atl/atlsafe.h"

#include "standalone/inc/atlmfc/afx.h"
#include "standalone/inc/atlmfc/afxdlgs.h"
#include "standalone/inc/atlmfc/afxwin.h"
#include "standalone/inc/atlmfc/atltypes.h"

#include "standalone/inc/win32xx/win32xx.h"

#include <cstdint>
#endif

#include "def.h"

#include "math/math.h"
#include "math/vector.h"
#include "math/matrix.h"
#include "math/bbox.h"

#include "ui/resource.h"

#include "utils/memutil.h"

#include "dispid.h"

#include "utils/variant.h"
#include "utils/vector.h"
#include "utils/vectorsort.h"
#ifndef __STANDALONE__
#include "vpinball.h"
#else
#include "standalone/vpinball_standalone_i.h"
#endif
#include "core/Settings.h"

#include "utils/wintimer.h"

#include "utils/eventproxy.h"

#include "ui/worker.h"

#include "utils/fileio.h"
#include "pinundo.h"
#include "iselect.h"

#include "ieditable.h"
#include "ui/codeview.h"

#include "utils/lzwreader.h"
#include "utils/lzwwriter.h"

#include "parts/Sound.h"
#include "parts/pinbinary.h"

#include "plugins/MsgPluginManager.h"

#include "extern.h"

#include "core/vpinball_h.h"
#include "parts/pintable.h"

#include "math/mesh.h"
#include "physics/collide.h"
#include "renderer/Renderer.h"

#include "ui/sur.h"
#include "ui/paintsur.h"
#include "ui/hitsur.h"
#include "ui/hitrectsur.h"

#include "parts/ball.h"

#include "physics/collideex.h"
#include "physics/hitball.h"
#include "physics/hittimer.h"
#include "physics/hitable.h"
#include "physics/hitflipper.h"
#include "physics/hitplunger.h"
#include "core/player.h"

#include "utils/color.h"

#include "parts/dragpoint.h"
#include "parts/timer.h"
#include "parts/flipper.h"
#include "parts/plunger.h"
#include "parts/textbox.h"
#include "parts/surface.h"
#include "parts/dispreel.h"
#include "parts/lightseq.h"
#include "parts/bumper.h"
#include "parts/trigger.h"
#include "parts/light.h"
#include "parts/kicker.h"
#include "parts/decal.h"
#include "parts/primitive.h"
#include "parts/hittarget.h"
#include "parts/gate.h"
#include "parts/spinner.h"
#include "parts/ramp.h"
#include "parts/flasher.h"
#include "parts/rubber.h"
#include "parts/PartGroup.h"

#include "utils/ushock_output.h"

#include "physics/kdtree.h"

#include "renderer/trace.h"
#include "renderer/Window.h"

inline void ShowError(const char* const sz)
{
   if(g_pvp)
      g_pvp->MessageBox(sz, "Visual Pinball Error", MB_OK | MB_ICONEXCLAMATION);
   else
      MessageBox(nullptr, sz, "Visual Pinball Error", MB_OK | MB_ICONEXCLAMATION);
}

inline void ShowError(const string& sz)
{
   if(g_pvp)
      g_pvp->MessageBox(sz.c_str(), "Visual Pinball Error", MB_OK | MB_ICONEXCLAMATION);
   else
      MessageBox(nullptr, sz.c_str(), "Visual Pinball Error", MB_OK | MB_ICONEXCLAMATION);
}

#include "editablereg.h"
