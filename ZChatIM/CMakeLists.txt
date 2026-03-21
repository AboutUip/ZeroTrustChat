# =============================================================
# ZChatIM - 高安全即时通讯系统 C++ 安全模块
# =============================================================

cmake_minimum_required(VERSION 3.20)

# 项目信息（含 C：thirdparty/sqlite/sqlite3.c 为 C 源文件）
project(ZChatIM VERSION 1.0.0 LANGUAGES CXX C)

# =========================
# Options
# =========================
option(ZCHATIM_BUILD_JNI "Build JNI shared library" ON)
option(ZCHATIM_BUILD_EXE "Build console executable" ON)
# Default OFF: 仓库默认不提交 tests/，发行/提交构建仅 main.cpp；--test* 运行会提示未编入测试。
# 本地自测：cmake -DZCHATIM_BUILD_TESTS=ON（须存在 ZChatIM/tests/*.cpp）。
option(ZCHATIM_BUILD_TESTS "Compile in-tree tests into ZChatIM.exe" OFF)

# 密码学后端（与 Crypto.cpp 一致）：
# - Windows：BCrypt + CryptoAPI（AES-GCM / PBKDF2 / RNG；Auth 会话随机）— **不需要** OpenSSL。
# - Linux / macOS：OpenSSL 3 libcrypto（同上算法；与 Windows **磁盘格式字节兼容**）。
if(WIN32)
    set(ZCHATIM_MM2_CRYPTO_BACKEND "BCrypt")
else()
    set(ZCHATIM_MM2_CRYPTO_BACKEND "OpenSSL3")
    if(NOT OPENSSL_ROOT_DIR AND DEFINED ENV{OPENSSL_ROOT_DIR})
        file(TO_CMAKE_PATH "$ENV{OPENSSL_ROOT_DIR}" _zchatim_openssl_root)
        set(OPENSSL_ROOT_DIR "${_zchatim_openssl_root}" CACHE PATH "OpenSSL installation root (override if FindOpenSSL fails)")
    endif()
    if(NOT OPENSSL_ROOT_DIR)
        set(_zchatim_openssl_reserved "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openssl/versions/current")
        if(EXISTS "${_zchatim_openssl_reserved}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_zchatim_openssl_reserved}" CACHE PATH "OpenSSL root (thirdparty/openssl/versions/current)")
            message(STATUS "OPENSSL_ROOT_DIR (auto from reserved folder): ${_zchatim_openssl_reserved}")
        endif()
    endif()
    find_package(OpenSSL 3.0 QUIET)
    if(NOT TARGET OpenSSL::Crypto)
        message(FATAL_ERROR
            "Could not find OpenSSL 3.0 or newer (Linux/macOS MM2 crypto + Auth RNG).\n"
            "\n"
            "Fix:\n"
            "  Linux: sudo apt install libssl-dev (or distro equivalent)\n"
            "  macOS: brew install openssl@3 && export OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)\n"
            "  Or set OPENSSL_ROOT_DIR to a tree with include/openssl/ssl.h and libcrypto.\n"
            "\n"
            "Windows builds use BCrypt and do not need this step.\n"
            "See docs/07-Engineering/01-Build-ZChatIM.md\n"
        )
    endif()
endif()

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
# SQLite amalgamation (metadata index, docs/02-Core/03-Storage.md)
# =========================
add_library(sqlite3 STATIC
    thirdparty/sqlite/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite
)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /wd4244 /wd4267 /wd4018 /wd4310)
endif()

# =========================
# 核心静态库（MM1 管理等实现）
# =========================
add_library(ZChatIMCore STATIC
    src/common/Utils.cpp
    src/common/Memory.cpp
    src/common/String.cpp
    src/common/File.cpp
    src/common/Time.cpp
    src/common/Random.cpp
    src/common/Logger.cpp
    src/mm1/MM1.cpp
    src/mm1/MM1_security_submodules.cpp
    src/mm1/managers/AuthSessionManager.cpp
    src/mm1/managers/SessionActivityManager.cpp
    src/mm2/MM2.cpp
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
if(WIN32)
    target_link_libraries(ZChatIMCore PUBLIC bcrypt advapi32 crypt32 PRIVATE sqlite3)
else()
    target_link_libraries(ZChatIMCore PUBLIC OpenSSL::Crypto PRIVATE sqlite3)
endif()

# =========================
# JNI shared library (optional)
# =========================
if(ZCHATIM_BUILD_JNI)
    # CMake 尝试查找 JNI。找不到时从 JAVA_HOME 兜底。
    find_package(JNI QUIET)
    if(NOT JNI_FOUND)
        if(DEFINED ENV{JAVA_HOME})
            set(JAVA_HOME $ENV{JAVA_HOME})
            set(JNI_INCLUDE_DIRS
                "${JAVA_HOME}/include"
                "${JAVA_HOME}/include/win32"
            )
        else()
            message(WARNING
                "JNI not found (and JAVA_HOME is not set). ZChatIMJNI will be skipped. "
                "Please install a JDK and/or set JAVA_HOME (e.g. $env:JAVA_HOME='C:\\Program Files\\Java\\jdk-xx') to build JNI."
            )
            set(ZCHATIM_BUILD_JNI OFF)
        endif()
    endif()

    if(ZCHATIM_BUILD_JNI)
        add_library(ZChatIMJNI SHARED
            jni/ZChatIMJNI.cpp
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
if(NOT WIN32)
    message(STATUS "OpenSSL: ${OPENSSL_VERSION}")
endif()
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build JNI: ${ZCHATIM_BUILD_JNI}")
message(STATUS "Build EXE: ${ZCHATIM_BUILD_EXE}")
message(STATUS "In-tree tests in EXE: ${ZCHATIM_BUILD_TESTS}")
message(STATUS "ZChatIM.exe output: ${CMAKE_BINARY_DIR}/bin (flat, MSVC multi-config)")
message(STATUS "===============================================")
