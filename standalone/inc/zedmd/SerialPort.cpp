#include "stdafx.h"
#include "SerialPort.h"

#ifdef __ANDROID__
static jobject gSingletonInstance;

static jmethodID gOpenMethod;
static jmethodID gIsOpenMethod;
static jmethodID gCloseMethod;
static jmethodID gAvailableMethod;
static jmethodID gClearDTRMethod;
static jmethodID gSetDTRMethod;
static jmethodID gClearRTSMethod;
static jmethodID gSetRTSMethod;
static jmethodID gWriteBytesMethod;
static jmethodID gReadBytesMethod;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* pjvm, void* reserved)
{
   JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();

   jclass serialPortClass = env->FindClass("org/vpinball/app/SerialPort");
   jmethodID getInstanceMethod = env->GetStaticMethodID(serialPortClass, "getInstance", "()Lorg/vpinball/app/SerialPort;");
   gSingletonInstance = env->NewGlobalRef(env->CallStaticObjectMethod(serialPortClass, getInstanceMethod));

   gOpenMethod = env->GetMethodID(serialPortClass, "open", "(IIII)Z");
   gIsOpenMethod = env->GetMethodID(serialPortClass, "isOpen", "()Z");
   gCloseMethod = env->GetMethodID(serialPortClass, "close", "()V");
   gAvailableMethod = env->GetMethodID(serialPortClass, "available", "()I");
   gClearDTRMethod = env->GetMethodID(serialPortClass, "clearDTR", "()V");
   gSetDTRMethod = env->GetMethodID(serialPortClass, "setDTR", "()V");
   gClearRTSMethod = env->GetMethodID(serialPortClass, "clearRTS", "()V");
   gSetRTSMethod = env->GetMethodID(serialPortClass, "setRTS", "()V");
   gWriteBytesMethod = env->GetMethodID(serialPortClass, "writeBytes", "([BI)I");
   gReadBytesMethod = env->GetMethodID(serialPortClass, "readBytes", "([BI)I");

   return JNI_VERSION_1_6;
}

#endif

void SerialPort::SetReadTimeout(int timeout)
{
    m_readTimeout = timeout;
}

void SerialPort::SetWriteTimeout(int timeout)
{
    m_writeTimeout = timeout;
}

bool SerialPort::Open(string* pDevice, int baudRate, int dataBits, int stopBits, int parity)
{
#ifndef __ANDROID__
   SerialDataBits serialDataBits;
   SerialStopBits serialStopBits;
   SerialParity serialParity;
   switch(dataBits) {
      case 5: serialDataBits = SERIAL_DATABITS_5; break;
      case 6: serialDataBits = SERIAL_DATABITS_6; break;
      case 7: serialDataBits = SERIAL_DATABITS_7; break;
      case 16: serialDataBits = SERIAL_DATABITS_16; break;
      default: serialDataBits = SERIAL_DATABITS_8; break;
   }
   switch(stopBits) {
      case 2: serialStopBits = SERIAL_STOPBITS_2; break;
      case 3: serialStopBits = SERIAL_STOPBITS_1_5; break;
      default: serialStopBits = SERIAL_STOPBITS_1; break;
   }
   switch(parity) {
      case 1: serialParity = SERIAL_PARITY_EVEN; break;
      case 2: serialParity = SERIAL_PARITY_ODD; break;
      case 3: serialParity = SERIAL_PARITY_MARK; break;
      case 4: serialParity = SERIAL_PARITY_SPACE; break;
      default: serialParity = SERIAL_PARITY_NONE; break;
   }
   return (m_seriallib.openDevice(pDevice->c_str(), baudRate, serialDataBits, serialParity, serialStopBits) == 1);
#else
   return ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallBooleanMethod(gSingletonInstance, gOpenMethod, baudRate, dataBits, stopBits, parity);
#endif
}

bool SerialPort::IsOpen()
{
#ifndef __ANDROID__
   return m_seriallib.isDeviceOpen();
#else
   return ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallBooleanMethod(gSingletonInstance, gIsOpenMethod);
#endif    
}

void SerialPort::Close()
{
#ifndef __ANDROID__
   m_seriallib.closeDevice();
#else
   ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallVoidMethod(gSingletonInstance, gCloseMethod);
#endif
}

int SerialPort::Available()
{
#ifndef __ANDROID__
   return m_seriallib.available();
#else
   return ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallIntMethod(gSingletonInstance, gAvailableMethod);
#endif
}

void SerialPort::ClearDTR()
{
#ifndef __ANDROID__
   m_seriallib.clearDTR();
#else
   ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallVoidMethod(gSingletonInstance, gClearDTRMethod);
#endif
}

void SerialPort::SetDTR()
{
#ifndef __ANDROID__
    m_seriallib.setDTR();
#else
    ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallVoidMethod(gSingletonInstance, gSetDTRMethod);
#endif
}

void SerialPort::ClearRTS()
{
#ifndef __ANDROID__
   m_seriallib.clearRTS();
#else
   ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallVoidMethod(gSingletonInstance, gClearRTSMethod);
#endif
}

void SerialPort::SetRTS()
{
#ifndef __ANDROID__
   m_seriallib.setRTS();
#else
   ((JNIEnv*)SDL_AndroidGetJNIEnv())->CallVoidMethod(gSingletonInstance, gSetRTSMethod);
#endif
}

int SerialPort::WriteBytes(UINT8* pBytes, int size)
{
#ifndef __ANDROID__
   return m_seriallib.writeBytes(pBytes, size);
#else
   JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
   jbyteArray jByteArray = env->NewByteArray(size);
   env->SetByteArrayRegion(jByteArray, 0, size, reinterpret_cast<const jbyte*>(pBytes));
   jint result = env->CallIntMethod(gSingletonInstance, gWriteBytesMethod, jByteArray, m_writeTimeout);
   env->DeleteLocalRef(jByteArray);
   return result;
#endif
}

int SerialPort::WriteChar(UINT8 byte)
{
#ifndef __ANDROID__
   return m_seriallib.writeChar(byte);
#else
   return WriteBytes(&byte, 1);
#endif
}

int SerialPort::ReadBytes(UINT8* pBytes, int size)
{
#ifndef __ANDROID__
   return m_seriallib.readBytes(pBytes, size, m_readTimeout);
#else
   JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
   jbyteArray jByteArray = env->NewByteArray(size);
   jint result = env->CallIntMethod(gSingletonInstance, gReadBytesMethod, jByteArray, m_readTimeout);
   jbyte* byteArrayData = env->GetByteArrayElements(jByteArray, NULL);
   memcpy(pBytes, byteArrayData, size);
   env->ReleaseByteArrayElements(jByteArray, byteArrayData, JNI_ABORT);
   env->DeleteLocalRef(jByteArray);
   return result;
#endif
}

int SerialPort::ReadChar(UINT8 *pByte)
{
#ifndef __ANDROID__
   return m_seriallib.readChar((char*)pByte, m_readTimeout);
#else
   return ReadBytes(pByte, 1);
#endif
}

UINT8 SerialPort::ReadByte()
{
   UINT8 byte = 0;
#ifndef __ANDROID__
   m_seriallib.readChar((char*)&byte, m_readTimeout);
#else
   ReadBytes(&byte, 1);
#endif
   return byte;
}