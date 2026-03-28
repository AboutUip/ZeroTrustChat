#!/usr/bin/env bash
# 在 Linux 上生成 libZChatIMJNI.so（需 JAVA_HOME、libssl-dev、树内 SQLCipher）。
set -euo pipefail
cd "$(dirname "$0")"
if [[ -z "${JAVA_HOME:-}" ]]; then
  echo "请设置 JAVA_HOME 指向 JDK（含 include/linux/jni_md.h）" >&2
  exit 1
fi
cmake --preset linux-jni-so-only
cmake --build --preset linux-jni-so-only-release
echo "JNI 库: $(pwd)/build-linux/lib/libZChatIMJNI.so"
echo "Java 运行示例: -Dzchat.native.dir=$(pwd)/build-linux/lib"
echo "（请确保该目录下尚有 libcrypto.so.3 / libssl.so.3 或已加入 LD_LIBRARY_PATH）"
