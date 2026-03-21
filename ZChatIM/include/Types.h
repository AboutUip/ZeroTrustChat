#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace ZChatIM
{
    // =============================================================
    // 基础类型长度定义
    // =============================================================
    
    constexpr size_t USER_ID_SIZE = 16;       // 用户ID长度 (16字节)

    // JNI 认证会话句柄 callerSessionId：须与 Auth 返回的二进制句柄同长；所有需授权 API 首参（除 Auth/Verify/Init 等外）。
    constexpr size_t JNI_AUTH_SESSION_TOKEN_BYTES = USER_ID_SIZE;
    // ZSP 协议 Header 内 SessionID 字段长度（见 docs/01-Architecture/02-ZSP-Protocol.md）。
    // 与 JniInterface::RegisterDeviceSession 等 API 中 16 字节 vector 会话/设备标识无包含关系。
    constexpr size_t SESSION_ID_SIZE = 4;
    constexpr size_t MESSAGE_ID_SIZE = 16;    // 消息ID长度 (16字节)
    constexpr size_t NONCE_SIZE = 12;         // AES-GCM Nonce长度 (12字节)
    constexpr size_t AUTH_TAG_SIZE = 16;      // GCM认证标签长度 (16字节)
    constexpr size_t CRYPTO_KEY_SIZE = 32;    // 加密密钥长度 (32字节)
    constexpr size_t SHA256_SIZE = 32;        // SHA-256哈希长度 (32字节)
    constexpr size_t BLOCK_ID_HASH_SIZE = 8;   // 消息块ID哈希长度 (8字节)
    
    // =============================================================
    // 魔术数定义
    // =============================================================
    
    constexpr uint8_t MAGIC_MB[2] = { 0x5A, 0x4D };  // 消息块魔术数 "ZM"
    constexpr char MAGIC_ZDB[4] = { 'Z', 'D', 'B', '\0' }; // .zdb文件魔术数
    
    // =============================================================
    // 消息块结构
    // =============================================================
    
    // 消息块头部
    struct BlockHead {
        uint8_t     magic[2];      // 魔术数 0x5A 0x4D
        uint8_t     version;       // 版本号
        uint8_t     flags;         // 标志位
        uint32_t    size;          // 消息块总大小
        uint8_t     idHash[8];     // 消息ID哈希
    };
    
    static_assert(sizeof(BlockHead) == 16, "BlockHead size must be 16 bytes");
    
    // 消息内容 (明文)
    struct MessageContent {
        uint64_t    sequence;      // 序列号
        uint64_t    timestamp;     // 时间戳
        uint8_t     prevHash[32];  // 前一条消息哈希
        uint8_t     senderId[16];  // 发送者ID
        std::vector<uint8_t> payload; // 消息内容
    };
    
    // 消息块 (完整结构)
    struct MessageBlock {
        BlockHead   head;           // 头部
        uint8_t     cryptoKey[32];  // 消息加密密钥
        uint8_t     nonce[12];      // AES-GCM Nonce
        std::vector<uint8_t> content; // 加密后的内容
        uint8_t     authTag[16];    // GCM认证标签
    };
    
    // =============================================================
    // 存储相关结构
    // =============================================================
    
    // .zdb文件头部
    struct ZdbHeader {
        char        magic[4];       // 魔术数 "ZDB\0"
        uint8_t     version;        // 版本号
        uint8_t     reserved[7];    // 保留字段
        uint64_t    totalSize;      // 文件总大小 (5MB)
        uint64_t    usedSize;       // 已用空间
        uint8_t     checksum[32];   // 文件校验和
    };
    
    static_assert(sizeof(ZdbHeader) == 64, "ZdbHeader size must be 64 bytes");
    
    // 数据块索引
    struct DataBlockIndex {
        std::string blockId;        // 块ID
        std::string dataId;         // 数据ID (消息ID或文件ID)
        uint32_t    chunkIndex;     // 分片索引
        std::string fileId;         // 文件ID
        uint64_t    offset;         // 偏移量
        uint64_t    length;         // 长度
        uint8_t     sha256[32];     // 数据哈希
    };
    
    // =============================================================
    // 错误码
    // =============================================================
    
    enum class ErrorCode {
        SUCCESS = 0,                // 成功
        
        // 通用错误
        ERROR_UNKNOWN = 1000,       // 未知错误
        ERROR_MEMORY = 1001,        // 内存错误
        ERROR_IO = 1002,            // IO错误
        ERROR_PARAM = 1003,         // 参数错误
        
        // 加密错误
        ERROR_CRYPTO_INIT = 2000,   // 加密初始化失败
        ERROR_ENCRYPT = 2001,       // 加密失败
        ERROR_DECRYPT = 2002,       // 解密失败
        ERROR_AUTH = 2003,          // 认证失败
        
        // 存储错误
        ERROR_STORAGE_FULL = 3000,  // 存储已满
        ERROR_FILE_NOT_FOUND = 3001, // 文件未找到
        ERROR_BLOCK_NOT_FOUND = 3002, // 块未找到
        ERROR_DATABASE = 3003,      // 数据库错误
        
        // 业务错误
        ERROR_SESSION_NOT_FOUND = 4000, // 会话未找到
        ERROR_MESSAGE_NOT_FOUND = 4001, // 消息未找到
        ERROR_FILE_EXPIRED = 4002,  // 文件过期
    };
    
    // =============================================================
    // 常量定义
    // =============================================================
    
    // ZDB文件配置
    constexpr size_t ZDB_FILE_SIZE = 5 * 1024 * 1024; // 5MB
    constexpr size_t ZDB_MIN_FILES = 5;              // 最小文件数
    constexpr size_t ZDB_MAX_FILES = 2000;           // 最大文件数
    constexpr size_t ZDB_MAX_WRITE_SIZE = 500 * 1024; // 单次最大写入 500KB
    
    // 缓存配置
    constexpr size_t SESSION_MESSAGE_LIMIT = 100;     // 每会话消息上限
    
    // 过期时间
    constexpr uint64_t MESSAGE_EXPIRE_DAYS = 7;      // 消息过期天数
    constexpr uint64_t FILE_EXPIRE_DAYS = 7;         // 文件过期天数
    
    // 文件传输
    constexpr size_t FILE_CHUNK_SIZE = 65536;        // 文件分片大小 64KB
    
} // namespace ZChatIM
