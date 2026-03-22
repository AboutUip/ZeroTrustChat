# OpenSSL 产物目录约定（本机 / 内网制品）

CMake **自动探测**顺序见根目录 **`README.md`**。下面是你把 **自己编译好的 Win64 OpenSSL** 放进仓库时的**推荐布局**（可只填其中一种；**`include/openssl/ssl.h` + `lib/libcrypto.lib`** 为必需）。

## 1) 推荐：预编译根（CMake 首选）

将 **`nmake install`**（或等价）生成的整棵安装树拷到：

```
thirdparty/openssl/prebuilt/windows-x64/openssl/
  include/openssl/ssl.h
  include/openssl/...
  lib/libcrypto.lib
  lib/libssl.lib          （通常也有）
  lib/engines-3/...       （若有）
  bin/libcrypto-3-x64.dll （若动态链接；可选）
  bin/libssl-3-x64.dll
```

**环境变量 / 缓存**：也可设 **`OPENSSL_ROOT_DIR`** 指向上述 **`openssl`** 目录（或你的任意安装根）。

## 2) 可选：按「构建阶段」归档（便于你同步多份制品）

若你希望 **install 输出**与**中间构建**分开存放：

```
thirdparty/openssl/builds/windows-x64/
  README.md
  openssl-src/            ← 可选：解压的源码（可不提交）
  openssl-build/          ← 可选：nmake 的工作目录
  install/                ← 推荐：与 §1 相同结构（可整棵复制到 prebuilt/.../openssl）
```

CMake **默认不**扫描 `builds/` 子路径；完成后请把 **`install/`** 再**复制**到 **`prebuilt/windows-x64/openssl/`**，或设 **`OPENSSL_ROOT_DIR`** 指向 **`install/`**。

## 3) 兼容旧路径

仍支持（探测顺序靠后）：

- `thirdparty/openssl/versions/win64/current/`
- `thirdparty/openssl/versions/current/`

## Linux / macOS

继续使用系统 **`libssl-dev` / Homebrew**，或把安装树放到 **`versions/current/`**（与历史文档一致）。
