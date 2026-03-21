#pragma once

// SQLite metadata index (docs/02-Core/03-Storage.md): zdb_files, data_blocks, user_data, group_data, group_members,
// im_messages, im_message_reply, friend_requests, mm2_group_display, mm2_file_transfer（**`user_version=4`**）.
// Plain SQLite for now; spec mentions SQLCipher — see class comment in .cpp.
//
// Thread-safety: connection opened with **SQLITE_OPEN_FULLMUTEX** (SQLite serialized / thread-safe API use).
// **`MM2` 仍对外持锁**串行化编排；**勿**在其它线程绕过 MM2 直接并发调用同一实例。

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ZChatIM::mm2 {

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

        // Open or create database file. On Windows, prefer this overload so non-ASCII paths work
        // (std::filesystem::path::string() is ACP-encoded; SQLite expects UTF-8).
        bool Open(const std::filesystem::path& dbPath);

        // UTF-8 path bytes (ASCII-only paths are fine on all platforms).
        bool Open(const std::string& dbPath);
        void Close();
        bool IsOpen() const;

        // CREATE TABLE IF NOT EXISTS + PRAGMA foreign_keys=ON、迁移、`user_version`（当前 **4**：见 **`03-Storage.md`**）。
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

        bool DeleteGroupMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);

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

        // --- mm2_group_display（群显示名，与 **`group_data` 大块**独立）---
        bool UpsertGroupDisplayName(
            const std::vector<uint8_t>& groupId,
            const std::string&          nameUtf8,
            uint64_t                    updatedSeconds,
            const std::vector<uint8_t>& updatedBy);
        bool GetGroupDisplayName(const std::vector<uint8_t>& groupId, std::string& outNameUtf8);

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
