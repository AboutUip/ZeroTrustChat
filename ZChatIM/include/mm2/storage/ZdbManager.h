#pragma once

#include "../Types.h"
#include "ZdbFile.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // ZDB文件管理器
        // =============================================================
        
        class ZdbManager {
        public:
            // =============================================================
            // 构造函数/析构函数
            // =============================================================
            
            ZdbManager();
            ~ZdbManager();
            
            // =============================================================
            // 初始化
            // =============================================================
            
            // 初始化
            bool Initialize(const std::string& dataDir);
            
            // 清理
            void Cleanup();
            
            // =============================================================
            // 文件管理
            // =============================================================
            
            // 创建新文件
            bool CreateFile(std::string& outFileId);
            
            // 打开文件
            bool OpenFile(const std::string& fileId);
            
            // 关闭文件
            bool CloseFile(const std::string& fileId);
            
            // 删除文件
            bool DeleteFile(const std::string& fileId);
            
            // =============================================================
            // 数据操作
            // =============================================================
            
            // 写入数据
            bool WriteData(const std::string& dataId, const uint8_t* data, size_t length, std::string& outFileId, uint64_t& outOffset);
            
            // 读取数据
            bool ReadData(const std::string& fileId, uint64_t offset, uint8_t* buffer, size_t length);
            
            // 删除数据
            bool DeleteData(const std::string& fileId, uint64_t offset, size_t length);
            
            // =============================================================
            // 空间管理
            // =============================================================
            
            // 分配空间
            bool AllocateSpace(size_t size, std::string& outFileId, uint64_t& outOffset);
            
            // 释放空间
            bool FreeSpace(const std::string& fileId, uint64_t offset, size_t size);
            
            // 获取总可用空间
            size_t GetTotalAvailableSpace() const;
            
            // 获取总已用空间
            size_t GetTotalUsedSpace() const;
            
            // 获取总空间
            size_t GetTotalSpace() const;
            
            // =============================================================
            // 文件信息
            // =============================================================
            
            // 获取文件列表
            std::vector<std::string> GetFileList() const;
            
            // 获取文件状态
            bool GetFileStatus(const std::string& fileId, size_t& usedSpace, size_t& availableSpace) const;
            
            // 检查是否需要创建新文件
            bool NeedCreateNewFile() const;
            
        private:
            // =============================================================
            // 内部方法
            // =============================================================
            
            // 选择文件（负载均衡）
            bool SelectFile(size_t size, std::string& outFileId);
            
            // 扫描现有文件
            bool ScanExistingFiles();
            
            // 生成文件名
            std::string GenerateFileName();
            
            // 构建文件路径
            std::string BuildFilePath(const std::string& fileId);
            
            // 检查目录
            bool CheckDirectory();
            
            // 清理过期文件
            bool CleanupExpiredFiles();
            
            // =============================================================
            // 成员变量
            // =============================================================
            
            std::string m_dataDir;                          // 数据目录
            mutable std::mutex m_mutex;                     // 互斥锁
            std::map<std::string, std::shared_ptr<ZdbFile>> m_files; // 文件映射
            std::vector<std::string> m_fileList;            // 文件列表
            size_t m_totalUsedSpace;                        // 总已用空间
            size_t m_totalAvailableSpace;                   // 总可用空间
        };
        
    } // namespace mm2
} // namespace ZChatIM
