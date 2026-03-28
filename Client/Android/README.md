# ZChat Android 客户端

本目录为 **ZSP 单连接参考实现**：纯 Java/Kotlin 侧与 `ZspSessionManager` 通信，**不嵌入** `ZChatIM` JNI。应用 ID：`com.kite.zchat`。

## 构建

| 项 | 说明 |
|----|------|
| JDK | 11（见 `app/build.gradle.kts` `compileOptions`） |
| Android Gradle Plugin | `gradle/libs.versions.toml` → `agp` |
| 最低 SDK | `minSdk = 29` |
| 目标 SDK | `targetSdk = 35` |

```bash
cd Client/Android
./gradlew :app:assembleDebug
./gradlew :app:assembleRelease
```

Windows 使用 `gradlew.bat`。首次构建需可访问 **Google Maven** 与 **Maven Central**（Firebase、WebRTC 等）。

## 工程结构（摘要）

| 路径 | 说明 |
|------|------|
| `app/src/main/java/com/kite/zchat/` | 界面、ZSP、聊天、语音通话 |
| `app/src/main/java/com/kite/zchat/zsp/` | ZSP 帧、会话、明文 IM 载荷（`ZspChatWire`） |
| `app/src/main/java/com/kite/zchat/chat/` | 本地消息库 `ChatMessageDb`、`ChatSync`、引用编解码 |
| `app/src/main/java/com/kite/zchat/call/` | WebRTC 音频会话、信令、来电调度（`VoiceCallEngine`） |
| `app/src/main/java/com/kite/zchat/push/` | FCM、通知渠道、好友请求轮询 Worker |

## 协议与产品文档

| 主题 | 文档 |
|------|------|
| ZSP 与 SYNC 语义（网关侧） | [`docs/01-Architecture/02-ZSP-Protocol.md`](../../docs/01-Architecture/02-ZSP-Protocol.md) |
| 消息同步（含 Android 客户端策略） | [`docs/04-Features/01-MessageSync.md`](../../docs/04-Features/01-MessageSync.md) |
| Android 参考客户端规范 | [`docs/04-Features/14-Android-Client-ZChat.md`](../../docs/04-Features/14-Android-Client-ZChat.md) |
| 引用回复格式 | [`docs/04-Features/10-MessageReply.md`](../../docs/04-Features/10-MessageReply.md) |

## 依赖要点

- **WebRTC**：`io.github.webrtc-sdk:android`（版本见 `libs.versions.toml`），避免使用不完整 AAR 导致运行时缺类（如 `org.webrtc.Environment`）。
- **Firebase**：`google-services.json` 由项目配置；用于 FCM 与数据消息唤醒同步。
- **BouncyCastle**：`bcprov-jdk18on`，与 ZSP/业务加解密约定一致。

## 维护提示

- 变更 SYNC、信令或 WebRTC 行为时，同步更新 [`docs/04-Features/14-Android-Client-ZChat.md`](../../docs/04-Features/14-Android-Client-ZChat.md) 与 [`01-MessageSync.md`](../../docs/04-Features/01-MessageSync.md) 第七节。
- 根目录 [`Client/README.md`](../README.md) 描述 `Client/` 下**发布物**约定；**源码**以本目录为准。
