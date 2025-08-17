package org.vpinball.app.jni;

public class VPinballJNI {
    public native String VPinballGetVersionStringFull();
    public native void VPinballInit(VPinballEventCallback callback);
    public native void VPinballLog(int level, String message);
    public native void VPinballResetLog();
    public native int VPinballLoadValueInt(String sectionName, String key, int defaultValue);
    public native float VPinballLoadValueFloat(String sectionName, String key, float defaultValue);
    public native String VPinballLoadValueString(String sectionName, String key, String defaultValue);
    public native void VPinballSaveValueInt(String sectionName, String key, int value);
    public native void VPinballSaveValueFloat(String sectionName, String key, float value);
    public native void VPinballSaveValueString(String sectionName, String key, String value);
    public native String VPinballExportTable(String uuid);
    public native void VPinballUpdateWebServer();
    public native int VPinballResetIni();
    public native int VPinballLoad(String source);
    public native int VPinballExtractScript(String source);
    public native int VPinballPlay();
    public native void VPinballStop();
    public native void VPinballToggleFPS();
    public native void VPinballSetPlayState(int enable);
    public native int VPinballGetCustomTableOptionsCount();
    public native String VPinballGetCustomTableOptions();
    public native void VPinballSetCustomTableOption(String jsonOption);
    public native void VPinballResetCustomTableOptions();
    public native void VPinballSaveCustomTableOptions();
    public native String VPinballGetTableOptions();
    public native void VPinballSetTableOptions(String jsonOptions);
    public native void VPinballResetTableOptions();
    public native void VPinballSaveTableOptions();
    public native String VPinballGetViewSetup();
    public native void VPinballSetViewSetup(String jsonSetup);
    public native void VPinballSetDefaultViewSetup();
    public native void VPinballResetViewSetup();
    public native void VPinballSaveViewSetup();
    public native int VPinballCaptureScreenshot(String filename);
    public native VPinballTableEventData VPinballGetTableEventData();
    public native void VPinballSetTableEventDataSuccess(boolean success);
    public native void VPinballSetWebServerUpdated();
    
    // VPX Table Management Functions
    public native int VPinballRefreshTables();
    public native String VPinballGetVPXTables();
    public native String VPinballGetVPXTable(String uuid);
    public native int VPinballAddVPXTable(String filePath);
    public native int VPinballRemoveVPXTable(String uuid);
    public native int VPinballRenameVPXTable(String uuid, String newName);
    public native int VPinballImportTableFile(String sourceFile);
    public native int VPinballSetTableArtwork(String uuid, String artworkPath);
    public native String VPinballGetTablesPath();
    public native void VPinballFreeString(String jsonString);
}