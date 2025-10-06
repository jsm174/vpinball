package org.vpinball.app.jni;

import org.vpinball.app.VPinballManager;

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
    public native int VPinballResetIni();
    public native void VPinballUpdateWebServer();
    public native int VPinballLoadTable(String uuid);
    public native int VPinballExtractTableScript();
    public native int VPinballPlay();
    public native void VPinballStop();
    public native String VPinballExportTable(String uuid);
    public native boolean VPinballFileExists(String path);
    public native boolean VPinballDeleteFile(String path);
    public native boolean VPinballCopyFile(String sourcePath, String destPath);
    public native String VPinballStageFile(String path);

    public native int VPinballRefreshTables();
    public native String VPinballGetTables();
    public native int VPinballDeleteTable(String uuid);
    public native int VPinballRenameTable(String uuid, String newName);
    public native int VPinballImportTable(String sourceFile);
    public native int VPinballSetTableImage(String uuid, String imagePath);
    public native String VPinballGetTablesPath();

    public static boolean SAFWriteFile(String relativePath, String content) {
        return VPinballManager.safFileSystem.writeFile(relativePath, content);
    }

    public static String SAFReadFile(String relativePath) {
        return VPinballManager.safFileSystem.readFile(relativePath);
    }

    public static boolean SAFExists(String relativePath) {
        return VPinballManager.safFileSystem.exists(relativePath);
    }

    public static String SAFListFiles(String extension) {
        return VPinballManager.safFileSystem.listFilesRecursive(extension);
    }

    public static boolean SAFCopyFile(String sourcePath, String destPath) {
        return VPinballManager.safFileSystem.copyFile(sourcePath, destPath);
    }

    public static boolean SAFCopyDirectory(String sourcePath, String destPath) {
        return VPinballManager.safFileSystem.copyDirectory(sourcePath, destPath);
    }

    public static boolean SAFCopySAFToFilesystem(String safRelativePath, String destPath) {
        return VPinballManager.safFileSystem.copySAFToFilesystem(safRelativePath, destPath);
    }

    public static boolean SAFDelete(String relativePath) {
        return VPinballManager.safFileSystem.delete(relativePath);
    }

    public static boolean SAFIsDirectory(String relativePath) {
        return VPinballManager.safFileSystem.isDirectory(relativePath);
    }

    public static String SAFListDirectory(String relativePath) {
        return VPinballManager.safFileSystem.listDirectory(relativePath);
    }
}