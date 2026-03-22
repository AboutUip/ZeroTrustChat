// ZChatIM JNI 动态库入口：`RegisterNatives` 绑定 `com.yhj.zchat.jni.ZChatIMNative` → `JniInterface` / `JniBridge`。
// 构建：ZChatIM/CMakeLists.txt（ZCHATIM_BUILD_MODE=JniDllOnly|Both、JAVA_HOME / JNI 头路径）。

#include <jni.h>

extern "C" jint zchatim_RegisterNatives(JNIEnv* env);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/)
{
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) != JNI_OK) {
        return JNI_ERR;
    }
    if (zchatim_RegisterNatives(env) != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_8;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/)
{
}
