# 仓库根入口：便于 Visual Studio「打开文件夹」在本目录发现 CMake 并列出目标。
# 使用 CMake 预设时，included 预设的源目录仍为 ZChatIM/（见根目录 CMakePresets.json）。
cmake_minimum_required(VERSION 3.20)
project(ZerOS-Chat)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ZChatIM/CMakeLists.txt")
    add_subdirectory(ZChatIM)
else()
    message(FATAL_ERROR "Missing ZChatIM/CMakeLists.txt")
endif()
