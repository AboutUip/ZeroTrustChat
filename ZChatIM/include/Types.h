#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Packed layouts at global scope (MSVC: #pragma pack outside namespace).
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

    constexpr size_t USER_ID_SIZE = 16;

    constexpr size_t JNI_AUTH_SESSION_TOKEN_BYTES = USER_ID_SIZE;
    constexpr size_t SESSION_ID_SIZE = 4;
    constexpr size_t MESSAGE_ID_SIZE   = 16;
    constexpr size_t NONCE_SIZE        = 12;
    constexpr size_t AUTH_TAG_SIZE     = 16;
    constexpr size_t CRYPTO_KEY_SIZE   = 32;
    constexpr size_t SHA256_SIZE       = 32;
    constexpr size_t BLOCK_ID_HASH_SIZE = 8;

    constexpr size_t AUTH_OPAQUE_CREDENTIAL_MIN_BYTES = 32;

    // mm1_user_kv; need MM2::Initialize.
    constexpr int32_t MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1 = 0x4C504831; // ASCII "LPH1"
    constexpr int32_t MM1_USER_KV_TYPE_LOCAL_RECOVERY_V1 = 0x4C524331; // ASCII "LRC1"
    /** mm1_user_kv：头像原图（JPEG/PNG/WebP 等）；经 UserDataManager→MM2::StoreMm1UserDataBlob 持久化；与 ZSP 单帧载荷上限对齐。 */
    constexpr int32_t MM1_USER_KV_TYPE_AVATAR_V1 = 0x41565431; // ASCII "AVT1"
    constexpr size_t  MM1_USER_AVATAR_MAX_BYTES   = 65535;
    /** mm1_user_kv：UTF-8 展示昵称；读权限与 AVT1 相同（本人或已接受好友）。 */
    constexpr int32_t MM1_USER_KV_TYPE_DISPLAY_NAME_V1 = 0x4E4D4E31; // ASCII "NMN1"
    constexpr size_t  MM1_USER_DISPLAY_NAME_MAX_BYTES = 256;
    constexpr size_t LOCAL_ACCOUNT_PASSWORD_MIN_UTF8_BYTES             = 8;
    constexpr size_t LOCAL_ACCOUNT_PASSWORD_MAX_UTF8_BYTES             = 512;
    constexpr int    LOCAL_ACCOUNT_PBKDF2_ITERATIONS                   = 200'000;

    constexpr int32_t MEDIA_CALL_KIND_AUDIO = 0;
    constexpr int32_t MEDIA_CALL_KIND_VIDEO = 1;

    constexpr uint8_t MAGIC_MB[2]   = {0x5A, 0x4D};
    constexpr char MAGIC_ZDB[4]     = {'Z', 'D', 'B', '\0'};

    struct MessageContent {
        uint64_t              sequence;
        uint64_t              timestamp;
        uint8_t               prevHash[32];
        uint8_t               senderId[16];
        std::vector<uint8_t> payload;
    };

    struct MessageBlock {
        BlockHead             head;
        uint8_t               cryptoKey[32];
        uint8_t               nonce[12];
        std::vector<uint8_t> content;
        uint8_t               authTag[16];
    };

    struct DataBlockIndex {
        std::string blockId;
        std::string dataId;
        uint32_t    chunkIndex;
        std::string fileId;
        uint64_t    offset;
        uint64_t    length;
        uint8_t     sha256[32];
    };

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

    constexpr size_t ZDB_FILE_SIZE       = 5 * 1024 * 1024;
    // Reserved; ZdbManager does not enforce (04-ZdbBinaryLayout 5).
    constexpr size_t ZDB_MIN_FILES       = 5;
    constexpr size_t ZDB_MAX_FILES       = 2000;
    constexpr size_t ZDB_MAX_WRITE_SIZE  = 500 * 1024;
    constexpr size_t SESSION_MESSAGE_LIMIT = 100;

    constexpr uint64_t MESSAGE_EXPIRE_DAYS = 7;
    constexpr uint64_t FILE_EXPIRE_DAYS    = 7;

    constexpr size_t FILE_CHUNK_SIZE = 65536;

} // namespace ZChatIM
