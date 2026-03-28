#!/usr/bin/env bash
# 将 libZChatIMJNI.so 与系统 OpenSSL 3 复制到 ZChatServer 的 classpath 目录，供 Linux 上 NativeLibraryLoader 从 /native/linux-x64/ 加载。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST="$SERVER_DIR/src/main/resources/native/linux-x64"
ZCHATIM_ROOT="${ZCHATIM_ROOT:-"$SERVER_DIR/../ZChatIM"}"
SSL_LIB="${SSL_LIB:-/usr/lib/x86_64-linux-gnu}"

JNI_SO="$ZCHATIM_ROOT/build-linux/lib/libZChatIMJNI.so"
CRYPTO_SO="$SSL_LIB/libcrypto.so.3"
SSL_SO="$SSL_LIB/libssl.so.3"

for f in "$JNI_SO" "$CRYPTO_SO" "$SSL_SO"; do
  if [[ ! -f "$f" ]]; then
    echo "缺少文件: $f" >&2
    echo "请先编译 ZChatIM（ZChatIM/build-jni-linux.sh），并安装 libssl3 / 检查 SSL_LIB（当前 SSL_LIB=$SSL_LIB）" >&2
    exit 1
  fi
done

mkdir -p "$DEST"
cp -L "$JNI_SO" "$CRYPTO_SO" "$SSL_SO" "$DEST/"
echo "已复制到 $DEST :"
ls -la "$DEST"
