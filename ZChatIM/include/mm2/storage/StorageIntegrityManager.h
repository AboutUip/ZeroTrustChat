#pragma once

#include "../Types.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // 存储完整性校验管理器契约
        // - 写入 .zdb 后：记录 data_blocks.sha256 到 SQLite
        // - 读取 .zdb 后：计算 sha256 并与 SQLite 中记录比对
        // =============================================================
        class StorageIntegrityManager
        {
        public:
            // ComputeSha256:
            // - 输入：任意数据块明文/密文原始字节
            // - 输出：32 bytes sha256（outSha256[32]）
            bool ComputeSha256(const uint8_t* data, size_t length, uint8_t outSha256[32]);

            // RecordDataBlockHash:
            // - 在 data_blocks 中记录该 (dataId, chunkIndex) 对应的 sha256
            // - length 建议与写入 .zdb 的 length 一致
            bool RecordDataBlockHash(
                const std::string& dataId,
                uint32_t chunkIndex,
                const std::string& fileId,
                uint64_t offset,
                uint64_t length,
                const uint8_t sha256[32]);

            // VerifyDataBlockHash:
            // - 从 SQLite 取出 (dataId, chunkIndex) 的记录 sha256
            // - 与传入 sha256 比对
            bool VerifyDataBlockHash(
                const std::string& dataId,
                uint32_t chunkIndex,
                const uint8_t sha256[32],
                bool& outMatch);
        };
    } // namespace mm2
} // namespace ZChatIM

