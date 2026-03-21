// Metadata index SQLite binding (see docs/02-Core/03-Storage.md).
// Production note: 03-Storage §4.2 mentions SQLCipher; this build uses vanilla SQLite until key management is wired.

#include "mm2/storage/SqliteMetadataDb.h"
#include "Types.h"

#include <sqlite3.h>

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


        constexpr int kSchemaUserVersion = 1;

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
            ");\n";

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

} // namespace ZChatIM::mm2
