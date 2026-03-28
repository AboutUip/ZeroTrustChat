# ZChatIM 构建说明

以 `ZChatIM/CMakeLists.txt` 为准。仓库不使用 vcpkg。产物与源列表对照见 [`Implementation-Status.md`](Implementation-Status.md) 第1节。

## 1. 工具链

| 项 | 要求 |
|----|------|
| CMake | ≥ 3.20 |
| 语言标准 | **C++17**；**C99** 仅在 **`ZCHATIM_USE_SQLCIPHER=OFF`** 时编 **`thirdparty/sqlite/sqlite3.c`** |
| Windows 建议 | Visual Studio 2022，**Visual Studio 17 2022**（x64） |

## 2. 密码学与系统依赖

| 平台 | 说明 |
|------|------|
| **全平台** | **OpenSSL 3.x**（MM2 GCM/PBKDF2/RAND、MM1 Ed25519 EVP）→ **`OpenSSL::Crypto`**；SQLCipher 另 **`OpenSSL::SSL`** |
| **Windows** | **`mm2_message_key.bin`** 可选 **DPAPI** → **`crypt32`** |

**Windows OpenSSL**：**`thirdparty/openssl/prebuilt/windows-x64/openssl/`** 或 **`OPENSSL_ROOT_DIR`**（须 **`include/openssl/ssl.h`** + **libcrypto**）。细则 **`thirdparty/openssl/LAYOUT.md`**。

**`-DZCHATIM_USE_SQLCIPHER=OFF`**：仍要 OpenSSL；元数据改明文 **`thirdparty/sqlite`**，**勿**用于需离线 `.db` 不可读的产品。

### 2.1 Linux

```bash
sudo apt install libssl-dev
cd ZChatIM && cmake -B build && cmake --build build --config Release
```

### 2.2 macOS

```bash
brew install openssl@3
export OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cd ZChatIM && cmake -B build && cmake --build build --config Release
```

### 2.3 SQLCipher（默认 `ON`）

树内 **`thirdparty/sqlcipher/`** amalgamation；Windows 可选 **`prebuilt/windows-x64/amalgamation/`**。密钥与 PRAGMA：**[`docs/02-Core/03-Storage.md`](../../docs/02-Core/03-Storage.md) 第4.2节**。

## 3. CMake 预设（Windows / Linux）

源码目录须为 **`ZChatIM/`**。**`CMakePresets.json`** 中：

### 3.1 Windows（Visual Studio 2022 x64）

| 预设 | 产物 |
|------|------|
| **`windows-exe-only`** | **`ZChatIM.exe`**（无 **`ZChatIMJNI.dll`**） |
| **`windows-jni-dll-only`** | **`ZChatIMJNI.dll`**；需 **`JAVA_HOME`** |
| **`windows-both`** | 二者皆有 |

构建示例：`cmake --build --preset windows-jni-dll-only-release`（Release 配置）。

**Visual Studio 识别预设**

1. **工具 → 选项 → CMake → 常规**：勾选 **「启用 CMake 预设」**（表述因 VS 版本略有不同）。
2. **打开文件夹** 任选其一：
   - **仓库根目录 `ZerOS-Chat/`**：读取根目录 **`CMakePresets.json`**（**`version` 6**，**`include`** → `ZChatIM/CMakePresets.json`），下拉中应出现 **`windows-exe-only`**、**`windows-jni-dll-only`**、**`windows-both`** 等；需本机 **CMake ≥ 3.24**（与 VS 2022 17.8+ 自带 CMake 一般可满足）。
   - **仅 `ZChatIM/`**：直接使用 **`ZChatIM/CMakePresets.json`**（**`version` 3**），**CMake ≥ 3.20** 即可。
3. 根目录另有 **`CMakeLists.txt`** **`add_subdirectory(ZChatIM)`**，便于在未选预设时仍能解析工程树。

Windows 预设的 **`windows-base`** 含 **`vendor.microsoft.com/VisualStudioSettings/CMake/1.0`**（如 **`intelliSenseMode`: `windows-msvc-x64`**），便于 MSVC IntelliSense 与 CMake 集成一致。

**在 Windows 上生成 `.so`（ELF）** MSVC 只能产出 `.dll`，不能在同一工具链下直接编出 Linux 共享库。请在 Visual Studio 里用 **WSL 目标 + Linux 配置预设 + 对应生成预设**（见下）。**`wsl-linux-*`** 预设的 **`vendor.hostOS`: `["Linux"]`** 表示 CMake 在 **WSL** 内配置/编译，产物在 **`build-linux-wsl/lib/libZChatIMJNI.so`**。需安装 **「使用 C++ 的 Linux 开发」**（或带 CMake 的 WSL 集成），并在 WSL 内安装 **`build-essential`**、**`libssl-dev`**、**`openjdk-*-jdk`**（设 **`JAVA_HOME`**）等。纯 Linux 实体机仍可用 **`linux-*`** 预设（仅当 **`hostSystemName`** 为 Linux 时在列表中出现）。

**Visual Studio：右侧只有「Build EXE + DLL」等 Windows 项时**

CMake 预设模式下，工具栏是 **三个** 下拉框（见 [Configure and build with CMake Presets](https://learn.microsoft.com/cpp/build/cmake-presets-vs)）：**目标系统**（最左）、**配置预设**（中间）、**生成预设**（最右）。中间、右侧的列表都依赖 **当前选中的目标系统** 与 **配置预设**：

1. **最左「目标系统」**：先选你的 **WSL 发行版**（例如 Ubuntu），不要停留在 **本机 / Local machine**。若列表里没有 WSL，需在「管理连接」中确认已安装 WSL，并安装 **Linux 与嵌入式开发** 相关工作负载。
2. **中间「配置预设」**：在已选 WSL 的前提下，才会出现带 **`(WSL)`** 的项；选 **`ZChatIM — Linux JNI .so (WSL)`**（或 **`ZChatIM — Linux EXE + .so (WSL)`**）。仅改最右「生成预设」而不改中间，**不会出现** **`Build libZChatIMJNI.so (WSL Release)`**，因为生成预设必须关联当前配置预设（文档：**Build Presets** 会按 **`configurePreset`** 过滤）。
3. **最右「生成预设」**：再选 **`Build libZChatIMJNI.so (WSL Release)`**（或对应的 WSL **EXE + .so** 项），然后 **生成 → 生成全部**。

本仓库 **`windows-base`** 已标注 **`hostOS`: `["Windows"]`**，在 WSL 目标下中间列表会优先显示 Linux/WSL 配置，减少与 Windows 预设混在一起的情况。

### 3.2 Linux（`Unix Makefiles`，仅当在 Linux 主机上配置时可见）

**不必须 WSL**：要得到 Linux 的 **`.so`**，只需要 **在 Linux 上跑 g++/CMake**（实体机、**VMware/VirtualBox 里的 Ubuntu**、云主机、WSL 都可以）。你在 **VMware Ubuntu** 里编译时，与 WSL 无关，直接用下面的 **`linux-*`** 预设或脚本即可；产物在 **`ZChatIM/build-linux/lib/`**。

**VMware Ubuntu 建议流程**

1. 在虚拟机里 **git clone** 仓库，或把工程放在虚拟机 **Linux 本地磁盘**（例如 `~/projects/ZerOS-Chat`）。从 Windows 共享文件夹编译有时会遇到权限或路径问题，优先用虚拟机内的 ext4 目录。
2. 安装依赖：`sudo apt update && sudo apt install -y build-essential cmake libssl-dev openjdk-17-jdk`（JDK 版本可按需改）。
3. 设置 **`JAVA_HOME`**（示例）：`export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64`，或用 `JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(which javac)")")")"`。
4. 在虚拟机终端：`cd ZChatIM`，执行 **`./build-jni-linux.sh`**，或：
   - `cmake --preset linux-jni-so-only && cmake --build --preset linux-jni-so-only-release`
5. 取 **`ZChatIM/build-linux/lib/libZChatIMJNI.so`**（及运行 Java 时可能需要的 OpenSSL 等 `.so`，视部署方式而定）。Java 侧可用 **`-Dzchat.native.dir=.../build-linux/lib`**（见 **`NativeLibraryLoader`**）。

若希望 **仍在 Windows 的 Visual Studio 里** 远程编译到这台 Ubuntu，可在 **连接管理器** 里添加该虚拟机的 **SSH**，目标系统选该主机，再选用 **面向 Linux 的配置预设**（需已安装「使用 C++ 的 Linux 开发」）；否则在虚拟机里用 **终端 + CMake** 即可，无需 VS。

| 预设 | 产物 |
|------|------|
| **`linux-exe-only`** | **`ZChatIM`** 可执行文件，无 **`libZChatIMJNI.so`** |
| **`linux-jni-so-only`** | **`build-linux/lib/libZChatIMJNI.so`**（及 **`libZChatIMCore.a`**）；需 **`JAVA_HOME`**、`libssl-dev`、树内 SQLCipher |
| **`linux-both`** | 可执行文件 + **`.so`** |

```bash
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64   # 按本机 JDK 调整
cd ZChatIM
./build-jni-linux.sh
# 或手动：cmake --preset linux-jni-so-only && cmake --build --preset linux-jni-so-only-release
# 产物：ZChatIM/build-linux/lib/libZChatIMJNI.so
```

运行 Java 时可将 **`build-linux/lib`** 加入依赖搜索路径，或使用 **`-Dzchat.native.dir=.../build-linux/lib`**（见 **`ZChatServer`** 的 **`NativeLibraryLoader`**）：该目录下需能按顺序找到 OpenSSL 与 **`libZChatIMJNI.so`**（名称见 loader 内常量）。

或在配置时直接设 **`ZCHATIM_BUILD_MODE`** 为 **`ExeOnly`** / **`JniDllOnly`** / **`Both`**，不必使用预设。

## 4. Release

多配置生成器：**`cmake --build <dir> --config Release`**。Ninja：**`-DCMAKE_BUILD_TYPE=Release`**。

## 5. 校验

CMake 应出现 **`MM2 crypto backend: OpenSSL3`**、**`Metadata SQLite: SQLCipher`**（默认）。

## 6. JNI

**`ZCHATIM_BUILD_JNI=ON`**（默认）。无 JDK 时 **`-DZCHATIM_BUILD_JNI=OFF`**。

| 产物 | 说明 |
|------|------|
| **`ZChatIMJNI`** | **`jni/ZChatIMJNI.cpp`**（**`JNI_OnLoad`**）、**`jni/JniNatives.cpp`** → **`com.ztrust.zchat.im.jni.ZChatIMNative`** |
| Java | **`ZChatServer/.../ZChatIMNative.java`** |

**平台与扩展名**：Windows **`ZChatIMJNI.dll`**；Linux **`libZChatIMJNI.so`**；macOS **`libZChatIMJNI.dylib`**。非 MSVC 生成器下库输出目录为 **`${CMAKE_BINARY_DIR}/lib`**。

**`JAVA_HOME` 兜底**：若 **`FindJNI`** 失败，CMake 会读 **`JAVA_HOME/include`** 及 **`linux`** / **`darwin`** / **`win32`** 子目录，勿仅配置 **`include/win32`**（旧行为在 Linux 上会找不到 **`jni_md.h`**）。

**Java 侧加载**：**`com.ztrust.zchat.im.jni.NativeLibraryLoader`** 支持 **`-Dzchat.native.dir=<目录>`** 显式 **`System.load` 顺序**、类路径 **`/native/linux-x64/`** 等打包资源，以及回退 **`System.loadLibrary("ZChatIMJNI")`**。

## 7. 控制台 EXE 与树内测试

**`ZCHATIM_BUILD_MODE`** 为 **`JniDllOnly`** 时不生成 **`ZChatIM.exe`**；**`ExeOnly`** 不生成 JNI DLL；**`Both`** 二者都生成。

默认 **`ZCHATIM_BUILD_TESTS=ON`**：编入 **`tests/*.cpp`**。**`--test`** 跑全量（common、MM1/MM2、`RunMinimalScenarioTestCasesMerged`、MM2-50、JNI IM smoke、JNI local+RTC）。**`--test-im-1k`**：1000 条 JNI IM 压测（**`test_im_1k_test.cpp`**，需 OpenSSL Ed25519）；失败 stderr 输出 **`IM-1K FAILURE REPORT`**。

**`-DZCHATIM_BUILD_TESTS=OFF`**：仅 **`main.cpp`**；**`--test` / `--test-im-1k`** 会失败并提示重建。

## 8. 运行期（Windows）

若 exe/jni 动态依赖 **`libcrypto-3-x64.dll`** 等，发行时需一并部署或写明 **`PATH`**。
