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
        class BlockIndex {
        public:
            BlockIndex();
            ~BlockIndex();

            bool Initialize(const std::string& dbPath);

            void Cleanup();

            bool Insert(const DataBlockIndex& index);

            bool Remove(const std::string& blockId);

            bool Update(const DataBlockIndex& index);

            bool FindByDataId(const std::string& dataId, DataBlockIndex& outIndex);

            bool FindByDataIdAndChunkIndex(
                const std::string& dataId,
                uint32_t chunkIndex,
                DataBlockIndex& outIndex);

            std::vector<DataBlockIndex> FindByFileId(const std::string& fileId);

            bool FindByBlockId(const std::string& blockId, DataBlockIndex& outIndex);

            bool VerifyDataBlockSha256(
                const std::string& dataId,
                uint32_t chunkIndex,
                const uint8_t sha256[32],
                bool& outMatch);

            bool InsertFileInfo(const std::string& fileId, size_t totalSize, size_t usedSize);

            bool UpdateFileUsedSize(const std::string& fileId, size_t usedSize);

            bool RemoveFileInfo(const std::string& fileId);

            bool GetFileInfo(const std::string& fileId, size_t& totalSize, size_t& usedSize);

            size_t GetTotalBlockCount();

            size_t GetTotalFileCount();

        private:
            bool CreateTables();

            bool ExecuteSql(const std::string& sql);

            bool BeginTransaction();

            bool CommitTransaction();

            bool RollbackTransaction();

            std::string FormatSql(const std::string& format, ...);

            std::string m_dbPath;
            void* m_db;
            mutable std::mutex m_mutex;
            std::map<std::string, DataBlockIndex> m_indexCache;
        };

    } // namespace mm2
} // namespace ZChatIM
