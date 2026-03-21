#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <system_error>

namespace ZChatIM
{
    namespace common
    {
        // =============================================================
        // 文件工具类
        // =============================================================
        
        class File {
        public:
            // =============================================================
            // 文件检查
            // =============================================================
            
            // 检查文件是否存在
            static bool Exists(const std::string& filePath);
            
            // 检查是否为文件
            static bool IsFile(const std::string& path);
            
            // 检查是否为目录
            static bool IsDirectory(const std::string& path);
            
            // =============================================================
            // 文件操作
            // =============================================================
            
            // 获取文件大小
            static uint64_t GetSize(const std::string& filePath);
            
            // 获取文件修改时间
            static uint64_t GetLastModified(const std::string& filePath);
            
            // 删除文件
            static bool Delete(const std::string& filePath);
            
            // 重命名文件
            static bool Rename(const std::string& oldPath, const std::string& newPath);
            
            // 复制文件
            static bool Copy(const std::string& srcPath, const std::string& destPath);
            
            // =============================================================
            // 目录操作
            // =============================================================
            
            // 创建目录
            static bool CreateDirectory(const std::string& path);
            
            // 创建目录（递归）
            static bool CreateDirectoryRecursive(const std::string& path);
            
            // 删除目录
            static bool DeleteDirectory(const std::string& path);
            
            // 删除目录（递归）
            static bool DeleteDirectoryRecursive(const std::string& path);
            
            // 列出目录项（仅文件名）。**成功**时返回 **true** 且 **ec** 清零（**out 可为空**＝空目录）；**打开或遍历失败**时返回 **false** 且 **ec** 非零。
            static bool ListDirectory(const std::string& path, std::vector<std::string>& out, std::error_code& ec);
            
            // =============================================================
            // 路径操作
            // =============================================================
            
            // 获取文件名
            static std::string GetFileName(const std::string& path);
            
            // 获取文件名（不含扩展名）
            static std::string GetFileNameWithoutExtension(const std::string& path);
            
            // 获取文件扩展名
            static std::string GetFileExtension(const std::string& path);
            
            // 获取目录路径
            static std::string GetDirectoryPath(const std::string& path);
            
            // 拼接路径
            static std::string JoinPath(const std::string& path1, const std::string& path2);
            
            // 获取绝对路径
            static std::string GetAbsolutePath(const std::string& path);
            
            // =============================================================
            // 文件读写
            // =============================================================
            
            // 读取文件
            static bool ReadFile(const std::string& filePath, std::vector<uint8_t>& buffer);
            
            // 写入文件
            static bool WriteFile(const std::string& filePath, const uint8_t* data, size_t length);
            
            // 追加写入文件
            static bool AppendFile(const std::string& filePath, const uint8_t* data, size_t length);
            
        private:
            // 禁止实例化
            File() = delete;
            ~File() = delete;
        };
        
    } // namespace common
} // namespace ZChatIM
