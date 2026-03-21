#pragma once

// SQLite metadata index (docs/02-Core/03-Storage.md): zdb_files, data_blocks, user_data, group_data, group_members.
// Plain SQLite for now; spec mentions SQLCipher — see class comment in .cpp.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ZChatIM::mm2 {

    // 03-Storage §2.3 type column (extend with new values as needed).
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

        // CREATE TABLE IF NOT EXISTS + PRAGMA foreign_keys=ON, user_version.
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

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace ZChatIM::mm2
