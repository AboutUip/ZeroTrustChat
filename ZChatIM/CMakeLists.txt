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
    src/mm1/MM1.cpp
    src/mm1/MM1_security_submodules.cpp
    src/mm1/managers/AuthSessionManager.cpp
    src/mm1/managers/SessionActivityManager.cpp
    src/mm2/crypto/Sha256.cpp
    src/mm2/storage/SqliteMetadataDb.cpp
    src/mm2/storage/StorageIntegrityManager.cpp
)
target_include_directories(ZChatIMCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ZChatIMCore PRIVATE sqlite3)
if(WIN32)
    # BCryptGenRandom：静态库使用者须解析该符号，故用 PUBLIC。
    target_link_libraries(ZChatIMCore PUBLIC bcrypt)
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
    add_executable(ZChatIM
        main.cpp
        tests/mm1_managers_test.cpp
    )
    target_link_libraries(ZChatIM PRIVATE ZChatIMCore)
    target_include_directories(ZChatIM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

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
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build JNI: ${ZCHATIM_BUILD_JNI}")
message(STATUS "Build EXE: ${ZCHATIM_BUILD_EXE}")
message(STATUS "ZChatIM.exe output: ${CMAKE_BINARY_DIR}/bin (flat, MSVC multi-config)")
message(STATUS "===============================================")
