# 技术文档索引

本目录：协议、业务、存储与 JNI 契约。C++ 构建与实现跟踪见 [`ZChatIM/docs/`](../ZChatIM/docs/README.md)。

## 体例

- 陈述句、现在时；避免口语与营销用语。
- 标识符、路径、命令用反引号。
- 与实现冲突时以 [`AUTHORITY.md`](AUTHORITY.md) 为准。

## 索引

| 路径 | 说明 |
|------|------|
| [AUTHORITY.md](AUTHORITY.md) | 权威顺序、持久化摘要、交付判定 |
| [01-Architecture/01-Overview.md](01-Architecture/01-Overview.md) | 架构总览 |
| [01-Architecture/02-ZSP-Protocol.md](01-Architecture/02-ZSP-Protocol.md) | ZSP |
| [02-Core/01-MM1.md](02-Core/01-MM1.md) | MM1 |
| [02-Core/02-MM2.md](02-Core/02-MM2.md) | MM2 概念 |
| [02-Core/03-Storage.md](02-Core/03-Storage.md) | 元数据、MM2、密钥 4.2～4.3、第七节 |
| [02-Core/04-ZdbBinaryLayout.md](02-Core/04-ZdbBinaryLayout.md) | `.zdb` v1 |
| [03-Business/](03-Business/01-SpringBoot.md) | Spring、认证、群、会话、密钥、销户 |
| [04-Features/README.md](04-Features/README.md) | 功能规范分组 |
| [05-Operations/01-Backup.md](05-Operations/01-Backup.md) | 备份 |
| [06-Appendix/01-JNI.md](06-Appendix/01-JNI.md) | JNI 契约与类型 |
| [06-Appendix/03-Version.md](06-Appendix/03-Version.md) | 版本 |
| [ZChatIM/docs/Build.md](../ZChatIM/docs/Build.md) | CMake、测试入口 |
| [ZChatIM/docs/Implementation-Status.md](../ZChatIM/docs/Implementation-Status.md) | 实现与风险 |
| [ZChatIM/docs/Scope.md](../ZChatIM/docs/Scope.md) | 范围、非交付项、阻塞 |
| [ZChatIM/docs/JNI-API-Documentation.md](../ZChatIM/docs/JNI-API-Documentation.md) | JNI 边界、场景表、分组路由 |
| [07-Engineering/README.md](07-Engineering/README.md) | 旧路径迁移说明 |

变更涉及 JNI 时须同步：`01-JNI.md`、`JniInterface.h`、`JniBridge.h`、`JNI-API-Documentation.md`。实现状态仅维护于 `ZChatIM/docs/Implementation-Status.md`。
