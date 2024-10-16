#include <jni.h>
#include "VPinball.h"

extern "C" {

JNIEXPORT jstring JNICALL Java_VPinballJNI_VPinballGetVersionStringFull(JNIEnv* env, jobject obj) {
    return env->NewStringUTF(VPinballGetVersionStringFull());
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballInit(JNIEnv* env, jobject obj, jobject callback) {
    // Implement callback handling
    VPinballInit(nullptr);
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballLog(JNIEnv* env, jobject obj, jint level, jstring message) {
    const char* msg = env->GetStringUTFChars(message, nullptr);
    VPinballLog(static_cast<VPINBALL_LOG_LEVEL>(level), msg);
    env->ReleaseStringUTFChars(message, msg);
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballResetLog(JNIEnv* env, jobject obj) {
    VPinballResetLog();
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballLoadValueInt(JNIEnv* env, jobject obj, jint section, jstring key, jint defaultValue) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    int result = VPinballLoadValueInt(static_cast<VPINBALL_SETTINGS_SECTION>(section), k, defaultValue);
    env->ReleaseStringUTFChars(key, k);
    return result;
}

JNIEXPORT jfloat JNICALL Java_VPinballJNI_VPinballLoadValueFloat(JNIEnv* env, jobject obj, jint section, jstring key, jfloat defaultValue) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    float result = VPinballLoadValueFloat(static_cast<VPINBALL_SETTINGS_SECTION>(section), k, defaultValue);
    env->ReleaseStringUTFChars(key, k);
    return result;
}

JNIEXPORT jstring JNICALL Java_VPinballJNI_VPinballLoadValueString(JNIEnv* env, jobject obj, jint section, jstring key, jstring defaultValue) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    const char* def = env->GetStringUTFChars(defaultValue, nullptr);
    const char* result = VPinballLoadValueString(static_cast<VPINBALL_SETTINGS_SECTION>(section), k, def);
    env->ReleaseStringUTFChars(key, k);
    env->ReleaseStringUTFChars(defaultValue, def);
    return env->NewStringUTF(result);
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSaveValueInt(JNIEnv* env, jobject obj, jint section, jstring key, jint value) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    VPinballSaveValueInt(static_cast<VPINBALL_SETTINGS_SECTION>(section), k, value);
    env->ReleaseStringUTFChars(key, k);
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSaveValueFloat(JNIEnv* env, jobject obj, jint section, jstring key, jfloat value) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    VPinballSaveValueFloat(static_cast<VPINBALL_SETTINGS_SECTION>(section), k, value);
    env->ReleaseStringUTFChars(key, k);
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSaveValueString(JNIEnv* env, jobject obj, jint section, jstring key, jstring value) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    const char* v = env->GetStringUTFChars(value, nullptr);
    VPinballSaveValueString(static_cast<VPINBALL_SETTINGS_SECTION>(section), k, v);
    env->ReleaseStringUTFChars(key, k);
    env->ReleaseStringUTFChars(value, v);
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballUncompress(JNIEnv* env, jobject obj, jstring source) {
    const char* src = env->GetStringUTFChars(source, nullptr);
    VPINBALL_STATUS status = VPinballUncompress(src);
    env->ReleaseStringUTFChars(source, src);
    return status;
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballCompress(JNIEnv* env, jobject obj, jstring source, jstring destination) {
    const char* src = env->GetStringUTFChars(source, nullptr);
    const char* dest = env->GetStringUTFChars(destination, nullptr);
    VPINBALL_STATUS status = VPinballCompress(src, dest);
    env->ReleaseStringUTFChars(source, src);
    env->ReleaseStringUTFChars(destination, dest);
    return status;
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballUpdateWebServer(JNIEnv* env, jobject obj) {
    VPinballUpdateWebServer();
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballResetIni(JNIEnv* env, jobject obj) {
    return VPinballResetIni();
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballLoad(JNIEnv* env, jobject obj, jstring source) {
    const char* src = env->GetStringUTFChars(source, nullptr);
    VPINBALL_STATUS status = VPinballLoad(src);
    env->ReleaseStringUTFChars(source, src);
    return status;
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballExtractScript(JNIEnv* env, jobject obj, jstring source) {
    const char* src = env->GetStringUTFChars(source, nullptr);
    VPINBALL_STATUS status = VPinballExtractScript(src);
    env->ReleaseStringUTFChars(source, src);
    return status;
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballPlay(JNIEnv* env, jobject obj) {
    return VPinballPlay();
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballStop(JNIEnv* env, jobject obj) {
    return VPinballStop();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSetPlayState(JNIEnv* env, jobject obj, jint enable) {
    VPinballSetPlayState(enable);
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballToggleFPS(JNIEnv* env, jobject obj) {
    VPinballToggleFPS();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballGetTableOptions(JNIEnv* env, jobject obj, jobject options) {
    // Implement conversion from jobject to VPinballTableOptions and call VPinballGetTableOptions
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSetTableOptions(JNIEnv* env, jobject obj, jobject options) {
    // Implement conversion from jobject to VPinballTableOptions and call VPinballSetTableOptions
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSetDefaultTableOptions(JNIEnv* env, jobject obj) {
    VPinballSetDefaultTableOptions();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballResetTableOptions(JNIEnv* env, jobject obj) {
    VPinballResetTableOptions();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSaveTableOptions(JNIEnv* env, jobject obj) {
    VPinballSaveTableOptions();
}

JNIEXPORT jint JNICALL Java_VPinballJNI_VPinballGetCustomTableOptionsCount(JNIEnv* env, jobject obj) {
    return VPinballGetCustomTableOptionsCount();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballGetCustomTableOption(JNIEnv* env, jobject obj, jint index, jobject option) {
    // Implement conversion from jobject to VPinballCustomTableOption and call VPinballGetCustomTableOption
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSetCustomTableOption(JNIEnv* env, jobject obj, jobject option) {
    // Implement conversion from jobject to VPinballCustomTableOption and call VPinballSetCustomTableOption
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSetDefaultCustomTableOptions(JNIEnv* env, jobject obj) {
    VPinballSetDefaultCustomTableOptions();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballResetCustomTableOptions(JNIEnv* env, jobject obj) {
    VPinballResetCustomTableOptions();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSaveCustomTableOptions(JNIEnv* env, jobject obj) {
    VPinballSaveCustomTableOptions();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballGetViewSetup(JNIEnv* env, jobject obj, jobject setup) {
    // Implement conversion from jobject to VPinballViewSetup and call VPinballGetViewSetup
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSetViewSetup(JNIEnv* env, jobject obj, jobject setup) {
    // Implement conversion from jobject to VPinballViewSetup and call VPinballSetViewSetup
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSetDefaultViewSetup(JNIEnv* env, jobject obj) {
    VPinballSetDefaultViewSetup();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballResetViewSetup(JNIEnv* env, jobject obj) {
    VPinballResetViewSetup();
}

JNIEXPORT void JNICALL Java_VPinballJNI_VPinballSaveViewSetup(JNIEnv* env, jobject obj) {
    VPinballSaveViewSetup();
}

}