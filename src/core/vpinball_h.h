// license:GPLv3+

// interface for the VPinball class.

#pragma once

#include "core/Settings.h"
#include "renderer/RenderDevice.h"

#include "parts/Sound.h"
#ifndef __STANDALONE__
   #include <wxx_dockframe.h>
   #include "ui/dialogs/ImageDialog.h"
   #include "ui/dialogs/SoundDialog.h"
   #include "ui/dialogs/EditorOptionsDialog.h"
   #include "ui/dialogs/VideoOptionsDialog.h"
   #include "ui/dialogs/AudioOptionsDialog.h"
   #include "ui/dialogs/CollectionManagerDialog.h"
   #include "ui/dialogs/PhysicsOptionsDialog.h"
   #include "ui/dialogs/RenderProbeDialog.h"
   #include "ui/dialogs/TableInfoDialog.h"
   #include "ui/dialogs/DimensionDialog.h"
   #include "ui/dialogs/MaterialDialog.h"
   #include "ui/dialogs/SoundDialog.h"
   #include "ui/dialogs/AboutDialog.h"
   #include "ui/dialogs/DrawingOrderDialog.h"
   #include "ui/dialogs/ToolbarDialog.h"
   #include "ui/dialogs/LayersListDialog.h"
   #include "ui/dialogs/NotesDialog.h"
   #include "ui/properties/PropertyDialog.h"
   #if defined(ENABLE_VR) || defined(ENABLE_XR)
      #include "ui/dialogs/VROptionsDialog.h"
   #endif

   #define OVERRIDE override
#else
   #define OVERRIDE
#endif

class PinTable;
class PinTableMDI;
class VPXFileFeedback;

class VPinball final : public CMDIDockFrame
{
public:
    enum TIMER_IDS
    {
        TIMER_ID_AUTOSAVE = 12345,
        TIMER_ID_CLOSE_TABLE = 12346
    };

    enum CopyPasteModes
    {
        COPY = 0,
        PASTE = 1,
        PASTE_AT = 2
    };

   VPinball();
   ~VPinball() OVERRIDE;

   void ShowSubDialog(CDialog& dlg, const bool show);

   void SetLogicalNumberOfProcessors(int procNumber);
   int GetLogicalNumberOfProcessors() const;

private:
   void ShowSearchSelect();
   void SetDefaultPhysics();
   void SetViewSolidOutline(size_t viewId);
   void ShowGridView();
   void ShowBackdropView();
   void AddControlPoint();
   void AddSmoothControlPoint();
   void SaveTable(const bool saveAs);
   void OpenNewTable(size_t tableId);
   void ProcessDeleteElement();
   void OpenRecentFile(const size_t menuId);
   void CopyPasteElement(const CopyPasteModes mode);
   void InitTools();
   void InitRegValues();
   bool CanClose();
   void GetMyPath();
   void UpdateRecentFileList(const string& filename);

public:
   void GetMyPrefPath();
   void AddMDITable(PinTableMDI* mdiTable);
   CMenu GetMainMenu(int id);
   void CloseAllDialogs();
   void ToggleScriptEditor();
   void ToggleBackglassView();
   bool ParseCommand(const size_t code, const bool notify);

   CComObject<PinTable> *GetActiveTable();
   bool LoadFile(const bool updateEditor, VPXFileFeedback* feedback = nullptr);
   void LoadFileName(const string& szFileName, const bool updateEditor, VPXFileFeedback* feedback = nullptr);
   void SetClipboard(vector<IStream*> * const pvstm);

   void DoPlay(const int playMode);

   void SetPosCur(float x, float y);
   void SetObjectPosCur(float x, float y);
   void ClearObjectPosCur();
   float ConvertToUnit(const float value) const;
   void SetPropSel(VectorProtected<ISelect> &pvsel);

   void SetActionCur(const string& szaction);
   void SetCursorCur(HINSTANCE hInstance, LPCTSTR lpCursorName);

   void GenerateTournamentFile();
   void GenerateImageFromTournamentFile(const string& tablefile, const string& txtfile);

   STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj);
   STDMETHOD_(ULONG, AddRef)();
   STDMETHOD_(ULONG, Release)();

   STDMETHOD(PlaySound)(BSTR bstr);

   STDMETHOD(FireKnocker)(int Count);
   STDMETHOD(QuitPlayer)(int CloseType);

   void CloseTable(const PinTable * const ppt);

   void ToggleToolbar();
   void SetEnableMenuItems();

   HANDLE PostWorkToWorkerThread(int workid, LPARAM lParam);

   void SetAutoSaveMinutes(const int minutes);
   void ShowDrawingOrderDialog(bool select);

   void SetStatusBarElementInfo(const string& info);
   void SetStatusBarUnitInfo(const string& info, const bool isUnit) // inlined, in the hope that string conversions will be skipped in case of early out in here
   {
    if (g_pplayer)
        return;

    string textBuf;

    if (!info.empty())
    {
       textBuf = info;
       if(isUnit)
       {
           switch(m_convertToUnit)
           {
               case 0:
               {
                   textBuf += " (inch)";
                   break;
               }
               case 1:
               {
                   textBuf += " (mm)";
                   break;
               }
               case 2:
               {
                   textBuf += " (VPUnits)";
                   break;
               }
               default:
                   assert(!"wrong unit");
               break;
           }
       }
    }

#ifndef __STANDALONE__
    ::SendMessage(m_hwndStatusBar, SB_SETTEXT, 5 | 0, (size_t)textBuf.c_str());
#endif
   }

   bool OpenFileDialog(const string& initDir, vector<string>& filename, const char* const fileFilter, const char* const defaultExt, const DWORD flags, const string& windowTitle = string());
   bool SaveFileDialog(const string& initDir, vector<string>& filename, const char* const fileFilter, const char* const defaultExt, const DWORD flags, const string& windowTitle = string());

   CDockProperty* GetPropertiesDocker();
   CDockToolbar *GetToolbarDocker();
   CDockNotes* GetNotesDocker();
   CDockLayers* GetLayersDocker();
   void ResetAllDockers();

   void DestroyNotesDocker()
   {
      m_notesDialog = nullptr;
      m_dockNotes = nullptr;
   }
   void CreateDocker();
   #ifndef __STANDALONE__
   LayersListDialog* GetLayersListDialog() { return GetLayersDocker()->GetContainLayers()->GetLayersDialog(); }
   #endif
   bool IsClosing() const { return m_closing; }

   ULONG m_cref;

   HINSTANCE theInstance;

   // registered window message ID for PinSim::FrontEndControls
   // (http://mjrnet.org/pinscape/PinSimFrontEndControls/PinSimFrontEndControls.htm)
   UINT m_pinSimFrontEndControlsMsg;

   // handler for PinSim::FrontEndControls messages
   LRESULT OnFrontEndControlsMsg(WPARAM wParam, LPARAM lParam);

   vector< CComObject<PinTable>* > m_vtable;
   CComObject<PinTable> *m_ptableActive;

//    HWND m_hwndToolbarMain;
   HWND m_hwndStatusBar;

   int m_palettescroll;

   vector<IStream*> m_vstmclipboard;

   int m_ToolCur; // palette button currently pressed

   int m_NextTableID; // counter to create next unique table name

   CodeViewer *m_pcv; // currently active code window

   bool m_backglassView = false; // whether viewing the playfield or screen layout

   bool m_alwaysDrawDragPoints;
   bool m_alwaysDrawLightCenters;
   int m_gridSize;
   int m_convertToUnit; // 0=Inches, 1=Millimeters, 2=VPUnits

   int m_securitylevel;

   string m_myPath;
   wstring m_wMyPath;
   string m_myPrefPath;
   string m_currentTablePath;

   int m_autosaveTime;

   Material m_dummyMaterial;
   COLORREF m_elemSelectColor;
   COLORREF m_elemSelectLockedColor;
   COLORREF m_backgroundColor;
   COLORREF m_fillColor;
   Vertex2D m_mouseCursorPosition;

   // overall app settings
   Settings m_settings;

   // command line parameters
   int m_disEnableTrueFullscreen;
   bool m_open_minimized;
   bool m_disable_pause_menu;
   bool m_povEdit; // table should be run in camera mode to change the POV (and then export that on exit), nothing else
   bool m_primaryDisplay; // force use of pixel(0,0) monitor
   bool m_table_played_via_command_line;
   volatile bool m_table_played_via_SelectTableOnStart;
   bool m_bgles = false; // override global emission scale by m_fgles below?
   float m_fgles = 0.f;
   wstring m_customParameters[MAX_CUSTOM_PARAM_INDEX];

   HBITMAP m_hbmInPlayMode;

protected:
   void PreCreate(CREATESTRUCT& cs) override;
   void PreRegisterClass(WNDCLASS& wc) override;
   void OnClose() override;
   void OnDestroy() OVERRIDE;
   int  OnCreate(CREATESTRUCT& cs) override;
   LRESULT OnPaint(UINT msg, WPARAM wparam, LPARAM lparam) OVERRIDE;
   void OnInitialUpdate() override;
   BOOL OnCommand(WPARAM wparam, LPARAM lparam) override;
   LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
   LRESULT OnMDIActivated(UINT msg, WPARAM wparam, LPARAM lparam) OVERRIDE;
   LRESULT OnMDIDestroyed(UINT msg, WPARAM wparam, LPARAM lparam) OVERRIDE;
#ifndef __STANDALONE__
   DockPtr NewDockerFromID(int id) override;
#endif

private:

   CDockNotes *GetDefaultNotesDocker();

   volatile bool m_unloadingTable;
   //CMenu m_mainMenu;
   vector<string> m_recentTableList;

   int m_logicalNumberOfProcessors;
   HANDLE  m_workerthread;
   unsigned int m_workerthreadid;
   bool    m_closing;
   HMODULE m_scintillaDll;

   ImageDialog m_imageMngDlg;
   SoundDialog m_soundMngDlg;
   AudioOptionsDialog m_audioOptDialog;
   EditorOptionsDialog m_editorOptDialog;
   CollectionManagerDialog m_collectionMngDlg;
   PhysicsOptionsDialog m_physicsOptDialog;
   TableInfoDialog m_tableInfoDialog;
   DimensionDialog m_dimensionDialog;
   RenderProbeDialog m_renderProbeDialog;
   MaterialDialog m_materialDialog;
   AboutDialog m_aboutDialog;
   #if defined(ENABLE_VR) || defined(ENABLE_XR)
      VROptionsDialog m_vrOptDialog;
   #endif

   ToolbarDialog *m_toolbarDialog = nullptr;
   PropertyDialog *m_propertyDialog = nullptr;
   CDockToolbar *m_dockToolbar = nullptr;
   CDockProperty *m_dockProperties = nullptr;
   CDockLayers *m_dockLayers = nullptr;
   NotesDialog *m_notesDialog = nullptr;
   CDockNotes* m_dockNotes = nullptr;
};
