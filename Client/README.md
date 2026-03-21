# 客户端发布物目录

本目录用于集中存放 **IM 服务对应的终端客户端** 的**可分发构建产物**（安装包 / 可执行文件），例如：

| 形态 | 常见扩展名 | 说明 |
|------|------------|------|
| Android | `.apk`、`.aab` | 移动端安装包 |
| Linux（Debian/Ubuntu 等） | `.deb` | 系统包管理器安装 |
| Windows | `.exe`、`.msi` | 安装程序或便携可执行文件 |
| macOS | `.dmg`、`.pkg` | 磁盘映像或安装包 |
| 其它 | `.AppImage`、`扁平目录` 等 | 按团队约定放置 |

**约定**：

- **源码与工程**仍放在各自仓库或子模块（如 Android Studio 工程、Electron 工程等）；**`Client/` 只放「给用户安装/运行」的产物**，避免与 **`ZChatIM/`**（C++ 核心库）混淆。
- **Git**：仓库根目录 **`.gitignore`** 已默认忽略 **`Client/`** 下除 **`README.md`** 以外的文件，避免误提交大体积安装包；分发请优先用 **CI 制品库 / Release 附件**。若确需将某个文件入库：`git add -f Client/your-artifact.apk`。
- 命名建议带上 **版本号 + 平台 + 架构**（示例：`ZerOS-Chat-1.2.0-android-arm64.apk`），并在发布说明中写明**依赖的 IM 服务版本**与**最低系统版本**。

**与本仓库的关系**：

- 若客户端通过 **JNI** 调用 **`ZChatIM`**，契约见 **[`docs/06-Appendix/01-JNI.md`](../docs/06-Appendix/01-JNI.md)** 与 **[`ZChatIM/docs/JNI-API-Documentation.md`](../ZChatIM/docs/JNI-API-Documentation.md)**。
- 协议与消息语义见 **[`docs/01-Architecture/02-ZSP-Protocol.md`](../docs/01-Architecture/02-ZSP-Protocol.md)**。

---

**维护**：新增平台或分发格式时，在本表与「约定」中补一行即可。
