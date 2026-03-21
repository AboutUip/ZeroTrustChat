# 本机 OpenSSL 预留目录（仅 Linux / macOS 构建）

**Windows 上构建 ZChatIM 不需要 OpenSSL**（MM2 使用 **BCrypt**）。本目录供 **Linux、macOS** 在无法使用系统包管理器时，把 **OpenSSL 3** 预编译包放在本地使用。

## 目录结构

```
thirdparty/openssl/
  README.md                 ← 本说明
  versions/
    README.md
    openssl-3.x.x-linux/    ← 某一版本根目录（含 include/、lib/）
    current/                ← 指向当前使用的版本（或复制一份）
```

若存在 **`versions/current/include/openssl/ssl.h`**，且 CMake 未设置 **`OPENSSL_ROOT_DIR`**，**`CMakeLists.txt`**（非 Windows）会自动使用 **`current`**。

**`versions/*` 下内容默认不提交 Git**（见仓库根 `.gitignore`）。
