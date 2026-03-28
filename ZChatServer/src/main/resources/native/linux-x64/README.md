# Linux x64 原生库（classpath：`/native/linux-x64/`）

`NativeLibraryLoader` 在 Linux amd64 上按顺序加载（见 `LINUX_SO_ORDER`）：

| 文件名 | 说明 |
|--------|------|
| `libcrypto.so.3` | OpenSSL 3 |
| `libssl.so.3` | OpenSSL 3 |
| `libZChatIMJNI.so` | ZChatIM JNI（`ZChatIM/build-linux/lib/`） |

三者齐全时可从 JAR 内解压加载，**无需** `-Dzchat.native.dir`。

## 填充目录

```bash
bash ZChatServer/scripts/populate-linux-native-resources.sh
```

可选：`ZCHATIM_ROOT`、`SSL_LIB`（默认 `/usr/lib/x86_64-linux-gnu`）。

手动：`cp -L` 上述三文件到本目录（`cp -L` 便于打进 fat JAR）。

## 语言级别 vs Linux 上的 JVM

| | 说明 |
|---|------|
| **项目** | **`pom.xml` 中 `java.version` 为 17**，字节码按 17 编译即可 |
| **Linux 运行** | 使用 **JDK 25** 没有问题；JVM 能运行更低版本字节码 |

JDK 25 ≥ 24，在 **本机用 25 跑 Maven** 时会启用 `jdk24-jvm-warnings` profile（Surefire / `spring-boot:run`）。**仅 `java -jar` 部署时**，启动进程**不会**自动带上这些参数，须在 Linux 上显式设置，例如：

```bash
export JAVA_TOOL_OPTIONS='--sun-misc-unsafe-memory-access=allow --enable-native-access=ALL-UNNAMED'
java -jar ZChat-0.0.1-SNAPSHOT.jar
```

（参数与 `pom.xml` 中 profile 一致，供 Netty / JNI。）

## 最小部署

目标机只需 **JDK（如 25）** + **已打入本目录 `.so` 的 fat JAR** + **外置配置**；不必部署整仓。数据目录由 `zchat.zsp.native.data-dir` 等指定。

## 故障：`ZMK1 (Windows DPAPI) cannot be read on this platform`

说明 **`data-dir` 里的 `mm2_message_key.bin` 是在 Windows 上生成的**（ZMK1，DPAPI 保护），**不能**在 Linux 上打开。

**开发 / 全新 Linux 环境**：删掉该实例的数据与索引目录后重启（默认在 `ZChatServer/target/zchat-mm-data`、`zchat-mm-index`，或你配置的 `ZCHAT_MM_DATA_DIR` / `ZCHAT_MM_INDEX_DIR`）。Linux 上会重新生成 **ZMK2**（与机器与路径绑定）。**注意**：原 Windows 下的密文数据无法用同一密钥直接接续，除非做专门迁移。

若使用 **口令型密钥文件（ZMKP）**，在配置里设置 **`zchat.zsp.native.passphrase`**（或环境变量），与 ZMK1 无关。
