// Storage integrity: SHA-256 over bytes + data_blocks row in SqliteMetadataDb (03-Storage.md 第五节).

#include "mm2/storage/StorageIntegrityManager.h"
#include "mm2/crypto/Sha256.h"
#include "mm2/storage/SqliteMetadataDb.h"
#include "Types.h"

#include <climits>
#include <cstring>

namespace ZChatIM::mm2 {

    bool StorageIntegrityManager::ComputeSha256(const uint8_t* data, size_t length, uint8_t outSha256[32])
    {
        lastError_.clear();
        if (outSha256 == nullptr) {
            lastError_ = "outSha256 is null";
            return false;
        }
        if (!crypto::Sha256(data, length, outSha256)) {
            lastError_ = "Sha256() failed";
            return false;
        }
        return true;
    }

    bool StorageIntegrityManager::RecordDataBlockHash(
        const std::vector<uint8_t>& dataId,
        uint32_t                    chunkIndex,
        const std::string&          fileId,
        uint64_t                    offset,
        uint64_t                    length,
        const uint8_t               sha256[32])
    {
        lastError_.clear();
        if (db_ == nullptr) {
            lastError_ = "SqliteMetadataDb not bound";
            return false;
        }
        if (dataId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            lastError_ = "dataId must be MESSAGE_ID_SIZE bytes";
            return false;
        }
        if (sha256 == nullptr) {
            lastError_ = "sha256 is null";
            return false;
        }
        if (fileId.empty()) {
            lastError_ = "file_id must be non-empty";
            return false;
        }
        if (chunkIndex > static_cast<uint32_t>(INT_MAX)) {
            lastError_ = "chunkIndex too large (must fit int32_t for sqlite bind)";
            return false;
        }
        if (!db_->UpsertDataBlock(dataId, static_cast<int32_t>(chunkIndex), fileId, offset, length, sha256)) {
            lastError_ = db_->LastError();
            return false;
        }
        return true;
    }

    bool StorageIntegrityManager::VerifyDataBlockHash(
        const std::vector<uint8_t>& dataId,
        uint32_t                    chunkIndex,
        const uint8_t               sha256[32],
        bool&                       outMatch)
    {
        lastError_.clear();
        outMatch = false;
        if (db_ == nullptr) {
            lastError_ = "SqliteMetadataDb not bound";
            return false;
        }
        if (dataId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            lastError_ = "dataId must be MESSAGE_ID_SIZE bytes";
            return false;
        }
        if (sha256 == nullptr) {
            lastError_ = "sha256 is null";
            return false;
        }
        if (chunkIndex > static_cast<uint32_t>(INT_MAX)) {
            lastError_ = "chunkIndex too large (must fit int32_t for sqlite bind)";
            return false;
        }
        std::string fileId;
        uint64_t    offset = 0;
        uint64_t    len    = 0;
        uint8_t     stored[ZChatIM::SHA256_SIZE]{};
        if (!db_->GetDataBlock(dataId, static_cast<int32_t>(chunkIndex), fileId, offset, len, stored)) {
            lastError_ = db_->LastError();
            return false;
        }
        outMatch = std::memcmp(stored, sha256, ZChatIM::SHA256_SIZE) == 0;
        lastError_.clear();
        return true;
    }

} // namespace ZChatIM::mm2
