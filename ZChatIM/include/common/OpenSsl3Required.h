#pragma once

// 所有链接 **OpenSSL::Crypto** 的翻译单元应尽早 `#include "common/OpenSsl3Required.h"`，
// 在编译期拒绝 OpenSSL 1.x / 错配的 include 路径（与 **CMake `find_package(OpenSSL 3.0)`** 一致）。

#include <openssl/opensslv.h>

#if !defined(OPENSSL_VERSION_MAJOR) || (OPENSSL_VERSION_MAJOR < 3)
#    error "ZChatIM requires OpenSSL 3.x (libcrypto). Check OPENSSL_ROOT_DIR / thirdparty/openssl layout."
#endif
