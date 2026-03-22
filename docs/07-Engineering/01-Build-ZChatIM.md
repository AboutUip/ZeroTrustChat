# ZChatIM 构建说明

本文档描述 **`ZChatIM/`** 目录下 C++ 组件的构建依赖与平台差异。实现以 **`ZChatIM/CMakeLists.txt`** 为准。**工程不集成 vcpkg**（仓库**无** **`vcpkg.json`**）。**`ZChatIMCore` 显式源文件列表**、**元数据 SQLite**（默认 **树内 SQLCipher amalgamation**，可选 **`sqlite3` 明文 amalgamation**）、**链接关系**与 **`ZCHATIM_BUILD_*` / `ZCHATIM_USE_SQLCIPHER`** 以该文件为准；**活文档对照**见 **`docs/02-Core/05-ZChatIM-Implementation-Status.md` 第1节**。

## 1. 工具链

| 项 | 要求 |
|----|------|
| CMake | ≥ 3.20 |
| 语言标准 | **C++17**；**C99** 仅在 **`ZCHATIM_USE_SQLCIPHER=OFF`** 时用于编译 **`thirdparty/sqlite/sqlite3.c`**（默认 ON 时元数据为 **SQLCipher**，不编该 amalgamation） |
| Windows 建议 | Visual Studio 2022，CMake 生成器 **Visual Studio 17 2022**（x64） |

## 2. 密码学与系统依赖

| 平台 | 密码学实现 | 链接库 |
|------|------------|--------|
| **全平台** | **OpenSSL 3.x**（**MM2** AES-GCM、PBKDF2、**RAND_bytes**；**MM1** Ed25519 **EVP**；**`common::Random` / `AuthSessionManager`** 安全随机） | **`OpenSSL::Crypto`**（**`find_package(OpenSSL 3.0)`**）；**SQLCipher** 另 **`OpenSSL::SSL`** |
| **Windows 额外** | **`mm2_message_key.bin`** 可选 **DPAPI** 封装 | **`crypt32`**（与 OpenSSL **并存**） |

- **Windows 无系统 OpenSSL**：将 **install 树**放到 **`thirdparty/openssl/prebuilt/windows-x64/openssl/`** 等（**`LAYOUT.md`**），或 **`OPENSSL_ROOT_DIR`**。
- **`-DZCHATIM_USE_SQLCIPHER=OFF`**：仍**需要** **OpenSSL**（MM2 / MM1 / 随机数）；仅元数据 SQLite 改明文 **`thirdparty/sqlite`**。

### 2.1 Linux（示例：Debian / Ubuntu）

```bash
sudo apt install libssl-dev
cd ZChatIM && cmake -B build && cmake --build build --config Release
```

### 2.2 macOS（Homebrew）

```bash
brew install openssl@3
export OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cd ZChatIM && cmake -B build && cmake --build build --config Release
```

### 2.3 预留 OpenSSL 目录

**推荐**：**`ZChatIM/thirdparty/openssl/prebuilt/windows-x64/openssl/`**（完整 **install** 树：**`include/openssl/ssl.h`** + **`lib/libcrypto.lib`** 等）。**可选**：先放到 **`thirdparty/openssl/builds/windows-x64/install/`**，再在 CMake 中设 **`OPENSSL_ROOT_DIR`** 指向该目录，或复制到 **`prebuilt/...`**。**兼容**：**`thirdparty/openssl/versions/current`** 等。细则见 **`ZChatIM/thirdparty/openssl/LAYOUT.md`** 与 **`README.md`**。

### 2.4 元数据 SQLCipher（默认 `ZCHATIM_USE_SQLCIPHER=ON`，**无 vcpkg**）

| 平台 | 依赖 | 说明 |
|------|------|------|
| **全平台（唯一方式）** | **`sqlite3.c`** + **`sqlite3.h`** | **默认**：**`ZChatIM/thirdparty/sqlcipher/`** 下 amalgamation；**Windows 可选**：**`prebuilt/windows-x64/amalgamation/`**（若存在则优先）。**OpenSSL 3** 与 MM2 / MM1 **共用**（**不需要** **`libsqlcipher-dev`**）。 |

- **关闭 SQLCipher**（仅本地调试明文索引）：**`-DZCHATIM_USE_SQLCIPHER=OFF`** → 回退 **`thirdparty/sqlite/sqlite3.c`**，**勿**用于需「离线 `.db` 不可读」的产品形态。
- **密钥与 PRAGMA** 以 **`docs/02-Core/03-Storage.md` 第4.2节** 为准（域分离派生、固定 **`cipher_page_size` / `kdf_iter` / HMAC-KDF 算法**）。

## 3. Windows 本地配置步骤

1. 用 Visual Studio 2022 打开 **`ZChatIM/`**（含 **`CMakeLists.txt`**）。
2. CMake 预设选用 **`windows-default`**（见 **`ZChatIM/CMakePresets.json`**）。
3. 生成解决方案并编译。

若仍出现 **OpenSSL** 相关报错：**Linux/macOS** 检查 **`OPENSSL_ROOT_DIR`** 与 **`libssl-dev` / `openssl@3`**；**Windows** 检查 **`prebuilt/windows-x64/openssl/`** 或 **`OPENSSL_ROOT_DIR`**（须 **OpenSSL 3**、**`include/openssl/ssl.h`** + **`libcrypto`**）。另：确认 CMake **源码目录**为 **`ZChatIM/`**，勿混入其它平台的 **`OPENSSL_*`** 缓存；必要时**删构建目录重新配置**。

## 4. 多配置生成器与 Release

使用 **Visual Studio** 等多配置生成器时：

- 在 IDE 中将 **解决方案配置** 设为 **Release**（或 **RelWithDebInfo**）后再生成；**默认 Debug 与 Release 输出可能对应不同期望**。
- 命令行需显式指定：  
  `cmake --build <构建目录> --config Release`

单配置生成器（如 Ninja）通常通过 **`-DCMAKE_BUILD_TYPE=Release`** 指定。

## 5. 配置校验

| 平台 | CMake 输出应反映 |
|------|------------------|
| 全平台 | CMake 输出 **`MM2 crypto backend: OpenSSL3`**、**`OpenSSL: …`**（3.x）；**`Metadata SQLite: SQLCipher`**（默认，树内 amalgamation，**无 vcpkg**） |

## 6. JNI 目标

默认 **`ZCHATIM_BUILD_JNI=ON`**。若本机无 JDK / **`JAVA_HOME`**，可 **` -DZCHATIM_BUILD_JNI=OFF`** 关闭 JNI 共享库目标。

| 产物 | 说明 |
|------|------|
| **`ZChatIMJNI`**（`SHARED`） | 源：**`jni/ZChatIMJNI.cpp`**（**`JNI_OnLoad` → `zchatim_RegisterNatives`**）、**`jni/JniNatives.cpp`**（**`RegisterNatives`** 绑定 **`com.yhj.zchat.jni.ZChatIMNative`**）。**链接** **`ZChatIMCore`**（内含 **`JniBridge`/`JniInterface`**）。输出名 **`ZChatIMJNI`**（**`System.loadLibrary("ZChatIMJNI")`**）。 |
| **Java** | 参考实现：**`ZChatServer/src/main/java/com/yhj/zchat/jni/ZChatIMNative.java`**（与 **`01-JNI.md`** / **`JniInterface.h`** 同序）。 |

## 7. 控制台 EXE 与树内测试

**`ZCHATIM_BUILD_EXE=OFF`** 时不生成 **`ZChatIM`** 可执行目标。

默认 **`ZCHATIM_BUILD_EXE=ON`**、**`ZCHATIM_BUILD_TESTS=ON`**：`ZChatIM` 会编译 **`tests/common_tools_test.cpp`**、**`tests/mm1_managers_test.cpp`**、**`tests/mm2_fifty_scenarios_test.cpp`**、**`tests/jni_im_smoke_test.cpp`**。控制台 **`--test`** **一次跑完全部**（**`RunMM1ManagerTests`** 内顺序：**`RunCommonToolsTests`** → 各 MM1/MM2 **`[CASE]`** → **minimal MM2**（**`RunMinimalScenarioTestCasesMerged`**）→ **MM2 fifty** → **JNI IM smoke**；见 **`05-ZChatIM-Implementation-Status.md`**）。**不再提供** **`--test-common` / `--test-minimal` / `--test-mm250` / `--test-jni-im`**（误打 **`--test-…`** 会提示使用 **`--test`**）。

若只要最小控制台 EXE（**仅 `main.cpp`**、不依赖 **`tests/*.cpp`**）：

```bash
cmake -B build -DZCHATIM_BUILD_TESTS=OFF
cmake --build build --config Release
```

此时运行 **`--test`** 会打印未编入测试的提示并以**非零退出码**结束；**无法识别的 `--test…` 参数**（如旧版 **`--test-mm250`**）为退出码 **2** 并提示改用 **`--test`**。

## 8. 运行期说明（Windows）

**MM2 消息加密**经 **OpenSSL**；若 **`ZChatIM.exe`** 动态依赖 **`libcrypto*.dll`** / **`libssl*.dll`**，须在发行说明中写明 **DLL 搜索路径**。**`-DZCHATIM_USE_SQLCIPHER=OFF`** 时回退内置 **sqlite3**，**仍**依赖 **OpenSSL**（MM2 / MM1 / 随机数）。
