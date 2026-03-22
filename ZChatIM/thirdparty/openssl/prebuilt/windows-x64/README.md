# Windows x64 — OpenSSL **安装树**落点

把 **MSVC x64** 编好的 OpenSSL **安装根**（含 **`include/openssl/ssl.h`**、**`lib/libcrypto.lib`**）放到：

**`openssl/`** 子目录（与本文同级）：

```
prebuilt/windows-x64/openssl/
  include/openssl/ssl.h
  lib/libcrypto.lib
  lib/libssl.lib
  bin/*.dll              （动态链时）
```

**不要**只放 `.tar.gz` 源码；必须是 **已 install** 的头文件 + 导入库（及可选 DLL）。

整理步骤示例：

1. 本地 `nmake install` 到例如 `D:\out\openssl-install\`  
2. 将 **`D:\out\openssl-install\*`** 全部复制到本目录下的 **`openssl\`**  
3. 在 **`ZChatIM/`** 下重新运行 CMake（或 `-DOPENSSL_ROOT_DIR=...`）

大文件默认 **不入 Git**（见仓库根 **`.gitignore`**）；内网用压缩包或制品库同步。
