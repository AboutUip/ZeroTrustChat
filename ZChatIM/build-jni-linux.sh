#!/usr/bin/env bash
# 在 Linux 上生成 libZChatIMJNI.so（需 JAVA_HOME、libssl-dev、树内 SQLCipher）。
set -euo pipefail
cd "$(dirname "$0")"
if [[ -z "${JAVA_HOME:-}" ]]; then
  echo "请设置 JAVA_HOME 指向 JDK（含 include/linux/jni_md.h）" >&2
  exit 1
fi
# 仓库内 amalgamation 在 prebuilt/windows-x64/amalgamation/；根目录常为空。复制后 CMake 与 Windows 使用「根目录优先」逻辑一致，且不改变 Windows 构建路径（本脚本仅在 Linux 使用）。
_sql_root="$(pwd)/thirdparty/sqlcipher"
_sql_prebuilt="$_sql_root/prebuilt/windows-x64/amalgamation"
if [[ ! -f "$_sql_root/sqlite3.c" || ! -f "$_sql_root/sqlite3.h" ]]; then
  if [[ -f "$_sql_prebuilt/sqlite3.c" && -f "$_sql_prebuilt/sqlite3.h" ]]; then
    echo "SQLCipher: 从 prebuilt/windows-x64/amalgamation 复制 sqlite3.c、sqlite3.h 到 thirdparty/sqlcipher/（供本机构建；勿误提交若团队仅用 prebuilt）"
    cp -f "$_sql_prebuilt/sqlite3.c" "$_sql_prebuilt/sqlite3.h" "$_sql_root/"
  else
    echo "未找到 SQLCipher amalgamation。需要以下任一：" >&2
    echo "  $_sql_root/sqlite3.c 与 sqlite3.h" >&2
    echo "  或 $_sql_prebuilt/sqlite3.c 与 sqlite3.h（完整 git clone 应包含）" >&2
    echo "详见 thirdparty/sqlcipher/README.md；或 -DZCHATIM_USE_SQLCIPHER=OFF（仅开发）" >&2
    exit 1
  fi
fi
cmake --preset linux-jni-so-only
cmake --build --preset linux-jni-so-only-release
echo "JNI 库: $(pwd)/build-linux/lib/libZChatIMJNI.so"
echo "Java 运行示例: -Dzchat.native.dir=$(pwd)/build-linux/lib"
echo "（请确保该目录下尚有 libcrypto.so.3 / libssl.so.3 或已加入 LD_LIBRARY_PATH）"
