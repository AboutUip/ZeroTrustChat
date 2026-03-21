# ZerOS-Chat · ZChatIM

面向**可控内网 / 强隔离网络**的即时通讯实现：将密码学、密钥与会话策略、本地加密落盘收敛到 **C++ 可信基（MM1 / MM2）**；Java 侧承担 ZSP 编解码与编排，**不**作为安全态宿主。

**规范权威**：**[`docs/README.md`](docs/README.md)**（目录索引、规范性引用链、维护约定）。

| 资源 | 链接 |
|------|------|
| 架构总览 | [`docs/01-Architecture/01-Overview.md`](docs/01-Architecture/01-Overview.md) |
| ZSP 协议 | [`docs/01-Architecture/02-ZSP-Protocol.md`](docs/01-Architecture/02-ZSP-Protocol.md) |
| C++ 实现状态 | [`docs/02-Core/05-ZChatIM-Implementation-Status.md`](docs/02-Core/05-ZChatIM-Implementation-Status.md) |
| JNI 接口表 | [`docs/06-Appendix/01-JNI.md`](docs/06-Appendix/01-JNI.md) |
| JNI 细则 | [`ZChatIM/docs/JNI-API-Documentation.md`](ZChatIM/docs/JNI-API-Documentation.md) |
| 构建 | [`docs/07-Engineering/01-Build-ZChatIM.md`](docs/07-Engineering/01-Build-ZChatIM.md) |

**许可证**：MIT（见仓库内 **`LICENSE`**）。

---

## 仓库布局

| 路径 | 说明 |
|------|------|
| **`docs/`** | 架构、协议、核心模块、业务、功能、运维、附录、工程（构建） |
| **`ZChatIM/`** | C++ 实现：CMake、头文件与源码、`jni/` |
| **`Client/`** | 终端客户端发布物（`.apk` / `.deb` / `.exe` 等），见 **[`Client/README.md`](Client/README.md)** |

Java / Spring Boot 工程可与本仓库解耦；与 MM1 的调用关系见 **`docs/03-Business/01-SpringBoot.md`**。

---

## 构建 ZChatIM（摘要）

完整说明见 **[`docs/07-Engineering/01-Build-ZChatIM.md`](docs/07-Engineering/01-Build-ZChatIM.md)**。

```bash
cd ZChatIM
cmake -B build -DZCHATIM_BUILD_JNI=OFF
cmake --build build --config Release
```

- **Windows**：MM2 使用 **BCrypt**，**无需** OpenSSL。
- **Linux / macOS**：须 **OpenSSL 3.x**；缺失时 CMake **失败**并提示安装路径。
- **Visual Studio 多配置**：请使用 **`--config Release`** 或在 IDE 中选择 **Release** 再生成。
- **树内 `--test` / `--test-minimal` / `--test-mm250`**：默认**不**编入 EXE；需要时在配置阶段加 **`-DZCHATIM_BUILD_TESTS=ON`**（见 **[构建说明](docs/07-Engineering/01-Build-ZChatIM.md)** 第6.1节）。

---

## 设计要点（只读摘要）

| 原则 | 含义 |
|------|------|
| 可信基极小化 | 敏感逻辑在 **MM1 / MM2**；网关为不可信区，仅路由与 JNI 调度。 |
| Java 不持密 | 业务层不处理安全载荷明文；密钥与策略在 MM1。 |
| JNI 边界 | 业务入口 **`callerSessionId`** 与头文件、**`01-JNI.md`** 严格一致；见 **`JniSecurityPolicy.h`**。 |
| 加密落盘 | 消息密文与文件分片在 **`.zdb`**；SQLite 为索引与元数据。详情见 **`docs/02-Core/03-Storage.md`**。 |

细则与表格见 **`docs/README.md` 第2节** 及架构/业务各篇。
