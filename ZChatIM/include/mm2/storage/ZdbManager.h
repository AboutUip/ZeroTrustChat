#pragma once

#include "Types.h"
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
        // 并发：所有公开 API 由 `m_mutex` 串行化；底层 `ZdbFile` 另有递归锁。
        // `fileId` 须为单层文件名（无路径分隔符、无 ".."），见实现内 `IsSafeZdbFileId`。
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
            
            // 分配空间：在持锁内向 .zdb 写入 `size` 字节 0（从返回的 outOffset 起），避免仅“口头预留”的 TOCTOU
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

            std::string LastError() const { return m_lastError; }

        private:
            // =============================================================
            // 内部方法
            // =============================================================
            
            // 选择首个「可用空间 >= size」的文件（`m_fileList` 已排序，顺序稳定但非严格负载均衡）
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

            // 在已持有 m_mutex 的前提下创建新文件（避免 CreateFile 与 WriteData 死锁）
            bool CreateNewFileUnlocked(std::string& outFileId);

            // 已持有 m_mutex：释放句柄并清空映射（Initialize 用，避免与 Cleanup 重入锁死锁）
            void CleanupUnlocked();

            // =============================================================
            // 成员变量
            // =============================================================
            
            std::string m_dataDir;                          // 数据目录
            std::string m_lastError;
            mutable std::mutex m_mutex;                     // 互斥锁
            std::map<std::string, std::shared_ptr<ZdbFile>> m_files; // 文件映射
            std::vector<std::string> m_fileList;            // 文件列表
        };
        
    } // namespace mm2
} // namespace ZChatIM
