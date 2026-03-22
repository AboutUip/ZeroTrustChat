#pragma once

// cSpell:words Upsert upsert
// (SQL/SQLite insert-or-replace terminology; appears in method names.)

// Metadata DB: 03-Storage. user_version=11; no IM tables (IM in MM2 RAM).
// SQLCipher: key from mm2_message_key.bin via DeriveMetadataSqlcipherKeyFromMessageMaster; else vanilla sqlite3.
// FULLMUTEX; serialize via MM2, do not bypass.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ZChatIM::mm2 {

#if defined(ZCHATIM_USE_SQLCIPHER)
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
        // 32B raw key; plain DB auto-migrates to SQLCipher (tmp/bak beside path).
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

        // CREATE TABLE IF NOT EXISTS + PRAGMA foreign_keys=ON、**`user_version=11`**（**无** **`im_messages` / `im_message_reply`**）。
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

        // **`group_id`** 在 **`group_members`** 中是否存在**至少一行**（用于将 **16B imSessionId** 视为群会话通道）。
        bool GroupIdHasAnyMemberRow(const std::vector<uint8_t>& groupId, bool& outHasAny);

        // --- mm1_device_sessions（**MM1 `DeviceSessionManager`**；PK user_id + session_id；**session_id** 16B）---
        bool DeleteMm1DeviceSessionsWhereSessionId(const std::vector<uint8_t>& sessionId);
        bool DeleteMm1DeviceSessionByUserAndSession(
            const std::vector<uint8_t>& userId,
            const std::vector<uint8_t>& sessionId);
        bool ListMm1DeviceSessionsForUser(
            const std::vector<uint8_t>& userId,
            std::vector<std::vector<uint8_t>>& outSessionIds,
            std::vector<std::vector<uint8_t>>& outDeviceIds,
            std::vector<uint64_t>&             outLoginTimeMs,
            std::vector<uint64_t>&             outLastActiveMs);
        bool InsertMm1DeviceSession(
            const std::vector<uint8_t>& userId,
            const std::vector<uint8_t>& sessionId,
            const std::vector<uint8_t>& deviceId,
            uint64_t                    loginTimeMs,
            uint64_t                    lastActiveMs);
        bool UpdateMm1DeviceSessionLastActive(
            const std::vector<uint8_t>& userId,
            const std::vector<uint8_t>& sessionId,
            uint64_t                    lastActiveMs);
        bool DeleteMm1DeviceSessionsIdleOlderThan(uint64_t nowMs, uint64_t idleTimeoutMs);
        bool DeleteAllMm1DeviceSessions();

        // --- mm1_im_session_activity（**MM1 `SessionActivityManager`**；PK im_session_id 16B）---
        bool UpsertMm1ImSessionActivity(const std::vector<uint8_t>& imSessionId, uint64_t lastActiveMs);
        bool SelectMm1ImSessionLastActive(const std::vector<uint8_t>& imSessionId, uint64_t& outLastActiveMs, bool& outFound);
        bool DeleteMm1ImSessionActivityIdleOlderThan(uint64_t nowMs, uint64_t idleTimeoutMs);
        bool DeleteAllMm1ImSessionActivity();

        // --- mm1_cert_pin_config（单行 id=1）、mm1_cert_pin_client（**`CertPinningManager`**）---
        bool GetMm1CertPinConfig(std::vector<uint8_t>& outCurrentSpki, std::vector<uint8_t>& outStandbySpki);
        bool SetMm1CertPinConfig(const std::vector<uint8_t>& currentSpkiSha256, const std::vector<uint8_t>& standbySpkiSha256);
        bool GetMm1CertPinClient(const std::vector<uint8_t>& clientId, uint32_t& outFailCount, bool& outBanned, bool& outFound);
        bool UpsertMm1CertPinClient(const std::vector<uint8_t>& clientId, uint32_t failCount, bool banned);
        bool DeleteMm1CertPinClient(const std::vector<uint8_t>& clientId);
        bool DeleteAllMm1CertPinData();

        // --- mm1_user_status（**MM1 `UserStatusManager`**；**最后已知**在线，**非**服务端真相源）---
        bool UpsertMm1UserStatus(const std::vector<uint8_t>& userId, bool online, uint64_t updatedMs);
        // 无行时 **`outFound=false`**、`outOnline` 未用；仅 SQL/参数失败时 **false**。
        bool GetMm1UserStatus(const std::vector<uint8_t>& userId, bool& outOnline, bool& outFound);
        bool DeleteAllMm1UserStatus();

        // --- mm1_mention_atall_window（**@ALL** 限速；**times_blob** = 至多 3×uint64 **小端** 毫秒时间戳）---
        bool SelectMm1MentionAtAllTimes(
            const std::vector<uint8_t>& groupId,
            const std::vector<uint8_t>& senderId,
            std::vector<uint64_t>& outTimesMs);
        bool UpsertMm1MentionAtAllTimes(
            const std::vector<uint8_t>& groupId,
            const std::vector<uint8_t>& senderId,
            const std::vector<uint64_t>& timesMs);
        bool DeleteAllMm1MentionAtAllWindows();

        bool DeleteDataBlock(const std::vector<uint8_t>& dataId, int32_t chunkIdx);

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
