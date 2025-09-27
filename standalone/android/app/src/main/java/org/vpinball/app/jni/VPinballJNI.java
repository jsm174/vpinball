package org.vpinball.app.jni;

public class VPinballJNI {
    public native String VPinballGetVersionStringFull();
    public native void VPinballSetEventCallback(VPinballEventCallback callback);
    public native void VPinballLog(int level, String message);
    public native void VPinballResetLog();
    public native int VPinballLoadValueInt(String sectionName, String key, int defaultValue);
    public native float VPinballLoadValueFloat(String sectionName, String key, float defaultValue);
    public native String VPinballLoadValueString(String sectionName, String key, String defaultValue);
    public native void VPinballSaveValueInt(String sectionName, String key, int value);
    public native void VPinballSaveValueFloat(String sectionName, String key, float value);
    public native void VPinballSaveValueString(String sectionName, String key, String value);
    public native String VPinballExportTable(String uuid);
    public native int VPinballResetIni();
    public native int VPinballLoad(String uuid);
    public native int VPinballExtractScript();
    public native int VPinballPlay();
    public native void VPinballStop();
    public native boolean VPinballFileExists(String path);
    public native String VPinballPrepareFileForViewing(String path);
    public native void VPinballToggleFPS();
    // public native int VPinballCaptureScreenshot(String filename);

    public native int VPinballRefreshTables();
    public native int VPinballReloadTablesPath();
    public native String VPinballGetTables();
    public native int VPinballAddVPXTable(String filePath);
    public native int VPinballDeleteTable(String uuid);
    public native int VPinballRenameTable(String uuid, String newName);
    public native int VPinballImportTable(String sourceFile);
    public native int VPinballSetTableArtwork(String uuid, String artworkPath);
    public native String VPinballGetTablesPath();
    public native void VPinballFreeString(String jsonString);

    // SAF proxy functions - called FROM C++ TO Java/Kotlin
    // These are static methods that forward to VPinballManager
    public static boolean SAFWriteFile(String relativePath, String content) {
        return org.vpinball.app.VPinballManager.INSTANCE.safWriteFile(relativePath, content);
    }

    public static String SAFReadFile(String relativePath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safReadFile(relativePath);
    }

    public static boolean SAFExists(String relativePath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safExists(relativePath);
    }

    public static String SAFListFiles(String extension) {
        return org.vpinball.app.VPinballManager.INSTANCE.safListFilesRecursive(extension);
    }

    public static boolean SAFCopyFile(String sourcePath, String destPath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safCopyFile(sourcePath, destPath);
    }

    public static boolean SAFCopyDirectory(String sourcePath, String destPath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safCopyDirectory(sourcePath, destPath);
    }

    public static boolean SAFCopySAFToFilesystem(String safRelativePath, String destPath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safCopySAFToFilesystem(safRelativePath, destPath);
    }

    public static boolean SAFDelete(String relativePath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safDelete(relativePath);
    }

    public static boolean SAFIsDirectory(String relativePath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safIsDirectory(relativePath);
    }

    public static String SAFListDirectory(String relativePath) {
        return org.vpinball.app.VPinballManager.INSTANCE.safListDirectory(relativePath);
    }
}