#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // 块索引
        // =============================================================
        
        class BlockIndex {
        public:
            // =============================================================
            // 构造函数/析构函数
            // =============================================================
            
            BlockIndex();
            ~BlockIndex();
            
            // =============================================================
            // 初始化
            // =============================================================
            
            // 初始化
            bool Initialize(const std::string& dbPath);
            
            // 清理
            void Cleanup();
            
            // =============================================================
            // 索引操作
            // =============================================================
            
            // 插入索引
            bool Insert(const DataBlockIndex& index);
            
            // 删除索引
            bool Remove(const std::string& blockId);
            
            // 更新索引
            bool Update(const DataBlockIndex& index);
            
            // =============================================================
            // 索引查询
            // =============================================================
            
            // 根据数据ID查找
            bool FindByDataId(const std::string& dataId, DataBlockIndex& outIndex);

            // 根据数据ID + chunkIndex 查找
            // 用于区分同一 dataId 下不同分片
            bool FindByDataIdAndChunkIndex(
                const std::string& dataId,
                uint32_t chunkIndex,
                DataBlockIndex& outIndex);
            
            // 根据文件ID查找
            std::vector<DataBlockIndex> FindByFileId(const std::string& fileId);
            
            // 根据块ID查找
            bool FindByBlockId(const std::string& blockId, DataBlockIndex& outIndex);

            // VerifyDataBlockSha256:
            // - 查询 (dataId, chunkIndex) 对应记录
            // - 将 SQLite 中记录的 sha256 与传入 sha256 比对
            // - outMatch: true 表示一致，false 表示不一致（实现层决定异常/标记失效策略）
            bool VerifyDataBlockSha256(
                const std::string& dataId,
                uint32_t chunkIndex,
                const uint8_t sha256[32],
                bool& outMatch);
            
            // =============================================================
            // 文件管理
            // =============================================================
            
            // 插入文件信息
            bool InsertFileInfo(const std::string& fileId, size_t totalSize, size_t usedSize);
            
            // 更新文件使用空间
            bool UpdateFileUsedSize(const std::string& fileId, size_t usedSize);
            
            // 删除文件信息
            bool RemoveFileInfo(const std::string& fileId);
            
            // 获取文件信息
            bool GetFileInfo(const std::string& fileId, size_t& totalSize, size_t& usedSize);
            
            // =============================================================
            // 统计
            // =============================================================
            
            // 获取总块数
            size_t GetTotalBlockCount();
            
            // 获取总文件数
            size_t GetTotalFileCount();
            
        private:
            // =============================================================
            // 内部方法
            // =============================================================
            
            // 创建表
            bool CreateTables();
            
            // 执行SQL
            bool ExecuteSql(const std::string& sql);
            
            // 开启事务
            bool BeginTransaction();
            
            // 提交事务
            bool CommitTransaction();
            
            // 回滚事务
            bool RollbackTransaction();
            
            // 格式化SQL
            std::string FormatSql(const std::string& format, ...);
            
            // =============================================================
            // 成员变量
            // =============================================================
            
            std::string m_dbPath;                  // 数据库路径
            void* m_db;                            // SQLite数据库句柄
            mutable std::mutex m_mutex;            // 互斥锁
            std::map<std::string, DataBlockIndex> m_indexCache; // 索引缓存
        };
        
    } // namespace mm2
} // namespace ZChatIM
