// Metadata index SQLite binding (see docs/02-Core/03-Storage.md).
// Production note: 03-Storage 第4.2节 mentions SQLCipher; this build uses vanilla SQLite until key management is wired.

#include "mm2/storage/SqliteMetadataDb.h"
#include "Types.h"

#include <sqlite3.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace ZChatIM::mm2 {

    namespace {

        static_assert(ZChatIM::SHA256_SIZE == 32U, "SHA256_SIZE must be 32 for sqlite BLOB and API");
        static_assert(
            ZChatIM::USER_ID_SIZE == ZChatIM::MESSAGE_ID_SIZE,
            "Schema assumes user_id/group_id and data_id BLOBs use the same 16-byte id size");

#ifdef _WIN32
        // SQLite on Windows interprets sqlite3_open_v2 filenames as UTF-8. MSVC path::string() is ACP.
        bool WidePathToUtf8(const std::wstring& w, std::string& out, std::string& errOut)
        {
            if (w.empty()) {
                out.clear();
                return true;
            }
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
            if (n <= 0) {
                errOut = "WideCharToMultiByte(CP_UTF8) failed";
                return false;
            }
            out.assign(static_cast<size_t>(n), '\0');
            if (WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr) != n) {
                errOut = "WideCharToMultiByte(CP_UTF8) size mismatch";
                return false;
            }
            return true;
        }
#endif


        constexpr int kSchemaUserVersion = 4;

        // Executed as a single script (sqlite3_exec).
        const char kCreateSchema[] =
            "PRAGMA foreign_keys = ON;\n"
            "CREATE TABLE IF NOT EXISTS zdb_files (\n"
            "  file_id TEXT PRIMARY KEY NOT NULL,\n"
            "  total_size INTEGER NOT NULL,\n"
            "  used_size INTEGER NOT NULL\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS data_blocks (\n"
            "  data_id BLOB NOT NULL,\n"
            "  chunk_idx INTEGER NOT NULL,\n"
            "  file_id TEXT NOT NULL REFERENCES zdb_files(file_id),\n"
            "  offset INTEGER NOT NULL,\n"
            "  length INTEGER NOT NULL,\n"
            "  sha256 BLOB NOT NULL,\n"
            "  PRIMARY KEY (data_id, chunk_idx)\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS user_data (\n"
            "  user_id BLOB NOT NULL,\n"
            "  file_id TEXT NOT NULL REFERENCES zdb_files(file_id),\n"
            "  offset INTEGER NOT NULL,\n"
            "  length INTEGER NOT NULL,\n"
            "  sha256 BLOB NOT NULL,\n"
            "  type INTEGER NOT NULL,\n"
            "  PRIMARY KEY (user_id, type)\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS group_data (\n"
            "  group_id BLOB PRIMARY KEY NOT NULL,\n"
            "  file_id TEXT NOT NULL REFERENCES zdb_files(file_id),\n"
            "  offset INTEGER NOT NULL,\n"
            "  length INTEGER NOT NULL,\n"
            "  sha256 BLOB NOT NULL\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS group_members (\n"
            "  group_id BLOB NOT NULL,\n"
            "  user_id BLOB NOT NULL,\n"
            "  role INTEGER NOT NULL,\n"
            "  joined_at INTEGER NOT NULL,\n"
            "  PRIMARY KEY (group_id, user_id)\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS im_messages (\n"
            "  message_id BLOB PRIMARY KEY NOT NULL,\n"
            "  session_id BLOB NOT NULL,\n"
            "  read_at_ms INTEGER,\n"
            "  edit_count INTEGER NOT NULL DEFAULT 0,\n"
            "  last_edit_time_s INTEGER NOT NULL DEFAULT 0\n"
            ");\n"
            "CREATE INDEX IF NOT EXISTS im_messages_by_session ON im_messages(session_id);\n"
            "CREATE TABLE IF NOT EXISTS im_message_reply (\n"
            "  message_id BLOB PRIMARY KEY NOT NULL,\n"
            "  replied_msg_id BLOB NOT NULL,\n"
            "  replied_sender_id BLOB NOT NULL,\n"
            "  replied_digest BLOB NOT NULL\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS friend_requests (\n"
            "  request_id BLOB PRIMARY KEY NOT NULL,\n"
            "  from_user BLOB NOT NULL,\n"
            "  to_user BLOB NOT NULL,\n"
            "  created_s INTEGER NOT NULL,\n"
            "  signature BLOB NOT NULL,\n"
            "  status INTEGER NOT NULL DEFAULT 0,\n"
            "  updated_s INTEGER NOT NULL DEFAULT 0,\n"
            "  responder BLOB\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS mm2_group_display (\n"
            "  group_id BLOB PRIMARY KEY NOT NULL,\n"
            "  name TEXT NOT NULL,\n"
            "  updated_s INTEGER NOT NULL,\n"
            "  updated_by BLOB NOT NULL\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS mm2_file_transfer (\n"
            "  logical_file_id TEXT PRIMARY KEY NOT NULL,\n"
            "  resume_chunk INTEGER NOT NULL DEFAULT 0,\n"
            "  complete_sha256 BLOB,\n"
            "  status INTEGER NOT NULL DEFAULT 0\n"
            ");\n";

        bool TableHasColumn(sqlite3* db, const char* table, const char* col, bool& out, std::string& errOut)
        {
            out = false;
            const std::string pragma = std::string("PRAGMA table_info(") + table + ");";
            sqlite3_stmt*     st     = nullptr;
            if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &st, nullptr) != SQLITE_OK) {
                errOut = sqlite3_errmsg(db);
                return false;
            }
            while (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(st, 1);
                if (name != nullptr && std::strcmp(reinterpret_cast<const char*>(name), col) == 0) {
                    out = true;
                    break;
                }
            }
            sqlite3_finalize(st);
            return true;
        }

        bool ExpectUserIdBlob(const std::vector<uint8_t>& id, std::string& errOut)
        {
            if (id.size() != ZChatIM::USER_ID_SIZE) {
                errOut = "user_id/group_id blob must be USER_ID_SIZE (16) bytes";
                return false;
            }
            return true;
        }

        bool ExpectDataIdBlob(const std::vector<uint8_t>& id, std::string& errOut)
        {
            if (id.size() != ZChatIM::MESSAGE_ID_SIZE) {
                errOut = "data_id must be MESSAGE_ID_SIZE (16) bytes";
                return false;
            }
            return true;
        }

        bool ExpectChunkIdxNonNegative(int32_t chunkIdx, std::string& errOut)
        {
            if (chunkIdx < 0) {
                errOut = "chunk_idx must be >= 0";
                return false;
            }
            return true;
        }

        bool ExpectSha256In(const uint8_t* sha256, std::string& errOut)
        {
            if (sha256 == nullptr) {
                errOut = "sha256 pointer is null";
                return false;
            }
            return true;
        }

        bool ExpectSha256Out(uint8_t* sha256Out, std::string& errOut)
        {
            if (sha256Out == nullptr) {
                errOut = "sha256 output pointer is null";
                return false;
            }
            return true;
        }

        bool ExpectNonEmptyFileId(const std::string& fileId, std::string& errOut)
        {
            if (fileId.empty()) {
                errOut = "file_id must be non-empty";
                return false;
            }
            return true;
        }

        bool CopySha256Column(sqlite3_stmt* stmt, int colIndex, uint8_t sha256Out[32], std::string& errOut)
        {
            if (sha256Out == nullptr) {
                errOut = "sha256 output pointer is null";
                return false;
            }
            if (sqlite3_column_type(stmt, colIndex) != SQLITE_BLOB) {
                errOut = "sha256 column not BLOB";
                return false;
            }
            const void* blob = sqlite3_column_blob(stmt, colIndex);
            const int   n    = sqlite3_column_bytes(stmt, colIndex);
            if (blob == nullptr || n != static_cast<int>(ZChatIM::SHA256_SIZE)) {
                errOut = "sha256 column length invalid";
                return false;
            }
            std::memcpy(sha256Out, blob, ZChatIM::SHA256_SIZE);
            return true;
        }

    } // namespace

    struct SqliteMetadataDb::Impl {
        sqlite3* db = nullptr;
        std::string lastError;
    };

    SqliteMetadataDb::SqliteMetadataDb()
        : impl_(std::make_unique<Impl>())
    {
    }

    SqliteMetadataDb::~SqliteMetadataDb()
    {
        Close();
    }

    SqliteMetadataDb::SqliteMetadataDb(SqliteMetadataDb&&) noexcept = default;

    SqliteMetadataDb& SqliteMetadataDb::operator=(SqliteMetadataDb&&) noexcept = default;

    bool SqliteMetadataDb::Open(const std::filesystem::path& dbPath)
    {
        if (!impl_) {
            return false;
        }
#ifdef _WIN32
        std::string utf8;
        if (!WidePathToUtf8(dbPath.native(), utf8, impl_->lastError)) {
            return false;
        }
        return Open(utf8);
#else
        // POSIX: path native narrow is typically UTF-8; u8string() is the portable spelling.
        return Open(dbPath.u8string());
#endif
    }

    bool SqliteMetadataDb::Open(const std::string& dbPath)
    {
        if (!impl_) {
            return false;
        }
        Close();
        sqlite3* raw = nullptr;
        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        const int rc    = sqlite3_open_v2(dbPath.c_str(), &raw, flags, nullptr);
        if (rc != SQLITE_OK) {
            impl_->lastError = raw ? sqlite3_errmsg(raw) : "sqlite3_open_v2 failed";
            if (raw) {
                sqlite3_close(raw);
            }
            return false;
        }
        impl_->db = raw;
        // Foreign keys are per-connection; enable on every open (not only InitializeSchema).
        sqlite3_exec(impl_->db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
        // 多进程/多连接短时锁冲突时重试（毫秒）；与 MM2 单例锁互补。
        sqlite3_busy_timeout(impl_->db, 5000);

        impl_->lastError.clear();
        return true;
    }

    void SqliteMetadataDb::Close()
    {
        if (!impl_ || impl_->db == nullptr) {
            return;
        }
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
        impl_->lastError.clear();
    }

    bool SqliteMetadataDb::IsOpen() const
    {
        return impl_ && impl_->db != nullptr;
    }

    bool SqliteMetadataDb::InitializeSchema()
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        char* errMsg = nullptr;
        const int rc = sqlite3_exec(impl_->db, kCreateSchema, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            impl_->lastError = errMsg ? errMsg : sqlite3_errmsg(impl_->db);
            sqlite3_free(errMsg);
            return false;
        }
        sqlite3_free(errMsg);

        // v2 → v3：已有库可能无 **`read_at_ms`** 列（`CREATE IF NOT EXISTS` 不会改旧表）。
        bool hasReadAtMs = false;
        if (!TableHasColumn(impl_->db, "im_messages", "read_at_ms", hasReadAtMs, impl_->lastError)) {
            return false;
        }
        if (!hasReadAtMs) {
            char* alterErr = nullptr;
            const int rcAlter = sqlite3_exec(
                impl_->db, "ALTER TABLE im_messages ADD COLUMN read_at_ms INTEGER;", nullptr, nullptr, &alterErr);
            if (rcAlter != SQLITE_OK) {
                impl_->lastError = alterErr ? alterErr : sqlite3_errmsg(impl_->db);
                sqlite3_free(alterErr);
                return false;
            }
            sqlite3_free(alterErr);
        }

        bool hasEditCount = false;
        if (!TableHasColumn(impl_->db, "im_messages", "edit_count", hasEditCount, impl_->lastError)) {
            return false;
        }
        if (!hasEditCount) {
            char* alterErr = nullptr;
            int     rcAlter = sqlite3_exec(
                impl_->db, "ALTER TABLE im_messages ADD COLUMN edit_count INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &alterErr);
            if (rcAlter != SQLITE_OK) {
                impl_->lastError = alterErr ? alterErr : sqlite3_errmsg(impl_->db);
                sqlite3_free(alterErr);
                return false;
            }
            sqlite3_free(alterErr);
            alterErr = nullptr;
            rcAlter  = sqlite3_exec(
                impl_->db, "ALTER TABLE im_messages ADD COLUMN last_edit_time_s INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &alterErr);
            if (rcAlter != SQLITE_OK) {
                impl_->lastError = alterErr ? alterErr : sqlite3_errmsg(impl_->db);
                sqlite3_free(alterErr);
                return false;
            }
            sqlite3_free(alterErr);
        }

        char* verErr = nullptr;
        const std::string pragmaUserVer =
            "PRAGMA user_version = " + std::to_string(kSchemaUserVersion) + ";";
        const int rc2 = sqlite3_exec(impl_->db, pragmaUserVer.c_str(), nullptr, nullptr, &verErr);
        if (rc2 != SQLITE_OK) {
            impl_->lastError = verErr ? verErr : sqlite3_errmsg(impl_->db);
            sqlite3_free(verErr);
            return false;
        }
        sqlite3_free(verErr);

        impl_->lastError.clear();
        return true;
    }

    std::string SqliteMetadataDb::LastError() const
    {
        return impl_ ? impl_->lastError : std::string();
    }

    bool SqliteMetadataDb::UpsertZdbFile(const std::string& fileId, uint64_t totalSize, uint64_t usedSize)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(fileId, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO zdb_files (file_id, total_size, used_size) VALUES (?, ?, ?)\n"
            "ON CONFLICT(file_id) DO UPDATE SET total_size = excluded.total_size, used_size = excluded.used_size;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, fileId.c_str(), static_cast<int>(fileId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(totalSize));
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(usedSize));

        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetZdbFile(const std::string& fileId, uint64_t& totalSize, uint64_t& usedSize)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(fileId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT total_size, used_size FROM zdb_files WHERE file_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, fileId.c_str(), static_cast<int>(fileId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "file_id not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        totalSize = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        usedSize  = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::InsertDataBlock(
        const std::vector<uint8_t>& dataId,
        int32_t                     chunkIdx,
        const std::string&          fileId,
        uint64_t                    offset,
        uint64_t                    length,
        const uint8_t               sha256[32])
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        // Must match UpsertDataBlock / GetDataBlock / DataBlockExists preconditions (strict layer).
        if (!ExpectDataIdBlob(dataId, impl_->lastError) || !ExpectChunkIdxNonNegative(chunkIdx, impl_->lastError)
            || !ExpectNonEmptyFileId(fileId, impl_->lastError) || !ExpectSha256In(sha256, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO data_blocks (data_id, chunk_idx, file_id, offset, length, sha256)\n"
            "VALUES (?, ?, ?, ?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, dataId.data(), static_cast<int>(dataId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(chunkIdx));
        sqlite3_bind_text(stmt, 3, fileId.c_str(), static_cast<int>(fileId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(offset));
        sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(length));
        sqlite3_bind_blob(stmt, 6, sha256, static_cast<int>(ZChatIM::SHA256_SIZE), SQLITE_TRANSIENT);

        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::DataBlockExists(const std::vector<uint8_t>& dataId, int32_t chunkIdx)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(dataId, impl_->lastError) || !ExpectChunkIdxNonNegative(chunkIdx, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "SELECT 1 FROM data_blocks WHERE data_id = ? AND chunk_idx = ? LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, dataId.data(), static_cast<int>(dataId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(chunkIdx));
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step == SQLITE_ROW) {
            impl_->lastError.clear();
            return true;
        }
        if (step == SQLITE_DONE) {
            impl_->lastError.clear();
            return false;
        }
        impl_->lastError = sqlite3_errmsg(impl_->db);
        return false;
    }

    bool SqliteMetadataDb::UpsertDataBlock(
        const std::vector<uint8_t>& dataId,
        int32_t                     chunkIdx,
        const std::string&          fileId,
        uint64_t                    offset,
        uint64_t                    length,
        const uint8_t               sha256[32])
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(dataId, impl_->lastError) || !ExpectChunkIdxNonNegative(chunkIdx, impl_->lastError)
            || !ExpectNonEmptyFileId(fileId, impl_->lastError) || !ExpectSha256In(sha256, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO data_blocks (data_id, chunk_idx, file_id, offset, length, sha256)\n"
            "VALUES (?, ?, ?, ?, ?, ?)\n"
            "ON CONFLICT(data_id, chunk_idx) DO UPDATE SET file_id = excluded.file_id, "
            "offset = excluded.offset, length = excluded.length, sha256 = excluded.sha256;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, dataId.data(), static_cast<int>(dataId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(chunkIdx));
        sqlite3_bind_text(stmt, 3, fileId.c_str(), static_cast<int>(fileId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(offset));
        sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(length));
        sqlite3_bind_blob(stmt, 6, sha256, static_cast<int>(ZChatIM::SHA256_SIZE), SQLITE_TRANSIENT);

        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetDataBlock(
        const std::vector<uint8_t>& dataId,
        int32_t                     chunkIdx,
        std::string&                fileIdOut,
        uint64_t&                   offsetOut,
        uint64_t&                   lengthOut,
        uint8_t                     sha256Out[32])
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(dataId, impl_->lastError) || !ExpectChunkIdxNonNegative(chunkIdx, impl_->lastError)
            || !ExpectSha256Out(sha256Out, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "SELECT file_id, offset, length, sha256 FROM data_blocks WHERE data_id = ? AND chunk_idx = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, dataId.data(), static_cast<int>(dataId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(chunkIdx));
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "data_blocks row not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        const unsigned char* fid = sqlite3_column_text(stmt, 0);
        const int            fl  = sqlite3_column_bytes(stmt, 0);
        if (fid == nullptr) {
            sqlite3_finalize(stmt);
            impl_->lastError = "data_blocks file_id null";
            return false;
        }
        fileIdOut.assign(reinterpret_cast<const char*>(fid), static_cast<size_t>(fl));
        offsetOut = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        lengthOut = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        if (!CopySha256Column(stmt, 3, sha256Out, impl_->lastError)) {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpsertUserData(
        const std::vector<uint8_t>& userId,
        const std::string&          fileId,
        uint64_t                    offset,
        uint64_t                    length,
        const uint8_t               sha256[32],
        int32_t                     type)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(userId, impl_->lastError) || !ExpectNonEmptyFileId(fileId, impl_->lastError)
            || !ExpectSha256In(sha256, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO user_data (user_id, file_id, offset, length, sha256, type) VALUES (?, ?, ?, ?, ?, ?)\n"
            "ON CONFLICT(user_id, type) DO UPDATE SET file_id = excluded.file_id, offset = excluded.offset, "
            "length = excluded.length, sha256 = excluded.sha256;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, userId.data(), static_cast<int>(userId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, fileId.c_str(), static_cast<int>(fileId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(offset));
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(length));
        sqlite3_bind_blob(stmt, 5, sha256, static_cast<int>(ZChatIM::SHA256_SIZE), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, static_cast<int>(type));

        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetUserData(
        const std::vector<uint8_t>& userId,
        int32_t                     type,
        std::string&                fileId,
        uint64_t&                   offset,
        uint64_t&                   length,
        uint8_t                     sha256Out[32])
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(userId, impl_->lastError) || !ExpectSha256Out(sha256Out, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "SELECT file_id, offset, length, sha256 FROM user_data WHERE user_id = ? AND type = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, userId.data(), static_cast<int>(userId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(type));
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "user_data row not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        const unsigned char* fid = sqlite3_column_text(stmt, 0);
        const int            fl  = sqlite3_column_bytes(stmt, 0);
        if (fid == nullptr) {
            sqlite3_finalize(stmt);
            impl_->lastError = "user_data file_id null";
            return false;
        }
        fileId.assign(reinterpret_cast<const char*>(fid), static_cast<size_t>(fl));
        offset = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        length = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        if (!CopySha256Column(stmt, 3, sha256Out, impl_->lastError)) {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpsertGroupData(
        const std::vector<uint8_t>& groupId,
        const std::string&          fileId,
        uint64_t                    offset,
        uint64_t                    length,
        const uint8_t               sha256[32])
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(groupId, impl_->lastError) || !ExpectNonEmptyFileId(fileId, impl_->lastError)
            || !ExpectSha256In(sha256, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO group_data (group_id, file_id, offset, length, sha256) VALUES (?, ?, ?, ?, ?)\n"
            "ON CONFLICT(group_id) DO UPDATE SET file_id = excluded.file_id, offset = excluded.offset, "
            "length = excluded.length, sha256 = excluded.sha256;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, groupId.data(), static_cast<int>(groupId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, fileId.c_str(), static_cast<int>(fileId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(offset));
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(length));
        sqlite3_bind_blob(stmt, 5, sha256, static_cast<int>(ZChatIM::SHA256_SIZE), SQLITE_TRANSIENT);

        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetGroupData(
        const std::vector<uint8_t>& groupId,
        std::string&                fileId,
        uint64_t&                   offset,
        uint64_t&                   length,
        uint8_t                     sha256Out[32])
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(groupId, impl_->lastError) || !ExpectSha256Out(sha256Out, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "SELECT file_id, offset, length, sha256 FROM group_data WHERE group_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, groupId.data(), static_cast<int>(groupId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "group_data row not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        const unsigned char* fid = sqlite3_column_text(stmt, 0);
        const int            fl  = sqlite3_column_bytes(stmt, 0);
        if (fid == nullptr) {
            sqlite3_finalize(stmt);
            impl_->lastError = "group_data file_id null";
            return false;
        }
        fileId.assign(reinterpret_cast<const char*>(fid), static_cast<size_t>(fl));
        offset = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        length = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        if (!CopySha256Column(stmt, 3, sha256Out, impl_->lastError)) {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpsertGroupMember(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        int32_t                     role,
        int64_t                     joinedAt)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(groupId, impl_->lastError)) {
            return false;
        }
        if (!ExpectUserIdBlob(userId, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO group_members (group_id, user_id, role, joined_at) VALUES (?, ?, ?, ?)\n"
            "ON CONFLICT(group_id, user_id) DO UPDATE SET role = excluded.role, joined_at = excluded.joined_at;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, groupId.data(), static_cast<int>(groupId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, userId.data(), static_cast<int>(userId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, static_cast<int>(role));
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(joinedAt));

        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetGroupMember(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        int32_t&                    roleOut,
        int64_t&                    joinedAtOut)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(groupId, impl_->lastError)) {
            return false;
        }
        if (!ExpectUserIdBlob(userId, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "SELECT role, joined_at FROM group_members WHERE group_id = ? AND user_id = ? LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, groupId.data(), static_cast<int>(groupId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, userId.data(), static_cast<int>(userId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "group_members row not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        roleOut     = sqlite3_column_int(stmt, 0);
        joinedAtOut = sqlite3_column_int64(stmt, 1);
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::DeleteGroupMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(groupId, impl_->lastError)) {
            return false;
        }
        if (!ExpectUserIdBlob(userId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "DELETE FROM group_members WHERE group_id = ? AND user_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, groupId.data(), static_cast<int>(groupId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, userId.data(), static_cast<int>(userId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_changes(impl_->db) == 0) {
            impl_->lastError = "group_members delete: row not found";
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::InsertImMessage(const std::vector<uint8_t>& sessionId, const std::vector<uint8_t>& messageId)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(sessionId, impl_->lastError)) {
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "INSERT INTO im_messages (message_id, session_id) VALUES (?, ?);";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, sessionId.data(), static_cast<int>(sessionId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::MarkImMessageRead(const std::vector<uint8_t>& messageId, int64_t readAtMs)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        if (!ImMessageExists(messageId)) {
            impl_->lastError = "im_messages row not found";
            return false;
        }
        static const char sql[] =
            "UPDATE im_messages SET read_at_ms = ? WHERE message_id = ? AND read_at_ms IS NULL;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(readAtMs));
        sqlite3_bind_blob(stmt, 2, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::ListUnreadImMessageIdsForSession(
        const std::vector<uint8_t>& sessionId,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outMessageIds)
    {
        outMessageIds.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(sessionId, impl_->lastError)) {
            return false;
        }
        if (limit == 0) {
            impl_->lastError.clear();
            return true;
        }
        if (limit > static_cast<size_t>(INT_MAX)) {
            impl_->lastError = "ListUnreadImMessageIdsForSession: limit too large";
            return false;
        }

        static const char sql[] = "SELECT message_id FROM im_messages WHERE session_id = ? AND read_at_ms IS NULL "
                                  "ORDER BY rowid ASC LIMIT ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, sessionId.data(), static_cast<int>(sessionId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

        int step = sqlite3_step(stmt);
        while (step == SQLITE_ROW) {
            if (sqlite3_column_type(stmt, 0) != SQLITE_BLOB) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id column not BLOB";
                outMessageIds.clear();
                return false;
            }
            const void* blob = sqlite3_column_blob(stmt, 0);
            const int   n    = sqlite3_column_bytes(stmt, 0);
            if (blob == nullptr || n != static_cast<int>(ZChatIM::MESSAGE_ID_SIZE)) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id blob length invalid";
                outMessageIds.clear();
                return false;
            }
            std::vector<uint8_t> mid(
                static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + n);
            outMessageIds.push_back(std::move(mid));
            step = sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            outMessageIds.clear();
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::ListImMessageIdsForSession(
        const std::vector<uint8_t>& sessionId,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outMessageIds)
    {
        outMessageIds.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(sessionId, impl_->lastError)) {
            return false;
        }
        if (limit == 0) {
            impl_->lastError.clear();
            return true;
        }
        if (limit > static_cast<size_t>(INT_MAX)) {
            impl_->lastError = "ListImMessageIdsForSession: limit too large";
            return false;
        }

        static const char sql[] =
            "SELECT message_id FROM im_messages WHERE session_id = ? ORDER BY rowid DESC LIMIT ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, sessionId.data(), static_cast<int>(sessionId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

        int step = sqlite3_step(stmt);
        while (step == SQLITE_ROW) {
            if (sqlite3_column_type(stmt, 0) != SQLITE_BLOB) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id column not BLOB";
                outMessageIds.clear();
                return false;
            }
            const void* blob = sqlite3_column_blob(stmt, 0);
            const int   n    = sqlite3_column_bytes(stmt, 0);
            if (blob == nullptr || n != static_cast<int>(ZChatIM::MESSAGE_ID_SIZE)) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id blob length invalid";
                outMessageIds.clear();
                return false;
            }
            std::vector<uint8_t> mid(
                static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + n);
            outMessageIds.push_back(std::move(mid));
            step = sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            outMessageIds.clear();
            return false;
        }
        std::reverse(outMessageIds.begin(), outMessageIds.end());
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::ListImMessageIdsForSessionChronological(
        const std::vector<uint8_t>& sessionId,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outMessageIds)
    {
        outMessageIds.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(sessionId, impl_->lastError)) {
            return false;
        }
        if (limit == 0) {
            impl_->lastError.clear();
            return true;
        }
        if (limit > static_cast<size_t>(INT_MAX)) {
            impl_->lastError = "ListImMessageIdsForSessionChronological: limit too large";
            return false;
        }

        static const char sql[] =
            "SELECT message_id FROM im_messages WHERE session_id = ? ORDER BY rowid ASC LIMIT ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, sessionId.data(), static_cast<int>(sessionId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

        int step = sqlite3_step(stmt);
        while (step == SQLITE_ROW) {
            if (sqlite3_column_type(stmt, 0) != SQLITE_BLOB) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id column not BLOB";
                outMessageIds.clear();
                return false;
            }
            const void* blob = sqlite3_column_blob(stmt, 0);
            const int   n    = sqlite3_column_bytes(stmt, 0);
            if (blob == nullptr || n != static_cast<int>(ZChatIM::MESSAGE_ID_SIZE)) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id blob length invalid";
                outMessageIds.clear();
                return false;
            }
            std::vector<uint8_t> mid(
                static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + n);
            outMessageIds.push_back(std::move(mid));
            step = sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            outMessageIds.clear();
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::ListImMessageIdsForSessionAfterMessageId(
        const std::vector<uint8_t>& sessionId,
        const std::vector<uint8_t>& afterMessageId,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outMessageIds)
    {
        outMessageIds.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(sessionId, impl_->lastError)) {
            return false;
        }
        if (!ExpectDataIdBlob(afterMessageId, impl_->lastError)) {
            return false;
        }
        if (limit == 0) {
            impl_->lastError.clear();
            return true;
        }
        if (limit > static_cast<size_t>(INT_MAX)) {
            impl_->lastError = "ListImMessageIdsForSessionAfterMessageId: limit too large";
            return false;
        }

        static const char sql[] = "SELECT m1.message_id FROM im_messages AS m1 "
                                  "WHERE m1.session_id = ? AND m1.rowid > "
                                  "(SELECT m2.rowid FROM im_messages AS m2 WHERE m2.session_id = ? AND m2.message_id = ? LIMIT 1) "
                                  "ORDER BY m1.rowid ASC LIMIT ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, sessionId.data(), static_cast<int>(sessionId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, sessionId.data(), static_cast<int>(sessionId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 3, afterMessageId.data(), static_cast<int>(afterMessageId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, static_cast<int>(limit));

        int step = sqlite3_step(stmt);
        while (step == SQLITE_ROW) {
            if (sqlite3_column_type(stmt, 0) != SQLITE_BLOB) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id column not BLOB";
                outMessageIds.clear();
                return false;
            }
            const void* blob = sqlite3_column_blob(stmt, 0);
            const int   n    = sqlite3_column_bytes(stmt, 0);
            if (blob == nullptr || n != static_cast<int>(ZChatIM::MESSAGE_ID_SIZE)) {
                sqlite3_finalize(stmt);
                impl_->lastError = "message_id blob length invalid";
                outMessageIds.clear();
                return false;
            }
            std::vector<uint8_t> mid(
                static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + n);
            outMessageIds.push_back(std::move(mid));
            step = sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            outMessageIds.clear();
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetImMessageSession(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& sessionIdOut)
    {
        sessionIdOut.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT session_id FROM im_messages WHERE message_id = ? LIMIT 1;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "im_messages row not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_column_type(stmt, 0) != SQLITE_BLOB) {
            sqlite3_finalize(stmt);
            impl_->lastError = "session_id column not BLOB";
            return false;
        }
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int   n    = sqlite3_column_bytes(stmt, 0);
        if (blob == nullptr || n != static_cast<int>(ZChatIM::USER_ID_SIZE)) {
            sqlite3_finalize(stmt);
            impl_->lastError = "session_id blob length invalid";
            return false;
        }
        sessionIdOut.assign(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + n);
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::ImMessageExists(const std::vector<uint8_t>& messageId)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT 1 FROM im_messages WHERE message_id = ? LIMIT 1;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step == SQLITE_ROW) {
            impl_->lastError.clear();
            return true;
        }
        if (step == SQLITE_DONE) {
            impl_->lastError.clear();
            return false;
        }
        impl_->lastError = sqlite3_errmsg(impl_->db);
        return false;
    }

    bool SqliteMetadataDb::DeleteImMessage(const std::vector<uint8_t>& messageId)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "DELETE FROM im_messages WHERE message_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_changes(impl_->db) == 0) {
            impl_->lastError = "im_messages delete: row not found";
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::DeleteDataBlock(const std::vector<uint8_t>& dataId, int32_t chunkIdx)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(dataId, impl_->lastError) || !ExpectChunkIdxNonNegative(chunkIdx, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "DELETE FROM data_blocks WHERE data_id = ? AND chunk_idx = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, dataId.data(), static_cast<int>(dataId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(chunkIdx));
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_changes(impl_->db) == 0) {
            impl_->lastError = "data_blocks delete: row not found";
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::DeleteMessageMetadataTransaction(
        const std::vector<uint8_t>& messageId,
        bool                        deleteDataBlockChunk0)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }

        char* errMsg = nullptr;
        if (sqlite3_exec(impl_->db, "BEGIN IMMEDIATE;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            impl_->lastError = errMsg ? errMsg : "BEGIN IMMEDIATE failed";
            sqlite3_free(errMsg);
            return false;
        }

        bool ok = true;

        if (ok) {
            static const char sqlRep[] = "DELETE FROM im_message_reply WHERE message_id = ?;";
            sqlite3_stmt*     stmt    = nullptr;
            if (sqlite3_prepare_v2(impl_->db, sqlRep, -1, &stmt, nullptr) != SQLITE_OK) {
                impl_->lastError = sqlite3_errmsg(impl_->db);
                ok               = false;
            } else {
                sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
                const int step = sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                if (step != SQLITE_DONE) {
                    impl_->lastError = sqlite3_errmsg(impl_->db);
                    ok               = false;
                }
            }
        }

        if (deleteDataBlockChunk0) {
            static const char sqlDb[] = "DELETE FROM data_blocks WHERE data_id = ? AND chunk_idx = ?;";
            sqlite3_stmt*     stmt    = nullptr;
            if (sqlite3_prepare_v2(impl_->db, sqlDb, -1, &stmt, nullptr) != SQLITE_OK) {
                impl_->lastError = sqlite3_errmsg(impl_->db);
                ok               = false;
            } else {
                sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, 0);
                const int step = sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                if (step != SQLITE_DONE) {
                    impl_->lastError = sqlite3_errmsg(impl_->db);
                    ok               = false;
                } else if (sqlite3_changes(impl_->db) != 1) {
                    impl_->lastError = "DeleteMessageMetadataTransaction: data_blocks row missing";
                    ok               = false;
                }
            }
        }

        if (ok) {
            static const char sqlIm[] = "DELETE FROM im_messages WHERE message_id = ?;";
            sqlite3_stmt*     stmt    = nullptr;
            if (sqlite3_prepare_v2(impl_->db, sqlIm, -1, &stmt, nullptr) != SQLITE_OK) {
                impl_->lastError = sqlite3_errmsg(impl_->db);
                ok               = false;
            } else {
                sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
                const int step = sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                if (step != SQLITE_DONE) {
                    impl_->lastError = sqlite3_errmsg(impl_->db);
                    ok               = false;
                } else if (sqlite3_changes(impl_->db) != 1) {
                    impl_->lastError = "DeleteMessageMetadataTransaction: im_messages row missing";
                    ok               = false;
                }
            }
        }

        if (!ok) {
            sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        errMsg = nullptr;
        if (sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            impl_->lastError = errMsg ? errMsg : "COMMIT failed";
            sqlite3_free(errMsg);
            sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpsertImMessageReply(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& repliedMsgId,
        const std::vector<uint8_t>& repliedSenderId,
        const std::vector<uint8_t>& repliedDigest)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError) || !ExpectDataIdBlob(repliedMsgId, impl_->lastError)
            || !ExpectUserIdBlob(repliedSenderId, impl_->lastError)) {
            return false;
        }
        if (repliedDigest.size() != ZChatIM::SHA256_SIZE) {
            impl_->lastError = "replied_digest must be SHA256_SIZE (32) bytes";
            return false;
        }
        static const char sql[] = "INSERT INTO im_message_reply (message_id, replied_msg_id, replied_sender_id, replied_digest) "
                                  "VALUES (?, ?, ?, ?) "
                                  "ON CONFLICT(message_id) DO UPDATE SET "
                                  "replied_msg_id=excluded.replied_msg_id, "
                                  "replied_sender_id=excluded.replied_sender_id, "
                                  "replied_digest=excluded.replied_digest;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, repliedMsgId.data(), static_cast<int>(repliedMsgId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 3, repliedSenderId.data(), static_cast<int>(repliedSenderId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 4, repliedDigest.data(), static_cast<int>(repliedDigest.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetImMessageReply(
        const std::vector<uint8_t>& messageId,
        std::vector<uint8_t>& outRepliedMsgId,
        std::vector<uint8_t>& outRepliedSenderId,
        std::vector<uint8_t>& outRepliedDigest)
    {
        outRepliedMsgId.clear();
        outRepliedSenderId.clear();
        outRepliedDigest.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "SELECT replied_msg_id, replied_sender_id, replied_digest FROM im_message_reply WHERE message_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "im_message_reply not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        const void* b0 = sqlite3_column_blob(stmt, 0);
        const int   n0 = sqlite3_column_bytes(stmt, 0);
        const void* b1 = sqlite3_column_blob(stmt, 1);
        const int   n1 = sqlite3_column_bytes(stmt, 1);
        const void* b2 = sqlite3_column_blob(stmt, 2);
        const int   n2 = sqlite3_column_bytes(stmt, 2);
        if (b0 == nullptr || n0 != ZChatIM::MESSAGE_ID_SIZE || b1 == nullptr || n1 != ZChatIM::USER_ID_SIZE
            || b2 == nullptr || n2 != static_cast<int>(ZChatIM::SHA256_SIZE)) {
            sqlite3_finalize(stmt);
            impl_->lastError = "im_message_reply row has invalid BLOB sizes";
            return false;
        }
        outRepliedMsgId.assign(static_cast<const uint8_t*>(b0), static_cast<const uint8_t*>(b0) + n0);
        outRepliedSenderId.assign(static_cast<const uint8_t*>(b1), static_cast<const uint8_t*>(b1) + n1);
        outRepliedDigest.assign(static_cast<const uint8_t*>(b2), static_cast<const uint8_t*>(b2) + n2);
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpdateImMessageEditState(
        const std::vector<uint8_t>& messageId,
        uint32_t                    editCount,
        uint64_t                    lastEditTimeSeconds)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "UPDATE im_messages SET edit_count = ?, last_edit_time_s = ? WHERE message_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_int(stmt, 1, static_cast<int>(editCount));
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(lastEditTimeSeconds));
        sqlite3_bind_blob(stmt, 3, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_changes(impl_->db) != 1) {
            impl_->lastError = "UpdateImMessageEditState: im_messages row missing";
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetImMessageEditState(
        const std::vector<uint8_t>& messageId,
        uint32_t& outEditCount,
        uint64_t& outLastEditTimeSeconds)
    {
        outEditCount           = 0;
        outLastEditTimeSeconds = 0;
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(messageId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT edit_count, last_edit_time_s FROM im_messages WHERE message_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, messageId.data(), static_cast<int>(messageId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "im_messages row not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        outEditCount           = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
        outLastEditTimeSeconds = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    size_t SqliteMetadataDb::CountImMessages()
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return 0;
        }
        static const char sql[] = "SELECT COUNT(*) FROM im_messages;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return 0;
        }
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return 0;
        }
        const sqlite3_int64 n = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return static_cast<size_t>(n < 0 ? 0 : n);
    }

    bool SqliteMetadataDb::ListAllImMessageIdsForSession(
        const std::vector<uint8_t>& sessionId,
        std::vector<std::vector<uint8_t>>& outMessageIds)
    {
        outMessageIds.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(sessionId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT message_id FROM im_messages WHERE session_id = ? ORDER BY rowid ASC;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, sessionId.data(), static_cast<int>(sessionId.size()), SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            const int   n    = sqlite3_column_bytes(stmt, 0);
            if (blob == nullptr || n != ZChatIM::MESSAGE_ID_SIZE) {
                sqlite3_finalize(stmt);
                impl_->lastError = "ListAllImMessageIdsForSession: invalid message_id column";
                outMessageIds.clear();
                return false;
            }
            const auto* p = static_cast<const uint8_t*>(blob);
            outMessageIds.emplace_back(p, p + n);
        }
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::InsertFriendRequest(
        const std::vector<uint8_t>& requestId,
        const std::vector<uint8_t>& fromUserId,
        const std::vector<uint8_t>& toUserId,
        uint64_t                    createdSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(requestId, impl_->lastError) || !ExpectUserIdBlob(fromUserId, impl_->lastError)
            || !ExpectUserIdBlob(toUserId, impl_->lastError)) {
            return false;
        }
        if (signatureEd25519.empty()) {
            impl_->lastError = "signature must be non-empty";
            return false;
        }
        static const char sql[] =
            "INSERT INTO friend_requests (request_id, from_user, to_user, created_s, signature, status, updated_s) "
            "VALUES (?, ?, ?, ?, ?, 0, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, fromUserId.data(), static_cast<int>(fromUserId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 3, toUserId.data(), static_cast<int>(toUserId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(createdSeconds));
        sqlite3_bind_blob(
            stmt, 5, signatureEd25519.data(), static_cast<int>(signatureEd25519.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(createdSeconds));
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpdateFriendRequestStatus(
        const std::vector<uint8_t>& requestId,
        int32_t                     status,
        const std::vector<uint8_t>& responderId,
        uint64_t                    updatedSeconds)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(requestId, impl_->lastError) || !ExpectUserIdBlob(responderId, impl_->lastError)) {
            return false;
        }
        if (status != 1 && status != 2) {
            impl_->lastError = "friend request status must be 1 (accepted) or 2 (rejected)";
            return false;
        }
        static const char sql[] = "UPDATE friend_requests SET status = ?, updated_s = ?, responder = ? "
                                  "WHERE request_id = ? AND status = 0;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_int(stmt, 1, static_cast<int>(status));
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(updatedSeconds));
        sqlite3_bind_blob(stmt, 3, responderId.data(), static_cast<int>(responderId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 4, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_changes(impl_->db) != 1) {
            impl_->lastError = "UpdateFriendRequestStatus: request not found or not pending";
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::DeleteFriendRequest(const std::vector<uint8_t>& requestId)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectDataIdBlob(requestId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "DELETE FROM friend_requests WHERE request_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_changes(impl_->db) != 1) {
            impl_->lastError = "DeleteFriendRequest: row not found";
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::DeleteExpiredPendingFriendRequests(uint64_t nowSeconds, uint64_t ttlSeconds)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        static const char sql[] = "DELETE FROM friend_requests WHERE status = 0 AND (?1 - ?2) > created_s;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(nowSeconds));
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(ttlSeconds));
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpsertGroupDisplayName(
        const std::vector<uint8_t>& groupId,
        const std::string&          nameUtf8,
        uint64_t                    updatedSeconds,
        const std::vector<uint8_t>& updatedBy)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(groupId, impl_->lastError) || !ExpectUserIdBlob(updatedBy, impl_->lastError)) {
            return false;
        }
        if (nameUtf8.empty()) {
            impl_->lastError = "group name must be non-empty";
            return false;
        }
        static const char sql[] = "INSERT INTO mm2_group_display (group_id, name, updated_s, updated_by) VALUES (?, ?, ?, ?) "
                                  "ON CONFLICT(group_id) DO UPDATE SET "
                                  "name=excluded.name, updated_s=excluded.updated_s, updated_by=excluded.updated_by;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, groupId.data(), static_cast<int>(groupId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, nameUtf8.c_str(), static_cast<int>(nameUtf8.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(updatedSeconds));
        sqlite3_bind_blob(stmt, 4, updatedBy.data(), static_cast<int>(updatedBy.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetGroupDisplayName(const std::vector<uint8_t>& groupId, std::string& outNameUtf8)
    {
        outNameUtf8.clear();
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectUserIdBlob(groupId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT name FROM mm2_group_display WHERE group_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, groupId.data(), static_cast<int>(groupId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "mm2_group_display not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        const unsigned char* t = sqlite3_column_text(stmt, 0);
        const int            n = sqlite3_column_bytes(stmt, 0);
        if (t == nullptr || n < 0) {
            sqlite3_finalize(stmt);
            impl_->lastError = "invalid name column";
            return false;
        }
        outNameUtf8.assign(reinterpret_cast<const char*>(t), static_cast<size_t>(n));
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::UpsertFileTransferResume(const std::string& logicalFileId, uint32_t resumeChunk)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(logicalFileId, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO mm2_file_transfer (logical_file_id, resume_chunk, status, complete_sha256) VALUES (?, ?, 0, NULL) "
            "ON CONFLICT(logical_file_id) DO UPDATE SET resume_chunk=excluded.resume_chunk, status=0, complete_sha256=NULL;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, logicalFileId.c_str(), static_cast<int>(logicalFileId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(resumeChunk));
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetFileTransferResumeChunk(const std::string& logicalFileId, uint32_t& outResumeChunk)
    {
        outResumeChunk = 0;
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(logicalFileId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT resume_chunk FROM mm2_file_transfer WHERE logical_file_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, logicalFileId.c_str(), static_cast<int>(logicalFileId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "mm2_file_transfer not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        const sqlite3_int64 v = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        if (v < 0 || v > static_cast<sqlite3_int64>(UINT32_MAX)) {
            impl_->lastError = "resume_chunk out of range";
            return false;
        }
        outResumeChunk = static_cast<uint32_t>(v);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::GetFileTransferStatus(const std::string& logicalFileId, int32_t& outStatus)
    {
        outStatus = 0;
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(logicalFileId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "SELECT status FROM mm2_file_transfer WHERE logical_file_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, logicalFileId.c_str(), static_cast<int>(logicalFileId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            impl_->lastError = (step == SQLITE_DONE) ? "mm2_file_transfer not found" : sqlite3_errmsg(impl_->db);
            return false;
        }
        outStatus = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::SetFileTransferComplete(const std::string& logicalFileId, const uint8_t sha256[32])
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(logicalFileId, impl_->lastError) || !ExpectSha256In(sha256, impl_->lastError)) {
            return false;
        }
        static const char sql[] =
            "INSERT INTO mm2_file_transfer (logical_file_id, resume_chunk, complete_sha256, status) VALUES (?, 0, ?, 1) "
            "ON CONFLICT(logical_file_id) DO UPDATE SET complete_sha256=excluded.complete_sha256, status=1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, logicalFileId.c_str(), static_cast<int>(logicalFileId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, sha256, static_cast<int>(ZChatIM::SHA256_SIZE), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::SetFileTransferCancelled(const std::string& logicalFileId)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(logicalFileId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "UPDATE mm2_file_transfer SET status = 2 WHERE logical_file_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, logicalFileId.c_str(), static_cast<int>(logicalFileId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        if (sqlite3_changes(impl_->db) != 1) {
            impl_->lastError = "SetFileTransferCancelled: row not found";
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::DeleteFileTransferMeta(const std::string& logicalFileId)
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        if (!ExpectNonEmptyFileId(logicalFileId, impl_->lastError)) {
            return false;
        }
        static const char sql[] = "DELETE FROM mm2_file_transfer WHERE logical_file_id = ?;";
        sqlite3_stmt* stmt      = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, logicalFileId.c_str(), static_cast<int>(logicalFileId.size()), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) {
            impl_->lastError = sqlite3_errmsg(impl_->db);
            return false;
        }
        impl_->lastError.clear();
        return true;
    }

    bool SqliteMetadataDb::RunVacuum()
    {
        if (!IsOpen()) {
            impl_->lastError = "database not open";
            return false;
        }
        char* errMsg = nullptr;
        if (sqlite3_exec(impl_->db, "VACUUM;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            impl_->lastError = errMsg ? errMsg : sqlite3_errmsg(impl_->db);
            sqlite3_free(errMsg);
            return false;
        }
        sqlite3_free(errMsg);
        impl_->lastError.clear();
        return true;
    }

} // namespace ZChatIM::mm2
