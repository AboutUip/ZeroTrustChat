#pragma once

#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Packed binary layouts at global scope (MSVC: avoids parse/IntelliSense issues
// when #pragma pack sits inside namespace ZChatIM).
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct ZChatIM_BlockHeadLayout {
    std::uint8_t magic[2];
    std::uint8_t version;
    std::uint8_t flags;
    std::uint32_t size;
    std::uint8_t idHash[8];
};

struct ZChatIM_ZdbHeaderLayout {
    char magic[4];
    std::uint8_t version;
    std::uint8_t reserved[7];
    std::uint8_t padding[4];
    std::uint64_t totalSize;
    std::uint64_t usedSize;
    std::uint8_t checksum[32];
};
#pragma pack(pop)

static_assert(sizeof(ZChatIM_BlockHeadLayout) == 16, "ZChatIM_BlockHeadLayout size");
static_assert(sizeof(ZChatIM_ZdbHeaderLayout) == 64, "ZChatIM_ZdbHeaderLayout size");

namespace ZChatIM
{
    using BlockHead = ZChatIM_BlockHeadLayout;
    using ZdbHeader = ZChatIM_ZdbHeaderLayout;

    // =============================================================
    // 基础类型长度定义
    // =============================================================

    constexpr size_t USER_ID_SIZE = 16; // 用户ID长度 (16字节)

    // JNI 认证会话句柄 callerSessionId：须与 Auth 返回的二进制句柄同长；所有需授权 API 首参（除 Auth/Verify/Init 等外）。
    constexpr size_t JNI_AUTH_SESSION_TOKEN_BYTES = USER_ID_SIZE;
    // ZSP 协议 Header 内 SessionID 字段长度（见 docs/01-Architecture/02-ZSP-Protocol.md）。
    constexpr size_t SESSION_ID_SIZE = 4;
    constexpr size_t MESSAGE_ID_SIZE   = 16; // 消息ID长度 (16字节)
    constexpr size_t NONCE_SIZE        = 12; // AES-GCM Nonce长度 (12字节)
    constexpr size_t AUTH_TAG_SIZE     = 16; // GCM认证标签长度 (16字节)
    constexpr size_t CRYPTO_KEY_SIZE   = 32; // 加密密钥长度 (32字节)
    constexpr size_t SHA256_SIZE       = 32; // SHA-256哈希长度 (32字节)
    constexpr size_t BLOCK_ID_HASH_SIZE = 8; // 消息块ID哈希长度 (8字节)

    // =============================================================
    // 魔术数定义
    // =============================================================

    constexpr uint8_t MAGIC_MB[2]   = {0x5A, 0x4D};              // 消息块魔术数 "ZM"
    constexpr char MAGIC_ZDB[4]     = {'Z', 'D', 'B', '\0'};     // .zdb文件魔术数

    // =============================================================
    // 消息内容 (明文)
    // =============================================================

    struct MessageContent {
        uint64_t              sequence;
        uint64_t              timestamp;
        uint8_t               prevHash[32];
        uint8_t               senderId[16];
        std::vector<uint8_t> payload;
    };

    // =============================================================
    // 消息块 (完整结构)
    // =============================================================

    struct MessageBlock {
        BlockHead             head;
        uint8_t               cryptoKey[32];
        uint8_t               nonce[12];
        std::vector<uint8_t> content;
        uint8_t               authTag[16];
    };

    // =============================================================
    // 数据块索引
    // =============================================================

    struct DataBlockIndex {
        std::string blockId;
        std::string dataId;
        uint32_t    chunkIndex;
        std::string fileId;
        uint64_t    offset;
        uint64_t    length;
        uint8_t     sha256[32];
    };

    // =============================================================
    // 错误码
    // =============================================================

    enum class ErrorCode {
        SUCCESS = 0,

        ERROR_UNKNOWN = 1000,
        ERROR_MEMORY  = 1001,
        ERROR_IO      = 1002,
        ERROR_PARAM   = 1003,

        ERROR_CRYPTO_INIT = 2000,
        ERROR_ENCRYPT     = 2001,
        ERROR_DECRYPT     = 2002,
        ERROR_AUTH        = 2003,

        ERROR_STORAGE_FULL     = 3000,
        ERROR_FILE_NOT_FOUND   = 3001,
        ERROR_BLOCK_NOT_FOUND  = 3002,
        ERROR_DATABASE         = 3003,

        ERROR_SESSION_NOT_FOUND = 4000,
        ERROR_MESSAGE_NOT_FOUND = 4001,
        ERROR_FILE_EXPIRED      = 4002,
    };

    // =============================================================
    // 常量定义
    // =============================================================

    constexpr size_t ZDB_FILE_SIZE       = 5 * 1024 * 1024;
    constexpr size_t ZDB_MIN_FILES       = 5;
    constexpr size_t ZDB_MAX_FILES       = 2000;
    constexpr size_t ZDB_MAX_WRITE_SIZE  = 500 * 1024;
    constexpr size_t SESSION_MESSAGE_LIMIT = 100;

    constexpr uint64_t MESSAGE_EXPIRE_DAYS = 7;
    constexpr uint64_t FILE_EXPIRE_DAYS    = 7;

    constexpr size_t FILE_CHUNK_SIZE = 65536;

} // namespace ZChatIM
