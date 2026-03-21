# =============================================================
# ZChatIM - 高安全即时通讯系统 C++ 安全模块
# =============================================================

cmake_minimum_required(VERSION 3.20)

# 项目信息
project(ZChatIM VERSION 1.0.0 LANGUAGES CXX)

# =========================
# Options
# =========================
option(ZCHATIM_BUILD_JNI "Build JNI shared library" ON)
option(ZCHATIM_BUILD_EXE "Build console executable" ON)

# C++ 标准配置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 头文件包含路径（包含头文件骨架）
include_directories(include)

# =========================
# 核心静态库（MM1 管理等实现）
# =========================
add_library(ZChatIMCore STATIC
    src/mm1/managers/AuthSessionManager.cpp
    src/mm1/managers/SessionActivityManager.cpp
)
target_include_directories(ZChatIMCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
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

        # Windows 生成 .dll。用于 JNI_OnLoad 等入口。
        set_target_properties(ZChatIMJNI PROPERTIES
            OUTPUT_NAME "ZChatIMJNI"
        )
        endif()
endif()

# =========================
# Console executable (optional)
# =========================
if(ZCHATIM_BUILD_EXE)
    add_executable(ZChatIM main.cpp)
    target_link_libraries(ZChatIM PRIVATE ZChatIMCore)
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
message(STATUS "===============================================")
