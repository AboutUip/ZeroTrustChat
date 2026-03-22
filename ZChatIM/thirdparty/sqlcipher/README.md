# SQLCipher（元数据索引，内网 / 完全离线）

`ZChatIM` **不集成 vcpkg**；在 **`ZCHATIM_USE_SQLCIPHER=ON`**（默认）时，CMake **只接受**树内 **SQLCipher amalgamation**，由工程**直接编译**。

## 必须提供的文件

**默认**：与本 `README.md` **同级**的 **`sqlite3.c`**、**`sqlite3.h`**。

**可选（Windows，便于与 OpenSSL 制品分开存放）**：若存在

**`prebuilt/windows-x64/amalgamation/sqlite3.c`** **与** **`sqlite3.h`**，

CMake **优先**使用该目录（见 **`prebuilt/windows-x64/README.md`**）。

| 文件 | 说明 |
|------|------|
| **`sqlite3.c`** | 带 codec 的 amalgamation |
| **`sqlite3.h`** | 头文件 |

若**两处均无**完整一对文件，配置阶段会 **`FATAL_ERROR`**（或 **`-DZCHATIM_USE_SQLCIPHER=OFF`** 使用明文 **`thirdparty/sqlite`**，仅建议开发调试用）。

## CMake 编译 `sqlite3.c` 时的宏（已由 `ZChatIM/CMakeLists.txt` 设置）

与官方生成 amalgamation 时的 **`OPTIONS`** 对齐，否则 **`sqlite3.c`** 内 **`#error`** 会失败：

- **`SQLITE_HAS_CODEC`**、**`SQLITE_TEMP_STORE=2`**
- **`SQLITE_THREADSAFE=2`**
- **`SQLITE_EXTRA_INIT=sqlcipher_extra_init`**、**`SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown`**

**注意**：当前官方 amalgamation 里 **`stdint.h` 的 `#include` 出现在 SQLCipher 的 `uint64_t`（xoshiro）代码之后。CMake 对 **`zchatim_sqlcipher`** 使用 **MSVC：`/FIstdint.h`**、**GCC/Clang：`-include stdint.h`**，避免 **`uint64_t` 未定义**导致的大量解析错误。

## 如何获得文件（仅在有制品源时执行一次）

在**可访问制品库**的环境从 **SQLCipher 官方或内网镜像**取得与团队**冻结版本**一致的源码，生成或提取 **amalgamation** 后，将 **`sqlite3.c` / `sqlite3.h`** 放入本目录，再整包同步至**无外网**构建环境。

### 若只有 GitHub 解压目录（例如仓库根下 `temp/sqlcipher-4.14.0/`）

那是**完整源码树**，里面**没有**现成的 `sqlite3.c`，必须先**生成 amalgamation**，再**复制**到本目录（与本文 `README.md` 同级）。

**Windows（MSVC，x64）** — 在 **「x64 Native Tools Command Prompt for VS」** 中进入源码根目录后执行（**一行**；`OPTIONS` 内容来自 [SQLCipher README — Compiling](https://github.com/sqlcipher/sqlcipher)）：

```bat
cd temp\sqlcipher-4.14.0
nmake /f Makefile.msc OPTIONS="-DSQLITE_HAS_CODEC -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -DSQLITE_TEMP_STORE=2" sqlite3.c
```

成功后源码根目录会出现 **`sqlite3.c`**、**`sqlite3.h`**，将其**复制**到 **`ZChatIM/thirdparty/sqlcipher/`**。  
说明：生成 amalgamation **一般不需要**本机已装 OpenSSL；**编译 ZChatIM** 时 Windows 仍要按下文链接 **OpenSSL**（仅 SQLCipher）。

**Linux / macOS** — 在源码根目录按 SQLCipher 文档 **`./configure --with-tempstore=yes`** 并带上 **`CFLAGS="-DSQLITE_HAS_CODEC -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown"`**，再 **`make sqlite3.c`**，然后把生成的 **`sqlite3.c` / `sqlite3.h`** 拷入本目录。

## OpenSSL

**全平台 OpenSSL 3**：SQLCipher 与 **MM2 / MM1 / 随机数** **共用**同一 **`OPENSSL_ROOT_DIR`**（或自动探测路径，见 **`thirdparty/openssl/LAYOUT.md`**）。

## 许可

SQLCipher（Zetetic）与 OpenSSL 的再分发请在合规流程中确认。
