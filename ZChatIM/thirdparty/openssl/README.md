# OpenSSL（全平台：MM2 / MM1 / 随机数 / SQLCipher）

**ZChatIM** 在 **Windows、Linux、macOS** 上均 **`find_package(OpenSSL 3.0 REQUIRED)`** 并链接 **`OpenSSL::Crypto`**（**SQLCipher** 另链 **`OpenSSL::SSL`**）。

- **MM2 `Crypto.cpp`**：AES-256-GCM、PBKDF2、**`RAND_bytes`**
- **`common/Ed25519.cpp`**：Ed25519 验签
- **`common/Random.cpp`**、**`AuthSessionManager`**：安全随机（**`RAND_bytes`**；Unix 可再读 **`/dev/urandom`**）
- **SQLCipher**：编解码（默认 **`ZCHATIM_USE_SQLCIPHER=ON`**）

**Windows 无系统开发包**：把 **`nmake install`**（或等价）得到的**安装根**放到约定目录，或设 **`OPENSSL_ROOT_DIR`**。

## 目录约定（细粒度）

详见 **`LAYOUT.md`**。摘要：

| 用途 | 路径 |
|------|------|
| **推荐（Win64 成品）** | **`prebuilt/windows-x64/openssl/`**（`include/openssl/ssl.h`、`lib/libcrypto.lib` …） |
| **可选（构建归档）** | **`builds/windows-x64/install/`** → 可复制到上一行 |
| **兼容** | **`versions/win64/current/`** 或 **`versions/current/`** |
| **任意** | **`OPENSSL_ROOT_DIR`** 环境变量 / CMake 缓存 |

**Linux / macOS**：系统 **`libssl-dev`** / **Homebrew**，或 **`versions/current/`**。

**`versions/*`、`prebuilt/*`、`builds/*` 下大文件默认不入 Git**（见仓库根 **`.gitignore`**）。

**MM2 密钥文件**：**Windows** **`ZMK1` + DPAPI**（**`crypt32`**，**`MM2.cpp`**）；**Apple** **`ZMK3` + Keychain**（**`MM2_message_key_darwin.cpp`**）；**其它 Unix** **`ZMK2` + 派生封装 + AES-GCM**（**`MM2.cpp`**）。均与 OpenSSL **并存**。
