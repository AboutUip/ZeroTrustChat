// ZChatIM JNI 动态库入口桩 — 随实现扩展 native 方法导出。
// 构建：见 ZChatIM/CMakeLists.txt（ZCHATIM_BUILD_JNI）。

#include <jni.h>

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* /*vm*/, void* /*reserved*/)
{
    // 后续：注册 native 方法、校验 JniBridge/JniInterface 初始化等
    return JNI_VERSION_1_8;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/)
{
}
