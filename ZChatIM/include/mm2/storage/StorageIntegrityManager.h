#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ZChatIM::mm2 {

    class SqliteMetadataDb;

    // .zdb bytes vs data_blocks.sha256; Bind after metadata Open (03-Storage 5).
    class StorageIntegrityManager {
    public:
        StorageIntegrityManager() = default;

        void Bind(SqliteMetadataDb* db) noexcept { db_ = db; }

        // length > 0 requires non-null data. outSha256 must be non-null.
        bool ComputeSha256(const uint8_t* data, size_t length, uint8_t outSha256[32]);

        // dataId must be MESSAGE_ID_SIZE (16) bytes (same as data_blocks.data_id).
        // chunkIndex must be <= INT_MAX so it maps safely to sqlite chunk_idx.
        bool RecordDataBlockHash(
            const std::vector<uint8_t>& dataId,
            uint32_t                    chunkIndex,
            const std::string&          fileId,
            uint64_t                    offset,
            uint64_t                    length,
            const uint8_t               sha256[32]);

        // Loads stored row; sets outMatch if row exists and stored sha256 equals passed digest.
        // Returns false if db not bound, bad dataId size, or row missing / DB error.
        bool VerifyDataBlockHash(
            const std::vector<uint8_t>& dataId,
            uint32_t                    chunkIndex,
            const uint8_t               sha256[32],
            bool&                       outMatch);

        std::string LastError() const { return lastError_; }

    private:
        SqliteMetadataDb* db_       = nullptr;
        std::string       lastError_;
    };

} // namespace ZChatIM::mm2
