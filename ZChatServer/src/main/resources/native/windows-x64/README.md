# Windows x64 原生库（classpath：`/native/windows-x64/`）

由 `NativeLibraryLoader.WINDOWS_DLL_ORDER` 定义加载顺序（OpenSSL、`ZChatIMJNI.dll` 等）。将对应 `.dll` 放入本目录后，Windows 下可从 classpath 自动加载。

详见 `com.ztrust.zchat.im.jni.NativeLibraryLoader` 与 **`docs/03-Business/01-SpringBoot.md`**（JNI 联调）。
