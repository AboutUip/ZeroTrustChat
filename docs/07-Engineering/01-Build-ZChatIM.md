# ZChatIM 构建说明

本文档描述 **`ZChatIM/`** 目录下 C++ 组件的构建依赖与平台差异。实现以 **`ZChatIM/CMakeLists.txt`** 为准。

## 1. 工具链

| 项 | 要求 |
|----|------|
| CMake | ≥ 3.20 |
| 语言标准 | C++17、C99（SQLite amalgamation） |
| Windows 建议 | Visual Studio 2022，CMake 生成器 **Visual Studio 17 2022**（x64） |

## 2. 密码学与系统依赖

| 平台 | 密码学实现 | 链接库（MM2 相关） |
|------|------------|-------------------|
| **Windows** | **BCrypt**（AES-GCM、PBKDF2、RNG；CryptoAPI 作为 RNG 后备） | `bcrypt`、`advapi32`、`crypt32`（DPAPI：`mm2_message_key.bin`） |
| **Linux / macOS** | **OpenSSL 3.x**（`libcrypto`） | `OpenSSL::Crypto`（由 `find_package(OpenSSL 3.0)` 解析） |

- **Windows**：不要求安装 OpenSSL，不要求设置 **`OPENSSL_ROOT_DIR`**。
- **Linux / macOS**：若未找到 OpenSSL 3，CMake 配置阶段 **`FATAL_ERROR`**。

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

可将官方预编译树置于 **`ZChatIM/thirdparty/openssl/versions/current`**（须包含 **`include/openssl/ssl.h`** 与对应 **`libcrypto`**）。说明见 **`ZChatIM/thirdparty/openssl/README.md`**。

## 3. Windows 本地配置步骤

1. 用 Visual Studio 2022 打开 **`ZChatIM/`**（含 **`CMakeLists.txt`**）。
2. CMake 预设选用 **`windows-default`**（见 **`ZChatIM/CMakePresets.json`**）。
3. 生成解决方案并编译。

若仍出现 **OpenSSL** 相关报错：确认 CMake **源码目录**为 **`ZChatIM/`**，且未将其它平台缓存中的 **`OPENSSL_*`** 变量带入；**删除 CMake 缓存后重新配置**。

## 4. 多配置生成器与 Release

使用 **Visual Studio** 等多配置生成器时：

- 在 IDE 中将 **解决方案配置** 设为 **Release**（或 **RelWithDebInfo**）后再生成；**默认 Debug 与 Release 输出可能对应不同期望**。
- 命令行需显式指定：  
  `cmake --build <构建目录> --config Release`

单配置生成器（如 Ninja）通常通过 **`-DCMAKE_BUILD_TYPE=Release`** 指定。

## 5. 配置校验

| 平台 | CMake 输出应反映 |
|------|------------------|
| Windows | MM2 密码学后端为 **BCrypt** |
| Linux / macOS | MM2 密码学后端为 **OpenSSL3**，且 OpenSSL 版本为 3.x |

## 6. JNI 目标

默认 **`ZCHATIM_BUILD_JNI=ON`**。若本机无 JDK / **`JAVA_HOME`**，可 **` -DZCHATIM_BUILD_JNI=OFF`** 关闭 JNI 共享库目标。

## 6.1 控制台 EXE 与树内测试

默认 **`ZCHATIM_BUILD_EXE=ON`**、**`ZCHATIM_BUILD_TESTS=OFF`**：`ZChatIM` 可执行文件**仅**编译 **`main.cpp`**，**不**依赖 **`tests/*.cpp`**，适合发布与最小构建。此时运行 **`--test` / `--test-minimal` / `--test-mm250`** 会打印提示并以退出码 **2** 结束。

若要在本机跑树内测试，须打开开关并保证仓库中存在对应源文件：

```bash
cmake -B build -DZCHATIM_BUILD_TESTS=ON
cmake --build build --config Release
```

会额外编译 **`tests/mm1_managers_test.cpp`**、**`tests/mm2_fifty_scenarios_test.cpp`**，上述 **`--test*`** 参数即生效。

## 7. 运行期说明（Windows）

当前 **ZChatIMCore** 在 Windows **不**链接 OpenSSL 动态库；无需部署 **`libcrypto*.dll`**。若后续变更链接关系，需在发行说明中单独列出 DLL 搜索路径要求。
