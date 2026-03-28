# =============================================================
# ZChatIM - 高安全即时通讯系统 C++ 安全模块
# =============================================================

cmake_minimum_required(VERSION 3.20)

# 项目信息（**`ZCHATIM_USE_SQLCIPHER=OFF`** 时编 **`thirdparty/sqlite/sqlite3.c`**，需 **C**；默认 ON 为 SQLCipher，无该 amalgamation）
project(ZChatIM VERSION 1.0.0 LANGUAGES CXX C)

# =========================
# Options
# =========================
# 元数据索引 **`zchatim_metadata.db`**：默认 **SQLCipher**（页级加密 + 固定「严格」PRAGMA，见 `docs/02-Core/03-Storage.md` 第4.2节）。
# 关闭则回退 **`thirdparty/sqlite/sqlite3.c`** 明文库（仅建议本地调试）。
option(ZCHATIM_USE_SQLCIPHER "Link SQLCipher for encrypted metadata SQLite (see docs)" ON)

# Single switch for outputs (VS: CMakePresets — windows-exe-only / windows-jni-dll-only / windows-both).
set(ZCHATIM_BUILD_MODE "Both" CACHE STRING "ExeOnly | JniDllOnly | Both")
set_property(CACHE ZCHATIM_BUILD_MODE PROPERTY STRINGS ExeOnly JniDllOnly Both)
if(ZCHATIM_BUILD_MODE STREQUAL "ExeOnly")
    set(ZCHATIM_BUILD_EXE ON)
    set(ZCHATIM_BUILD_JNI OFF)
elseif(ZCHATIM_BUILD_MODE STREQUAL "JniDllOnly")
    set(ZCHATIM_BUILD_EXE OFF)
    set(ZCHATIM_BUILD_JNI ON)
elseif(ZCHATIM_BUILD_MODE STREQUAL "Both")
    set(ZCHATIM_BUILD_EXE ON)
    set(ZCHATIM_BUILD_JNI ON)
else()
    message(FATAL_ERROR "ZCHATIM_BUILD_MODE must be ExeOnly, JniDllOnly, or Both (got '${ZCHATIM_BUILD_MODE}')")
endif()

# Only when ZChatIM.exe is built: merge tests/*.cpp (--test).
option(ZCHATIM_BUILD_TESTS "Compile in-tree tests into ZChatIM.exe" ON)

# 密码学后端（**全平台 OpenSSL 3**）：
# - **MM2 `Crypto.cpp`**：**EVP_aes_256_gcm** / **PKCS5_PBKDF2_HMAC** / **RAND_bytes**。
# - **`common/Ed25519.cpp`**：**EVP_PKEY_ED25519** 验签。
# - **`common/Random.cpp`**、**`AuthSessionManager`** 安全随机：**RAND_bytes**（失败再 **/dev/urandom** 仅 Unix）。
# - **SQLCipher**：**OpenSSL::SSL** + **OpenSSL::Crypto**（默认 ON）。
# Windows：**无系统 OpenSSL** → 将 **install 树**放到 **`thirdparty/openssl/prebuilt/windows-x64/openssl/`** 等（见 **`thirdparty/openssl/LAYOUT.md`**）或 **`OPENSSL_ROOT_DIR`**。
set(ZCHATIM_MM2_CRYPTO_BACKEND "OpenSSL3")
if(NOT OPENSSL_ROOT_DIR AND DEFINED ENV{OPENSSL_ROOT_DIR})
    file(TO_CMAKE_PATH "$ENV{OPENSSL_ROOT_DIR}" _zchatim_openssl_root)
    set(OPENSSL_ROOT_DIR "${_zchatim_openssl_root}" CACHE PATH "OpenSSL installation root (override if FindOpenSSL fails)")
endif()
if(NOT OPENSSL_ROOT_DIR)
    set(_zchatim_ssl_candidates "")
    if(WIN32)
        list(APPEND _zchatim_ssl_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openssl/prebuilt/windows-x64/openssl"
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openssl/builds/windows-x64/install"
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openssl/versions/win64/current"
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openssl/versions/current"
        )
    else()
        list(APPEND _zchatim_ssl_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openssl/versions/current"
        )
    endif()
    foreach(_root IN LISTS _zchatim_ssl_candidates)
        if(EXISTS "${_root}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_root}" CACHE PATH "OpenSSL root (auto from thirdparty layout)")
            message(STATUS "OPENSSL_ROOT_DIR (auto): ${_root}")
            break()
        endif()
    endforeach()
endif()
find_package(OpenSSL 3.0 REQUIRED)
if(OPENSSL_ROOT_DIR)
    message(STATUS "OPENSSL_ROOT_DIR (effective): ${OPENSSL_ROOT_DIR}")
endif()
if(NOT TARGET OpenSSL::Crypto)
    message(FATAL_ERROR
        "Could not find OpenSSL 3.0 or newer.\n"
        "\n"
        "Fix:\n"
        "  Linux: sudo apt install libssl-dev\n"
        "  macOS: brew install openssl@3 && export OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)\n"
        "  Windows: install tree with include/openssl/ssl.h + libcrypto →\n"
        "    thirdparty/openssl/prebuilt/windows-x64/openssl/  (see thirdparty/openssl/LAYOUT.md)\n"
        "    or set OPENSSL_ROOT_DIR.\n"
        "See ZChatIM/docs/Build.md\n"
    )
endif()

# JAVA_HOME 兜底时按平台追加 jni.h 子目录（FindJNI 成功时自带正确路径）。
macro(zchatim_jni_includes_from_java_home jdk_home)
    set(JNI_INCLUDE_DIRS "${jdk_home}/include")
    if(WIN32)
        list(APPEND JNI_INCLUDE_DIRS "${jdk_home}/include/win32")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND JNI_INCLUDE_DIRS "${jdk_home}/include/linux")
    elseif(APPLE)
        list(APPEND JNI_INCLUDE_DIRS "${jdk_home}/include/darwin")
    endif()
endmacro()

# C++ 标准配置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C 标准（SQLite amalgamation）
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# MSVC: 源文件按 UTF-8 解析，避免中文注释在代码页 936 下告警/乱码断行
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 输出目录（库 / 归档）
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 头文件包含路径（包含头文件骨架）
include_directories(include)

# =========================
# SQLite / SQLCipher（元数据索引，docs/02-Core/03-Storage.md）
# =========================
# **不使用 vcpkg / 系统 sqlcipher 包**：`ZCHATIM_USE_SQLCIPHER=ON` 时**仅**接受树内 amalgamation。
set(_zchatim_sqlcipher_vendor_dir "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlcipher")
set(_zchatim_sqlcipher_prebuilt "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlcipher/prebuilt/windows-x64/amalgamation")
if(WIN32
   AND EXISTS "${_zchatim_sqlcipher_prebuilt}/sqlite3.c"
   AND EXISTS "${_zchatim_sqlcipher_prebuilt}/sqlite3.h")
    set(_zchatim_sqlcipher_vendor_dir "${_zchatim_sqlcipher_prebuilt}")
    message(STATUS "SQLCipher amalgamation dir: prebuilt/windows-x64/amalgamation")
endif()

if(ZCHATIM_USE_SQLCIPHER)
    message(STATUS "SQLCipher amalgamation path: ${_zchatim_sqlcipher_vendor_dir}")
    if(NOT EXISTS "${_zchatim_sqlcipher_vendor_dir}/sqlite3.c"
       OR NOT EXISTS "${_zchatim_sqlcipher_vendor_dir}/sqlite3.h")
        message(FATAL_ERROR
            "ZCHATIM_USE_SQLCIPHER=ON requires SQLCipher amalgamation in-tree (no vcpkg / no pkg-config sqlcipher):\n"
            "  ${_zchatim_sqlcipher_vendor_dir}/sqlite3.c\n"
            "  ${_zchatim_sqlcipher_vendor_dir}/sqlite3.h\n"
            "See thirdparty/sqlcipher/README.md (optional: prebuilt/windows-x64/amalgamation/)\n"
            "Or -DZCHATIM_USE_SQLCIPHER=OFF for plaintext metadata (dev only).\n"
        )
    endif()

    add_library(zchatim_sqlcipher STATIC
        "${_zchatim_sqlcipher_vendor_dir}/sqlite3.c"
    )
    target_include_directories(zchatim_sqlcipher PUBLIC
        "${_zchatim_sqlcipher_vendor_dir}"
    )
    # 与 SQLCipher 官方 amalgamation 要求一致（见 amalgamation 内 #error 与 thirdparty/sqlcipher/README.md）
    target_compile_definitions(zchatim_sqlcipher PRIVATE
        SQLITE_HAS_CODEC
        SQLITE_TEMP_STORE=2
        SQLITE_THREADSAFE=2
        SQLITE_EXTRA_INIT=sqlcipher_extra_init
        SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown
        SQLITE_ENABLE_RTREE
        SQLITE_ENABLE_UNLOCK_NOTIFY
        SQLITE_ENABLE_COLUMN_METADATA
    )
    # amalgamation 中 SQLCipher 段（~109625 行）使用 uint64_t，但 #include <stdint.h> 在更后（~14677）才出现；
    # 强制先包含 stdint.h，否则 MSVC 会报 xoshiro_s / '[' 等一串语法错误。
    if(MSVC)
        target_compile_options(
            zchatim_sqlcipher PRIVATE
            /FIstdint.h
            /wd4244 /wd4267 /wd4018 /wd4310
        )
    else()
        target_compile_options(zchatim_sqlcipher PRIVATE -include stdint.h)
    endif()
    target_link_libraries(zchatim_sqlcipher PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(zchatim_sqlcipher PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(zchatim_sqlcipher PRIVATE m)
        endif()
    endif()
    set(_zchatim_sqlite_link_target zchatim_sqlcipher)
else()
    add_library(sqlite3 STATIC
        thirdparty/sqlite/sqlite3.c
    )
    target_include_directories(sqlite3 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite
    )
    if(MSVC)
        target_compile_options(sqlite3 PRIVATE /wd4244 /wd4267 /wd4018 /wd4310)
    endif()
    set(_zchatim_sqlite_link_target sqlite3)
endif()

# =========================
# 核心静态库（MM1 管理等实现）
# =========================
add_library(ZChatIMCore STATIC
    src/common/Utils.cpp
    src/common/Ed25519.cpp
    src/common/Memory.cpp
    src/common/String.cpp
    src/common/File.cpp
    src/common/Time.cpp
    src/common/Random.cpp
    src/common/Logger.cpp
    src/mm1/MM1.cpp
    src/mm1/MM1_security_submodules.cpp
    src/mm1/managers/AuthSessionManager.cpp
    src/mm1/managers/LocalAccountCredentialManager.cpp
    src/mm1/managers/RtcCallSessionManager.cpp
    src/mm1/managers/VoiceVideoCallManager.cpp
    src/mm1/managers/RtcCallManager.cpp
    src/mm1/managers/MediaCallCoordinator.cpp
    src/mm1/managers/MessageReplyManager.cpp
    src/mm1/managers/MessageRecallManager.cpp
    src/mm1/managers/UserDataManager.cpp
    src/mm1/managers/FriendManager.cpp
    src/mm1/managers/GroupManager.cpp
    src/mm1/managers/GroupNameManager.cpp
    src/mm1/managers/GroupMuteManager.cpp
    src/mm1/managers/MessageEditManager.cpp
    src/mm1/managers/MentionPermissionManager.cpp
    src/mm1/managers/FriendNoteManager.cpp
    src/mm1/managers/AccountDeleteManager.cpp
    src/mm1/managers/FriendVerificationManager.cpp
    src/mm1/managers/MM1_manager_stubs.cpp
    src/mm1/managers/SessionActivityManager.cpp
    src/jni/JniBridge.cpp
    src/jni/JniInterface.cpp
    src/mm2/MM2.cpp
    src/mm2/MM2_message_key_passphrase.cpp
    src/mm2/crypto/Sha256.cpp
    src/mm2/storage/Crypto.cpp
    src/mm2/storage/BlockIndex.cpp
    src/mm2/storage/MessageQueryManager.cpp
    src/mm2/storage/SqliteMetadataDb.cpp
    src/mm2/storage/StorageIntegrityManager.cpp
    src/mm2/storage/ZdbFile.cpp
    src/mm2/storage/ZdbManager.cpp
)
target_include_directories(ZChatIMCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# **`SQLITE_HAS_CODEC`**：包含 **`sqlite3.h`** 时暴露 **`sqlite3_key_v2`** 等 SQLCipher API（与 **`zchatim_sqlcipher`** 编译 **`sqlite3.c`** 时一致）。
target_compile_definitions(ZChatIMCore PUBLIC
    $<$<BOOL:${ZCHATIM_USE_SQLCIPHER}>:ZCHATIM_USE_SQLCIPHER=1>
    $<$<BOOL:${ZCHATIM_USE_SQLCIPHER}>:SQLITE_HAS_CODEC=1>
)

target_link_libraries(ZChatIMCore PUBLIC OpenSSL::Crypto PRIVATE ${_zchatim_sqlite_link_target})
if(WIN32)
    # DPAPI：**mm2_message_key.bin**（**`MM2.cpp`** — **CryptProtectData** / **CryptUnprotectData**）
    target_link_libraries(ZChatIMCore PUBLIC crypt32)
elseif(APPLE)
    # **ZMK3**：**Keychain** 存 **32B 封装密钥**（**`MM2_message_key_darwin.cpp`**）
    target_sources(ZChatIMCore PRIVATE src/mm2/MM2_message_key_darwin.cpp)
    target_link_libraries(ZChatIMCore PRIVATE "-framework" "CoreFoundation" "-framework" "Security")
endif()

# 供 `mm1::security::JniSecurity` 使用 `jni.h`（与 ZChatIMJNI 同源；仅头文件 + 条件编译）。
if(ZCHATIM_BUILD_JNI)
    find_package(JNI QUIET)
    if(NOT JNI_FOUND AND DEFINED ENV{JAVA_HOME})
        file(TO_CMAKE_PATH "$ENV{JAVA_HOME}" _zchatim_jh)
        zchatim_jni_includes_from_java_home("${_zchatim_jh}")
        set(JNI_FOUND TRUE)
    endif()
    if(JNI_FOUND)
        target_include_directories(ZChatIMCore PRIVATE ${JNI_INCLUDE_DIRS})
        target_compile_definitions(ZChatIMCore PRIVATE ZCHATIM_HAVE_JNI=1)
    endif()
endif()

# =========================
# JNI shared library (optional)
# =========================
if(ZCHATIM_BUILD_JNI)
    # 与上一段一致：若仍未有 JNI 头（例如仅编 JNI 目标而 Core 未开 JNI 宏），再解析一次。
    if(NOT JNI_FOUND)
        find_package(JNI QUIET)
    endif()
    if(NOT JNI_FOUND AND DEFINED ENV{JAVA_HOME})
        file(TO_CMAKE_PATH "$ENV{JAVA_HOME}" _zchatim_jh2)
        zchatim_jni_includes_from_java_home("${_zchatim_jh2}")
        set(JNI_FOUND TRUE)
    endif()
    if(NOT JNI_FOUND)
        message(WARNING
            "JNI not found (and JAVA_HOME is not set). ZChatIMJNI will be skipped. "
            "Set JAVA_HOME to a JDK (Windows/Linux/macOS) to build JNI."
        )
        set(ZCHATIM_BUILD_JNI OFF)
    endif()

    if(ZCHATIM_BUILD_JNI)
        add_library(ZChatIMJNI SHARED
            jni/ZChatIMJNI.cpp
            jni/JniNatives.cpp
        )

        target_include_directories(ZChatIMJNI PRIVATE
            ${JNI_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(ZChatIMJNI PRIVATE ZChatIMCore)

        set_target_properties(ZChatIMJNI PROPERTIES
            OUTPUT_NAME "ZChatIMJNI"
        )

        # VS 多配置：避免输出到 lib/Release/，便于路径一致
        if(MSVC)
            foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
                set_target_properties(ZChatIMJNI PROPERTIES
                    LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/lib"
                    RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/lib"
                )
            endforeach()
        else()
            # Linux/macOS：统一输出到 build/lib，便于 java.library.path / zchat.native.dir
            set_target_properties(ZChatIMJNI PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            )
        endif()
    endif()
endif()

# =========================
# Console executable (optional)
# =========================
if(ZCHATIM_BUILD_EXE)
    set(_zchatim_exe_sources main.cpp)
    if(ZCHATIM_BUILD_TESTS)
        list(APPEND _zchatim_exe_sources
            tests/common_tools_test.cpp
            tests/mm1_managers_test.cpp
            tests/mm2_fifty_scenarios_test.cpp
            tests/jni_im_smoke_test.cpp
            tests/jni_local_register_rtc_test.cpp
            tests/test_im_1k_test.cpp
        )
    endif()
    add_executable(ZChatIM ${_zchatim_exe_sources})
    target_link_libraries(ZChatIM PRIVATE ZChatIMCore)
    target_include_directories(ZChatIM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(NOT ZCHATIM_BUILD_TESTS)
        target_compile_definitions(ZChatIM PRIVATE ZCHATIM_NO_INTREE_TESTS=1)
    endif()

    # Visual Studio / 多配置生成器：默认会把 exe 放到 bin/Release/，而启动项常查找 bin/ZChatIM.exe。
    # 统一输出到 ${CMAKE_BINARY_DIR}/bin（无 Debug/Release 子目录）。
    set(_zchatim_bin "${CMAKE_BINARY_DIR}/bin")
    set_target_properties(ZChatIM PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${_zchatim_bin}"
    )
    if(MSVC)
        foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            set_target_properties(ZChatIM PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${_zchatim_bin}"
            )
        endforeach()
    endif()
endif()

# 编译信息
message(STATUS "===============================================")
message(STATUS "ZChatIM Build Configuration")
message(STATUS "===============================================")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "MM2 crypto backend: ${ZCHATIM_MM2_CRYPTO_BACKEND}")
if(ZCHATIM_USE_SQLCIPHER)
    message(STATUS "Metadata SQLite: SQLCipher (thirdparty/sqlcipher amalgamation, no vcpkg)")
else()
    message(STATUS "Metadata SQLite: vanilla sqlite3.c amalgamation (plaintext)")
endif()
if(OPENSSL_VERSION)
    message(STATUS "OpenSSL: ${OPENSSL_VERSION}")
endif()
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "ZCHATIM_BUILD_MODE: ${ZCHATIM_BUILD_MODE}")
message(STATUS "Build JNI (ZChatIMJNI): ${ZCHATIM_BUILD_JNI}")
message(STATUS "Build EXE (ZChatIM): ${ZCHATIM_BUILD_EXE}")
message(STATUS "In-tree tests in EXE: ${ZCHATIM_BUILD_TESTS}")
if(ZCHATIM_BUILD_EXE)
    message(STATUS "ZChatIM.exe -> ${CMAKE_BINARY_DIR}/bin (flat, MSVC multi-config)")
endif()
if(ZCHATIM_BUILD_JNI)
    if(WIN32)
        set(_zchatim_jni_name "ZChatIMJNI.dll")
    elseif(APPLE)
        set(_zchatim_jni_name "libZChatIMJNI.dylib")
    else()
        set(_zchatim_jni_name "libZChatIMJNI.so")
    endif()
    message(STATUS "${_zchatim_jni_name} -> ${CMAKE_BINARY_DIR}/lib")
endif()
message(STATUS "===============================================")
