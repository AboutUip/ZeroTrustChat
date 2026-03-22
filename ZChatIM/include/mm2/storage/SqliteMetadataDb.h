#pragma once

// SQLite metadata index (docs/02-Core/03-Storage.md): zdb_files, data_blocks, user_data, group_data, group_members,
// im_messages, im_message_reply, friend_requests, mm2_group_display, mm2_file_transfer, **mm2_group_mute**（**`user_version=6`**）.
// **`ZCHATIM_USE_SQLCIPHER=1`**（默认，见 CMake）：**SQLCipher** 页级加密 + 固定 PRAGMA；密钥为 **`mm2_message_key.bin` 主密钥**经域分离 **SHA-256** 派生的 **32 字节 raw key**（见 **`DeriveMetadataSqlcipherKeyFromMessageMaster`**）。关闭宏时回退 **vanilla `sqlite3.c`**。
//
// Thread-safety: connection opened with **SQLITE_OPEN_FULLMUTEX** (SQLite serialized / thread-safe API use).
// **`MM2` 仍对外持锁**串行化编排；**勿**在其它线程绕过 MM2 直接并发调用同一实例。

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ZChatIM::mm2 {

#if defined(ZCHATIM_USE_SQLCIPHER)
    // 由 **`MM2::Initialize`** 在加载 **`mm2_message_key.bin`**（32B 主密钥）后调用；与消息 **AES-GCM** 密钥材料分离。
    bool DeriveMetadataSqlcipherKeyFromMessageMaster(
        const std::vector<uint8_t>& messageMasterKey32,
        std::vector<uint8_t>&       outSqlcipherKey32);
#endif

    // 03-Storage 第2.3节 type column (extend with new values as needed).
    enum class UserDataType : int32_t {
        Profile    = 0,
        FriendList = 1,
        Settings   = 2,
    };

    class SqliteMetadataDb {
    public:
        SqliteMetadataDb();
        ~SqliteMetadataDb();

        SqliteMetadataDb(SqliteMetadataDb&&) noexcept;
        SqliteMetadataDb& operator=(SqliteMetadataDb&&) noexcept;

        SqliteMetadataDb(const SqliteMetadataDb&)            = delete;
        SqliteMetadataDb& operator=(const SqliteMetadataDb&) = delete;

#if defined(ZCHATIM_USE_SQLCIPHER)
        // **`sqlcipherKey32`**：须恰为 **32** 字节 **raw** 密钥（由 **`DeriveMetadataSqlcipherKeyFromMessageMaster`** 从消息主密钥派生）。
        // 若磁盘上为**旧版明文** `SQLite format 3` 库，**首次**打开会自动迁移为 SQLCipher 文件（同路径旁可能短暂出现 **`.zchatim_sqlcipher_migrate.tmp`** / **`.pre_sqlcipher.bak`**）。
        bool Open(const std::filesystem::path& dbPath, const std::vector<uint8_t>& sqlcipherKey32);
        bool Open(const std::string& dbPathUtf8, const std::vector<uint8_t>& sqlcipherKey32);
#else
        // Open or create database file. On Windows, prefer this overload so non-ASCII paths work
        // (std::filesystem::path::string() is ACP-encoded; SQLite expects UTF-8).
        bool Open(const std::filesystem::path& dbPath);

        // UTF-8 path bytes (ASCII-only paths are fine on all platforms).
        bool Open(const std::string& dbPath);
#endif
        void Close();
        bool IsOpen() const;

        // CREATE TABLE IF NOT EXISTS + PRAGMA foreign_keys=ON、迁移、`user_version`（当前 **6**：见 **`03-Storage.md`**）。
        bool InitializeSchema();

        std::string LastError() const;

        // --- zdb_files --- (file_id must be non-empty UTF-8 / application-defined id)
        bool UpsertZdbFile(const std::string& fileId, uint64_t totalSize, uint64_t usedSize);
        bool GetZdbFile(const std::string& fileId, uint64_t& totalSize, uint64_t& usedSize);

        // --- data_blocks (composite PK data_id + chunk_idx) ---
        // data_id: exactly MESSAGE_ID_SIZE (16) bytes; chunk_idx >= 0; file_id non-empty; sha256 non-null (same rules as UpsertDataBlock).
        bool InsertDataBlock(
            const std::vector<uint8_t>& dataId,
            int32_t                     chunkIdx,
            const std::string&          fileId,
            uint64_t                    offset,
            uint64_t                    length,
            const uint8_t               sha256[32]);

        bool DataBlockExists(const std::vector<uint8_t>& dataId, int32_t chunkIdx);

        // Same shape as InsertDataBlock; INSERT ... ON CONFLICT DO UPDATE (re-record hash / location).
        bool UpsertDataBlock(
            const std::vector<uint8_t>& dataId,
            int32_t                     chunkIdx,
            const std::string&          fileId,
            uint64_t                    offset,
            uint64_t                    length,
            const uint8_t               sha256[32]);

        bool GetDataBlock(
            const std::vector<uint8_t>& dataId,
            int32_t                     chunkIdx,
            std::string&                fileIdOut,
            uint64_t&                   offsetOut,
            uint64_t&                   lengthOut,
            uint8_t                     sha256Out[32]);

        // --- user_data (PK user_id + type); user_id must be USER_ID_SIZE (16) bytes ---
        bool UpsertUserData(
            const std::vector<uint8_t>& userId,
            const std::string&          fileId,
            uint64_t                    offset,
            uint64_t                    length,
            const uint8_t               sha256[32],
            int32_t                     type);

        bool GetUserData(
            const std::vector<uint8_t>& userId,
            int32_t                     type,
            std::string&                fileId,
            uint64_t&                   offset,
            uint64_t&                   length,
            uint8_t                     sha256Out[32]);

        // --- mm1_user_kv（MM1/JNI 小型 BLOB；PK user_id + type）---
        // 与 **`user_data`（ZDB 指针 + SHA）** 无关；单条 **`data`** 上限 **16 MiB**（实现内校验）。
        bool UpsertMm1UserKvBlob(const std::vector<uint8_t>& userId, int32_t type, const std::vector<uint8_t>& data);
        // 成功且 **无行** 时 **`outData` 为空**；失败返回 **false**（见 **`LastError()`**）。
        bool GetMm1UserKvBlob(const std::vector<uint8_t>& userId, int32_t type, std::vector<uint8_t>& outData);
        // 删除到行则 **true**；无匹配行则 **false**（**非**错误）。
        bool DeleteMm1UserKvBlob(const std::vector<uint8_t>& userId, int32_t type);

        // --- group_data (PK group_id); group_id must be USER_ID_SIZE (16) bytes ---
        bool UpsertGroupData(
            const std::vector<uint8_t>& groupId,
            const std::string&          fileId,
            uint64_t                    offset,
            uint64_t                    length,
            const uint8_t               sha256[32]);

        bool GetGroupData(
            const std::vector<uint8_t>& groupId,
            std::string&                fileId,
            uint64_t&                   offset,
            uint64_t&                   length,
            uint8_t                     sha256Out[32]);

        // --- group_members (PK group_id + user_id); both ids must be USER_ID_SIZE bytes ---
        bool UpsertGroupMember(
            const std::vector<uint8_t>& groupId,
            const std::vector<uint8_t>& userId,
            int32_t                     role,
            int64_t                     joinedAt);

        bool GetGroupMember(
            const std::vector<uint8_t>& groupId,
            const std::vector<uint8_t>& userId,
            int32_t&                    roleOut,
            int64_t&                    joinedAtOut);

        // 是否存在 `group_members` 行。`return true` 时 **`outExists`** 已定义；`false` 表示打开/SQL 等**硬错误**（查 **`LastError()`**）。
        bool GetGroupMemberRowExists(
            const std::vector<uint8_t>& groupId,
            const std::vector<uint8_t>& userId,
            bool&                       outExists);

        bool DeleteGroupMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);

        // `group_id` 下所有 `user_id`（**ORDER BY joined_at, user_id**）。
        bool ListGroupMemberUserIds(
            const std::vector<uint8_t>& groupId,
            std::vector<std::vector<uint8_t>>& outUserIds);

        // --- im_messages (MM2 encrypted message index; session_id / message_id = USER_ID_SIZE / MESSAGE_ID_SIZE BLOBs) ---
        // **`read_at_ms`**：SQLite INTEGER，**NULL**=未读；非 NULL=已读（Unix 毫秒）。schema **v3**。
        bool InsertImMessage(const std::vector<uint8_t>& sessionId, const std::vector<uint8_t>& messageId);
        // 将 **`read_at_ms`** 从 NULL 设为 **`readAtMs`**（**首次已读**）；已读则 **true** 且不覆盖时间戳。
        bool MarkImMessageRead(const std::vector<uint8_t>& messageId, int64_t readAtMs);
        // 会话内 **`read_at_ms IS NULL`** 的 `message_id`，按 **`rowid ASC`**（最早未读在前），最多 **`limit`** 条。
        bool ListUnreadImMessageIdsForSession(
            const std::vector<uint8_t>& sessionId,
            size_t                      limit,
            std::vector<std::vector<uint8_t>>& outMessageIds);
        // 列出某会话下的 message_id：按 **`rowid`** 取 **最近** `limit` 条，再转为 **插入先后正序**（先插入在前；**非**墙钟时间）。
        // `limit==0` 时成功并清空 `outMessageIds`。
        bool ListImMessageIdsForSession(
            const std::vector<uint8_t>& sessionId,
            size_t                      limit,
            std::vector<std::vector<uint8_t>>& outMessageIds);
        // 按 `rowid` **升序**取前 `limit` 条 `message_id`（会话内最早 → 更晚）。
        bool ListImMessageIdsForSessionChronological(
            const std::vector<uint8_t>& sessionId,
            size_t                      limit,
            std::vector<std::vector<uint8_t>>& outMessageIds);
        // 取 `rowid` **大于** `afterMessageId` 对应行的最多 `limit` 条（升序）。`afterMessageId` 须为 **`MESSAGE_ID_SIZE`**；若该 id 不在该 `sessionId` 下则结果为空。
        bool ListImMessageIdsForSessionAfterMessageId(
            const std::vector<uint8_t>& sessionId,
            const std::vector<uint8_t>& afterMessageId,
            size_t                      limit,
            std::vector<std::vector<uint8_t>>& outMessageIds);
        bool GetImMessageSession(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& sessionIdOut);
        bool ImMessageExists(const std::vector<uint8_t>& messageId);
        bool DeleteImMessage(const std::vector<uint8_t>& messageId);

        bool DeleteDataBlock(const std::vector<uint8_t>& dataId, int32_t chunkIdx);

        // MM2 `DeleteMessage`：在 **`.zdb` 已清零之后**调用，**`BEGIN IMMEDIATE`** 内原子删除
        // **`data_blocks`（可选 chunk 0）+ `im_messages`**，避免仅删一行导致的索引半状态。
        bool DeleteMessageMetadataTransaction(const std::vector<uint8_t>& messageId, bool deleteDataBlockChunk0);

        // --- im_message_reply（回复关系；**`message_id`** 为「本条回复消息」）---
        bool UpsertImMessageReply(
            const std::vector<uint8_t>& messageId,
            const std::vector<uint8_t>& repliedMsgId,
            const std::vector<uint8_t>& repliedSenderId,
            const std::vector<uint8_t>& repliedDigest);
        bool GetImMessageReply(
            const std::vector<uint8_t>& messageId,
            std::vector<uint8_t>& outRepliedMsgId,
            std::vector<uint8_t>& outRepliedSenderId,
            std::vector<uint8_t>& outRepliedDigest);

        // --- im_messages 编辑列（**v4**）---
        bool UpdateImMessageEditState(
            const std::vector<uint8_t>& messageId,
            uint32_t                    editCount,
            uint64_t                    lastEditTimeSeconds);
        bool GetImMessageEditState(
            const std::vector<uint8_t>& messageId,
            uint32_t& outEditCount,
            uint64_t& outLastEditTimeSeconds);

        size_t CountImMessages();
        bool ListAllImMessageIdsForSession(
            const std::vector<uint8_t>& sessionId,
            std::vector<std::vector<uint8_t>>& outMessageIds);

        // --- friend_requests（MM2 本地记录；签名为 BLOB）---
        bool InsertFriendRequest(
            const std::vector<uint8_t>& requestId,
            const std::vector<uint8_t>& fromUserId,
            const std::vector<uint8_t>& toUserId,
            uint64_t                    createdSeconds,
            const std::vector<uint8_t>& signatureEd25519);
        bool UpdateFriendRequestStatus(
            const std::vector<uint8_t>& requestId,
            int32_t                     status,
            const std::vector<uint8_t>& responderId,
            uint64_t                    updatedSeconds);
        bool DeleteFriendRequest(const std::vector<uint8_t>& requestId);
        bool DeleteExpiredPendingFriendRequests(uint64_t nowSeconds, uint64_t ttlSeconds);

        // 读 pending/accepted/rejected 行（用于 MM1 响应前校验 to_user）。
        bool GetFriendRequestRow(
            const std::vector<uint8_t>& requestId,
            std::vector<uint8_t>&       outFromUser,
            std::vector<uint8_t>&       outToUser,
            int32_t&                    outStatus);
        // status==1 的边：返回与 userId 互为好友的对端 user_id（去重）。
        bool ListAcceptedFriendPeerUserIds(
            const std::vector<uint8_t>& userId,
            std::vector<std::vector<uint8_t>>& outPeers);
        // 删除 status==1 且端点为 (userA,userB) 的所有行（双向）。
        bool DeleteAcceptedFriendshipEdgesBetween(
            const std::vector<uint8_t>& userA,
            const std::vector<uint8_t>& userB);

        // --- mm2_group_display（群显示名，与 **`group_data` 大块**独立）---
        bool UpsertGroupDisplayName(
            const std::vector<uint8_t>& groupId,
            const std::string&          nameUtf8,
            uint64_t                    updatedSeconds,
            const std::vector<uint8_t>& updatedBy);
        bool GetGroupDisplayName(const std::vector<uint8_t>& groupId, std::string& outNameUtf8);
        bool DeleteGroupDisplayName(const std::vector<uint8_t>& groupId);

        // --- mm2_group_mute（**MM1 `GroupMuteManager`**；PK group_id + user_id）---
        // **`duration_s`**：**`-1`**=永久；**`>=0`** 时结束时刻为 **`start_ms + duration_s*1000`**（毫秒）。
        bool UpsertGroupMute(
            const std::vector<uint8_t>& groupId,
            const std::vector<uint8_t>& userId,
            int64_t                     startMs,
            int64_t                     durationSeconds,
            const std::vector<uint8_t>& mutedBy,
            const std::vector<uint8_t>& reason);
        bool DeleteGroupMute(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);
        // 无行时 **`outExists=false`** 且返回 **true**；仅 SQL/参数错误时 **false**。
        bool GetGroupMuteRow(
            const std::vector<uint8_t>& groupId,
            const std::vector<uint8_t>& userId,
            bool&                       outExists,
            int64_t&                    outStartMs,
            int64_t&                    outDurationS,
            std::vector<uint8_t>&       outMutedBy,
            std::vector<uint8_t>&       outReason);
        bool DeleteExpiredGroupMutes(int64_t nowMs);

        // --- mm2_file_transfer（逻辑 fileId 字符串；续传/完成态）---
        // status：0=in progress，1=completed，2=cancelled
        bool UpsertFileTransferResume(const std::string& logicalFileId, uint32_t resumeChunk);
        bool GetFileTransferResumeChunk(const std::string& logicalFileId, uint32_t& outResumeChunk);
        bool GetFileTransferStatus(const std::string& logicalFileId, int32_t& outStatus);
        bool SetFileTransferComplete(const std::string& logicalFileId, const uint8_t sha256[32]);
        bool SetFileTransferCancelled(const std::string& logicalFileId);
        bool DeleteFileTransferMeta(const std::string& logicalFileId);

        bool RunVacuum();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace ZChatIM::mm2
