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

## 3. Windows 本地

Visual Studio 打开 **`ZChatIM/`**，在 **CMake 预设** 中选其一（**`CMakePresets.json`**）：

| 预设 | 产物 |
|------|------|
| **`windows-exe-only`** | **`ZChatIM.exe`**（无 **`ZChatIMJNI.dll`**） |
| **`windows-jni-dll-only`** | **`ZChatIMJNI.dll`**（无 **`ZChatIM.exe`**）；需 **`JAVA_HOME`** |
| **`windows-both`** | 二者皆有 |

或在配置时设 **`ZCHATIM_BUILD_MODE`** 为 **`ExeOnly`** / **`JniDllOnly`** / **`Both`**。CMake 源码目录须为 **`ZChatIM/`**。

## 4. Release

多配置生成器：**`cmake --build <dir> --config Release`**。Ninja：**`-DCMAKE_BUILD_TYPE=Release`**。

## 5. 校验

CMake 应出现 **`MM2 crypto backend: OpenSSL3`**、**`Metadata SQLite: SQLCipher`**（默认）。

## 6. JNI

**`ZCHATIM_BUILD_JNI=ON`**（默认）。无 JDK 时 **`-DZCHATIM_BUILD_JNI=OFF`**。

| 产物 | 说明 |
|------|------|
| **`ZChatIMJNI`** | **`jni/ZChatIMJNI.cpp`**（**`JNI_OnLoad`**）、**`jni/JniNatives.cpp`** → **`com.yhj.zchat.jni.ZChatIMNative`** |
| Java | **`ZChatServer/.../ZChatIMNative.java`** |

## 7. 控制台 EXE 与树内测试

**`ZCHATIM_BUILD_MODE`** 为 **`JniDllOnly`** 时不生成 **`ZChatIM.exe`**；**`ExeOnly`** 不生成 JNI DLL；**`Both`** 二者都生成。

默认 **`ZCHATIM_BUILD_TESTS=ON`**：编入 **`tests/*.cpp`**。**`--test`** 跑全量（common、MM1/MM2、`RunMinimalScenarioTestCasesMerged`、MM2-50、JNI IM smoke、JNI local+RTC）。**`--test-im-1k`**：1000 条 JNI IM 压测（**`test_im_1k_test.cpp`**，需 OpenSSL Ed25519）；失败 stderr 输出 **`IM-1K FAILURE REPORT`**。

**`-DZCHATIM_BUILD_TESTS=OFF`**：仅 **`main.cpp`**；**`--test` / `--test-im-1k`** 会失败并提示重建。

## 8. 运行期（Windows）

若 exe/jni 动态依赖 **`libcrypto-3-x64.dll`** 等，发行时需一并部署或写明 **`PATH`**。
