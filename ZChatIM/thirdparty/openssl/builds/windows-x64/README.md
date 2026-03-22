# Windows x64 — OpenSSL 构建工作区（可选）

用于你自己在仓库旁**归档**源码 / 构建目录 / **install 前缀**，便于与团队约定路径。

- **`openssl-src/`**（可选）：从 GitHub / 官网解压的源码树。  
- **`openssl-build/`**（可选）：`perl Configure` + `nmake` 的工作目录（若你分开建 out-of-source）。  
- **`install/`**（推荐）：`nmake install` 的目标前缀；完成后可将 **`install/*`** 复制到 **`prebuilt/windows-x64/openssl/`**，或把 **`OPENSSL_ROOT_DIR`** 指到 **`install/`**。

CMake **不会**自动搜索本目录下的子文件夹；请最终以 **`prebuilt/.../openssl`** 或 **`OPENSSL_ROOT_DIR`** 指向**安装根**。
