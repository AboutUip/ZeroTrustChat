// BlockIndex 桩实现：**MM2 热路径不经过此类**（`data_blocks` / `zdb_files` 在 **SqliteMetadataDb**）。
// 保留本编译单元供历史 API / 未来独立索引实验；**`MM2` 类不再嵌入 `BlockIndex` 成员**。

#include "mm2/storage/BlockIndex.h"

namespace ZChatIM::mm2 {

    BlockIndex::BlockIndex()
        : m_db(nullptr)
    {
    }

    BlockIndex::~BlockIndex()
    {
        Cleanup();
    }

    bool BlockIndex::Initialize(const std::string& /*dbPath*/)
    {
        return false;
    }

    void BlockIndex::Cleanup()
    {
        m_indexCache.clear();
        m_dbPath.clear();
        m_db = nullptr;
    }

    bool BlockIndex::Insert(const DataBlockIndex&)
    {
        return false;
    }

    bool BlockIndex::Remove(const std::string&)
    {
        return false;
    }

    bool BlockIndex::Update(const DataBlockIndex&)
    {
        return false;
    }

    bool BlockIndex::FindByDataId(const std::string&, DataBlockIndex&)
    {
        return false;
    }

    bool BlockIndex::FindByDataIdAndChunkIndex(const std::string&, uint32_t, DataBlockIndex&)
    {
        return false;
    }

    std::vector<DataBlockIndex> BlockIndex::FindByFileId(const std::string&)
    {
        return {};
    }

    bool BlockIndex::FindByBlockId(const std::string&, DataBlockIndex&)
    {
        return false;
    }

    bool BlockIndex::VerifyDataBlockSha256(const std::string&, uint32_t, const uint8_t /*sha256*/[32], bool& outMatch)
    {
        outMatch = false;
        return false;
    }

    bool BlockIndex::InsertFileInfo(const std::string&, size_t, size_t)
    {
        return false;
    }

    bool BlockIndex::UpdateFileUsedSize(const std::string&, size_t)
    {
        return false;
    }

    bool BlockIndex::RemoveFileInfo(const std::string&)
    {
        return false;
    }

    bool BlockIndex::GetFileInfo(const std::string&, size_t&, size_t&)
    {
        return false;
    }

    size_t BlockIndex::GetTotalBlockCount()
    {
        return 0;
    }

    size_t BlockIndex::GetTotalFileCount()
    {
        return 0;
    }

    bool BlockIndex::CreateTables()
    {
        return false;
    }

    bool BlockIndex::ExecuteSql(const std::string&)
    {
        return false;
    }

    bool BlockIndex::BeginTransaction()
    {
        return false;
    }

    bool BlockIndex::CommitTransaction()
    {
        return false;
    }

    bool BlockIndex::RollbackTransaction()
    {
        return false;
    }

    std::string BlockIndex::FormatSql(const std::string& format, ...)
    {
        return format;
    }

} // namespace ZChatIM::mm2
