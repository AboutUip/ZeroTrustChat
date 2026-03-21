// MM2 实现：文件分片等路径打通 ZdbManager + SqliteMetadataDb + StorageIntegrityManager（见 MM2.h 注释）。
//
// StoreFileChunk：WriteData 成功后若 GetFileStatus / UpsertZdbFile / ComputeSha256 / RecordDataBlockHash 失败，
// 调用 RevertFailedPutDataBlockUnlocked（与 PutDataBlockBlobUnlocked 一致），减少孤儿尾块与索引不一致。

#include "mm2/MM2.h"
#include "mm2/crypto/Sha256.h"
#include "mm2/storage/Crypto.h"

#include <chrono>
#include "Types.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <fstream>
#include <filesystem>
#include <string>
#include <utility>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <wincrypt.h>
#endif

namespace ZChatIM::mm2 {

    namespace {

        void SecureZeroBytes(void* ptr, size_t len)
        {
            if (ptr == nullptr || len == 0) {
                return;
            }
#ifdef _WIN32
            SecureZeroMemory(ptr, len);
#else
            auto* v = static_cast<volatile unsigned char*>(ptr);
            while (len != 0) {
                *v++ = 0;
                --len;
            }
#endif
        }

#ifdef _WIN32
        // indexDir 常来自 path::string()（ACP）；ofstream(path) 在部分环境下打开失败，而 SQLite 已用宽路径成功。
        // 此处用 path 原生宽文件名与 CRT 宽打开，与元数据库路径语义一致。
        //
        // mm2_message_key.bin 格式（Windows）：
        // - 旧版：恰好 32 字节明文 AES-256 密钥（仍可读；首次成功加载后会尽力改写为 ZMK1）。
        // - ZMK1：魔数 "ZMK\1" + uint32 LE blobLen + CryptProtectData 输出（当前用户 DPAPI）。
        static constexpr uint8_t kMm2MessageKeyMagic[4] = {'Z', 'M', 'K', 1};

        static bool ReadAllBytesMessageKeyFileWin32(const std::filesystem::path& keyPath, std::vector<uint8_t>& fileBytes, std::string& errOut)
        {
            fileBytes.clear();
            FILE* f = nullptr;
#    if defined(_MSC_VER)
            if (_wfopen_s(&f, keyPath.native().c_str(), L"rb") != 0 || f == nullptr) {
                errOut = "cannot open mm2_message_key.bin for read";
                return false;
            }
#    else
            f = _wfopen(keyPath.native().c_str(), L"rb");
            if (f == nullptr) {
                errOut = "cannot open mm2_message_key.bin for read";
                return false;
            }
#    endif
            if (std::fseek(f, 0, SEEK_END) != 0) {
                std::fclose(f);
                errOut = "mm2_message_key.bin seek end failed";
                return false;
            }
            const long szl = std::ftell(f);
            if (szl < 0) {
                std::fclose(f);
                errOut = "mm2_message_key.bin size query failed";
                return false;
            }
            if (std::fseek(f, 0, SEEK_SET) != 0) {
                std::fclose(f);
                errOut = "mm2_message_key.bin seek set failed";
                return false;
            }
            if (szl == 0) {
                std::fclose(f);
                errOut = "mm2_message_key.bin is empty";
                return false;
            }
            fileBytes.resize(static_cast<size_t>(szl));
            const size_t n = std::fread(fileBytes.data(), 1, fileBytes.size(), f);
            std::fclose(f);
            if (n != fileBytes.size()) {
                errOut = "mm2_message_key.bin read incomplete";
                return false;
            }
            return true;
        }

        static bool UnprotectMm2MessageKeyBlob(const uint8_t* blob, DWORD cbBlob, std::vector<uint8_t>& outKey32, std::string& errOut)
        {
            outKey32.clear();
            DATA_BLOB in{cbBlob, const_cast<BYTE*>(blob)};
            DATA_BLOB plain{};
            if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &plain)) {
                errOut = "CryptUnprotectData failed for mm2_message_key.bin";
                return false;
            }
            struct LocalFreeScope {
                DATA_BLOB* b;
                ~LocalFreeScope()
                {
                    if (b != nullptr && b->pbData != nullptr) {
                        LocalFree(b->pbData);
                        b->pbData = nullptr;
                    }
                }
            } freePlain{&plain};
            if (plain.cbData != ZChatIM::CRYPTO_KEY_SIZE) {
                errOut = "mm2_message_key.bin DPAPI payload size is not 32 bytes";
                return false;
            }
            outKey32.assign(plain.pbData, plain.pbData + ZChatIM::CRYPTO_KEY_SIZE);
            return true;
        }

        bool ReadMessageKeyFileWin32(const std::filesystem::path& keyPath, std::vector<uint8_t>& outKey, std::string& errOut)
        {
            outKey.clear();
            std::vector<uint8_t> raw;
            if (!ReadAllBytesMessageKeyFileWin32(keyPath, raw, errOut)) {
                return false;
            }
            if (raw.size() == ZChatIM::CRYPTO_KEY_SIZE) {
                outKey = std::move(raw);
                return true;
            }
            if (raw.size() < 4U + sizeof(uint32_t)) {
                errOut = "mm2_message_key.bin: file too short for ZMK1 format";
                return false;
            }
            if (std::memcmp(raw.data(), kMm2MessageKeyMagic, sizeof(kMm2MessageKeyMagic)) != 0) {
                errOut = "mm2_message_key.bin: unknown format (not 32-byte legacy or ZMK1)";
                return false;
            }
            const uint32_t blobLen = static_cast<uint32_t>(raw[4]) | (static_cast<uint32_t>(raw[5]) << 8U)
                | (static_cast<uint32_t>(raw[6]) << 16U) | (static_cast<uint32_t>(raw[7]) << 24U);
            const size_t expected = 8U + static_cast<size_t>(blobLen);
            if (expected != raw.size()) {
                errOut = "mm2_message_key.bin: ZMK1 blob length mismatch";
                return false;
            }
            return UnprotectMm2MessageKeyBlob(raw.data() + 8, static_cast<DWORD>(blobLen), outKey, errOut);
        }

        bool WriteMessageKeyFileWin32(const std::filesystem::path& keyPath, const uint8_t* data, std::string& errOut)
        {
            DATA_BLOB in{static_cast<DWORD>(ZChatIM::CRYPTO_KEY_SIZE), const_cast<BYTE*>(data)};
            DATA_BLOB enc{};
            if (!CryptProtectData(
                    &in,
                    L"ZChatIM mm2_message_key",
                    nullptr,
                    nullptr,
                    nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN,
                    &enc)) {
                errOut = "CryptProtectData failed for mm2_message_key.bin";
                return false;
            }
            struct LocalFreeScope {
                DATA_BLOB* b;
                ~LocalFreeScope()
                {
                    if (b != nullptr && b->pbData != nullptr) {
                        LocalFree(b->pbData);
                        b->pbData = nullptr;
                    }
                }
            } freeEnc{&enc};

            std::vector<uint8_t> file;
            file.reserve(8U + static_cast<size_t>(enc.cbData));
            file.insert(file.end(), kMm2MessageKeyMagic, kMm2MessageKeyMagic + sizeof(kMm2MessageKeyMagic));
            const uint32_t le = static_cast<uint32_t>(enc.cbData);
            file.push_back(static_cast<uint8_t>(le & 0xFFU));
            file.push_back(static_cast<uint8_t>((le >> 8U) & 0xFFU));
            file.push_back(static_cast<uint8_t>((le >> 16U) & 0xFFU));
            file.push_back(static_cast<uint8_t>((le >> 24U) & 0xFFU));
            file.insert(file.end(), enc.pbData, enc.pbData + enc.cbData);

            FILE* f = nullptr;
#    if defined(_MSC_VER)
            if (_wfopen_s(&f, keyPath.native().c_str(), L"wb") != 0 || f == nullptr) {
                errOut = "cannot open mm2_message_key.bin for write";
                return false;
            }
#    else
            f = _wfopen(keyPath.native().c_str(), L"wb");
            if (f == nullptr) {
                errOut = "cannot open mm2_message_key.bin for write";
                return false;
            }
#    endif
            const size_t w = std::fwrite(file.data(), 1, file.size(), f);
            std::fclose(f);
            if (w != file.size()) {
                errOut = "writing mm2_message_key.bin failed (incomplete write)";
                return false;
            }
            return true;
        }
#endif

        std::vector<uint8_t> EncodeQueryMessageRow(const std::vector<uint8_t>& msgId, const std::vector<uint8_t>& payload)
        {
            if (msgId.size() != ZChatIM::MESSAGE_ID_SIZE) {
                return {};
            }
            if (payload.size() > static_cast<size_t>(UINT32_MAX)) {
                return {};
            }
            const uint32_t len = static_cast<uint32_t>(payload.size());
            std::vector<uint8_t> row;
            row.reserve(ZChatIM::MESSAGE_ID_SIZE + 4U + payload.size());
            row.insert(row.end(), msgId.begin(), msgId.end());
            row.push_back(static_cast<uint8_t>((len >> 24) & 0xFFU));
            row.push_back(static_cast<uint8_t>((len >> 16) & 0xFFU));
            row.push_back(static_cast<uint8_t>((len >> 8) & 0xFFU));
            row.push_back(static_cast<uint8_t>(len & 0xFFU));
            row.insert(row.end(), payload.begin(), payload.end());
            return row;
        }

        std::vector<uint8_t> MakeFileChunkDataId(const std::string& fileId, uint32_t chunkIndex)
        {
            std::vector<uint8_t> in;
            in.reserve(fileId.size() + 4U);
            in.assign(fileId.begin(), fileId.end());
            in.push_back(static_cast<uint8_t>(chunkIndex & 0xFFU));
            in.push_back(static_cast<uint8_t>((chunkIndex >> 8) & 0xFFU));
            in.push_back(static_cast<uint8_t>((chunkIndex >> 16) & 0xFFU));
            in.push_back(static_cast<uint8_t>((chunkIndex >> 24) & 0xFFU));
            uint8_t full[ZChatIM::SHA256_SIZE]{};
            if (!crypto::Sha256(in.data(), in.size(), full)) {
                return {};
            }
            return std::vector<uint8_t>(full, full + ZChatIM::MESSAGE_ID_SIZE);
        }

    } // namespace

    std::atomic<uint64_t> MM2::s_sequence{1};

    MM2& MM2::Instance()
    {
        static MM2 s_instance;
        return s_instance;
    }

    MM2::MM2()
        : m_initialized(false)
    {
        m_storageIntegrityManager.Bind(nullptr);
    }

    MM2::~MM2()
    {
        Cleanup();
    }

    void MM2::SetLastError(std::string_view message) const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.assign(message.begin(), message.end());
    }

    std::string MM2::LastError() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_lastError;
    }

    void MM2::CleanupUnlocked()
    {
        m_messageQueryManager.SetOwner(nullptr);
        // 与 EnsureMessageCryptoReadyUnlocked / Crypto::Init 成对；并擦除内存中的消息密钥材料。
        if (!m_messageStorageKey.empty()) {
            SecureZeroBytes(m_messageStorageKey.data(), m_messageStorageKey.size());
            m_messageStorageKey.clear();
        }
        Crypto::Cleanup();
        m_zdbManager.Cleanup();
        m_metadataDb.Close();
        m_storageIntegrityManager.Bind(nullptr);
        m_sessionCache.clear();
        m_initialized = false;
    }

    void MM2::Cleanup()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        CleanupUnlocked();
        m_dataDir.clear();
        m_indexDir.clear();
        m_metadataDbPath.clear();
    }

    bool MM2::Initialize(const std::string& dataDir, const std::string& indexDir)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();

        if (m_initialized) {
            CleanupUnlocked();
        }

        if (dataDir.empty() || indexDir.empty()) {
            SetLastError("Initialize: dataDir and indexDir must be non-empty");
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }

        m_dataDir  = dataDir;
        m_indexDir = indexDir;

        std::error_code ec;
        std::filesystem::create_directories(dataDir, ec);
        if (ec) {
            SetLastError(std::string("create_directories(dataDir) failed: ") + ec.message());
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        ec.clear();
        std::filesystem::create_directories(indexDir, ec);
        if (ec) {
            SetLastError(std::string("create_directories(indexDir) failed: ") + ec.message());
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }

        m_metadataDbPath = std::filesystem::path(indexDir) / "zchatim_metadata.db";

        if (!m_zdbManager.Initialize(dataDir)) {
            SetLastError(m_zdbManager.LastError());
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        if (!m_metadataDb.Open(m_metadataDbPath)) {
            SetLastError(m_metadataDb.LastError());
            m_zdbManager.Cleanup();
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        if (!m_metadataDb.InitializeSchema()) {
            SetLastError(m_metadataDb.LastError());
            m_metadataDb.Close();
            m_zdbManager.Cleanup();
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        m_storageIntegrityManager.Bind(&m_metadataDb);
        // 文件分片（StoreFileChunk）仅依赖 Zdb + SQLite + SIM，不要求消息密钥；消息加解密在 StoreMessage / GetSessionMessages / MessageQueryManager::List* 等路径再 EnsureMessageCryptoReadyUnlocked。
        m_initialized = true;
        m_messageQueryManager.SetOwner(this);
        return true;
    }

    bool MM2::StoreFileChunk(const std::string& fileId, uint32_t chunkIndex, const std::vector<uint8_t>& data)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (fileId.empty()) {
            SetLastError("empty fileId");
            return false;
        }
        if (chunkIndex > static_cast<uint32_t>(INT_MAX)) {
            SetLastError("chunkIndex too large");
            return false;
        }
        if (data.size() > ZChatIM::ZDB_MAX_WRITE_SIZE) {
            SetLastError("chunk exceeds ZDB_MAX_WRITE_SIZE");
            return false;
        }

        const std::vector<uint8_t> dataId = MakeFileChunkDataId(fileId, chunkIndex);
        if (dataId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("file chunk dataId derivation failed");
            return false;
        }
        const int32_t cidx = static_cast<int32_t>(chunkIndex);

        if (m_metadataDb.DataBlockExists(dataId, cidx)) {
            std::string oldZdb;
            uint64_t    oldOff = 0;
            uint64_t    oldLen = 0;
            uint8_t     oldSha[ZChatIM::SHA256_SIZE]{};
            if (!m_metadataDb.GetDataBlock(dataId, cidx, oldZdb, oldOff, oldLen, oldSha)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            if (!m_zdbManager.OpenFile(oldZdb)) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
            if (oldLen > static_cast<uint64_t>(SIZE_MAX)) {
                SetLastError("stored chunk length invalid");
                return false;
            }
            if (!m_zdbManager.DeleteData(oldZdb, oldOff, static_cast<size_t>(oldLen))) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
        }

        const std::string dataIdBin(dataId.begin(), dataId.end());
        std::string       zdbFileId;
        uint64_t          offset = 0;
        if (!m_zdbManager.WriteData(dataIdBin, data.data(), data.size(), zdbFileId, offset)) {
            SetLastError(m_zdbManager.LastError());
            return false;
        }

        size_t used = 0, avail = 0;
        if (!m_zdbManager.GetFileStatus(zdbFileId, used, avail)) {
            SetLastError("GetFileStatus failed after write");
            (void)RevertFailedPutDataBlockUnlocked(dataId, cidx, zdbFileId, offset, data.size());
            return false;
        }
        if (!m_metadataDb.UpsertZdbFile(zdbFileId, ZChatIM::ZDB_FILE_SIZE, static_cast<uint64_t>(used))) {
            SetLastError(m_metadataDb.LastError());
            (void)RevertFailedPutDataBlockUnlocked(dataId, cidx, zdbFileId, offset, data.size());
            return false;
        }

        uint8_t sha[ZChatIM::SHA256_SIZE]{};
        if (!m_storageIntegrityManager.ComputeSha256(data.data(), data.size(), sha)) {
            SetLastError(m_storageIntegrityManager.LastError());
            (void)RevertFailedPutDataBlockUnlocked(dataId, cidx, zdbFileId, offset, data.size());
            return false;
        }
        if (!m_storageIntegrityManager.RecordDataBlockHash(dataId, chunkIndex, zdbFileId, offset,
                static_cast<uint64_t>(data.size()), sha)) {
            SetLastError(m_storageIntegrityManager.LastError());
            (void)RevertFailedPutDataBlockUnlocked(dataId, cidx, zdbFileId, offset, data.size());
            return false;
        }
        return true;
    }

    bool MM2::GetFileChunk(const std::string& fileId, uint32_t chunkIndex, std::vector<uint8_t>& outData)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        outData.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (fileId.empty()) {
            SetLastError("empty fileId");
            return false;
        }
        if (chunkIndex > static_cast<uint32_t>(INT_MAX)) {
            SetLastError("chunkIndex too large");
            return false;
        }

        const std::vector<uint8_t> dataId = MakeFileChunkDataId(fileId, chunkIndex);
        if (dataId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("file chunk dataId derivation failed");
            return false;
        }
        const int32_t cidx = static_cast<int32_t>(chunkIndex);

        std::string zdbFileId;
        uint64_t    off = 0;
        uint64_t    len = 0;
        uint8_t     storedSha[ZChatIM::SHA256_SIZE]{};
        if (!m_metadataDb.GetDataBlock(dataId, cidx, zdbFileId, off, len, storedSha)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (len > static_cast<uint64_t>(SIZE_MAX)) {
            SetLastError("chunk length invalid");
            return false;
        }

        if (!m_zdbManager.OpenFile(zdbFileId)) {
            SetLastError(m_zdbManager.LastError());
            return false;
        }
        outData.resize(static_cast<size_t>(len));
        if (!m_zdbManager.ReadData(zdbFileId, off, outData.data(), outData.size())) {
            SetLastError(m_zdbManager.LastError());
            outData.clear();
            return false;
        }

        uint8_t computed[ZChatIM::SHA256_SIZE]{};
        if (!m_storageIntegrityManager.ComputeSha256(outData.data(), outData.size(), computed)) {
            SetLastError(m_storageIntegrityManager.LastError());
            outData.clear();
            return false;
        }
        bool match = false;
        if (!m_storageIntegrityManager.VerifyDataBlockHash(dataId, chunkIndex, computed, match) || !match) {
            SetLastError("GetFileChunk: sha256 verify mismatch or db error");
            outData.clear();
            return false;
        }
        return true;
    }

    bool MM2::LoadOrCreateMessageStorageKeyUnlocked()
    {
        m_messageStorageKey.clear();
        if (m_indexDir.empty()) {
            SetLastError("LoadOrCreateMessageStorageKey: indexDir empty");
            return false;
        }
        namespace fs = std::filesystem;
        const fs::path keyPath = fs::path(m_indexDir) / "mm2_message_key.bin";
        std::error_code ec;
        const bool      keyFilePresent = fs::exists(keyPath, ec);
        if (ec) {
            SetLastError(std::string("LoadOrCreateMessageStorageKey: exists(keyPath) failed: ") + ec.message());
            return false;
        }
        if (keyFilePresent) {
#ifdef _WIN32
            std::error_code      ecFsz;
            const std::uintmax_t fszBefore = fs::file_size(keyPath, ecFsz);
            std::vector<uint8_t> key;
            std::string          readErr;
            if (!ReadMessageKeyFileWin32(keyPath, key, readErr)) {
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + readErr);
                return false;
            }
            m_messageStorageKey = std::move(key);
            // 旧版 32 字节明文 → 尽力改写为 ZMK1（DPAPI）；失败不阻止本次会话（例如目录只读）。
            if (!ecFsz && fszBefore == ZChatIM::CRYPTO_KEY_SIZE) {
                std::string migErr;
                (void)WriteMessageKeyFileWin32(keyPath, m_messageStorageKey.data(), migErr);
            }
            return true;
#else
            std::ifstream in(keyPath, std::ios::binary);
            if (!in) {
                SetLastError("LoadOrCreateMessageStorageKey: cannot open mm2_message_key.bin for read");
                return false;
            }
            std::vector<uint8_t> key(ZChatIM::CRYPTO_KEY_SIZE);
            in.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
            if (in.gcount() != static_cast<std::streamsize>(ZChatIM::CRYPTO_KEY_SIZE)) {
                SetLastError("LoadOrCreateMessageStorageKey: key file is not exactly 32 bytes");
                return false;
            }
            if (in.peek() != std::ifstream::traits_type::eof()) {
                SetLastError("LoadOrCreateMessageStorageKey: key file has trailing bytes");
                return false;
            }
            m_messageStorageKey = std::move(key);
            return true;
#endif
        }
        std::vector<uint8_t> key = Crypto::GenerateKey(ZChatIM::CRYPTO_KEY_SIZE);
        if (key.size() != ZChatIM::CRYPTO_KEY_SIZE) {
            SetLastError("LoadOrCreateMessageStorageKey: Crypto::GenerateKey failed (secure random)");
            return false;
        }
#ifdef _WIN32
        std::string writeErr;
        if (!WriteMessageKeyFileWin32(keyPath, key.data(), writeErr)) {
            SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + writeErr);
            return false;
        }
#else
        std::ofstream out(keyPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            SetLastError("LoadOrCreateMessageStorageKey: cannot open mm2_message_key.bin for write");
            return false;
        }
        out.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
        if (!out) {
            SetLastError("LoadOrCreateMessageStorageKey: writing mm2_message_key.bin failed");
            return false;
        }
#endif
        m_messageStorageKey = std::move(key);
        return true;
    }

    bool MM2::EnsureMessageCryptoReadyUnlocked()
    {
        if (m_messageStorageKey.size() == ZChatIM::CRYPTO_KEY_SIZE) {
            return true;
        }
        if (!Crypto::Init()) {
            SetLastError("Crypto::Init failed");
            return false;
        }
        if (!LoadOrCreateMessageStorageKeyUnlocked()) {
            // LoadOrCreateMessageStorageKeyUnlocked 已 SetLastError 具体原因；此处仅在没有详情时兜底。
            if (LastError().empty()) {
                SetLastError("message storage key setup failed");
            }
            return false;
        }
        return true;
    }

    bool MM2::RevertFailedPutDataBlockUnlocked(
        const std::vector<uint8_t>& dataId16,
        int32_t                     chunkIdx,
        const std::string&          zdbFileId,
        uint64_t                    offset,
        size_t                      blobLen)
    {
        if (zdbFileId.empty()) {
            SetLastError("revert: empty zdbFileId");
            return false;
        }
        if (blobLen > 0) {
            if (offset > static_cast<uint64_t>(SIZE_MAX) || blobLen > static_cast<size_t>(SIZE_MAX)
                || offset + static_cast<uint64_t>(blobLen) < offset) {
                SetLastError("revert: offset/length invalid");
                return false;
            }
            if (!m_zdbManager.OpenFile(zdbFileId)) {
                SetLastError(std::string("revert: OpenFile: ") + m_zdbManager.LastError());
                return false;
            }
            if (!m_zdbManager.DeleteData(zdbFileId, offset, blobLen)) {
                SetLastError(std::string("revert: DeleteData: ") + m_zdbManager.LastError());
                return false;
            }
        }
        if (m_metadataDb.DataBlockExists(dataId16, chunkIdx)) {
            if (!m_metadataDb.DeleteDataBlock(dataId16, chunkIdx)) {
                SetLastError(std::string("revert: DeleteDataBlock: ") + m_metadataDb.LastError());
                return false;
            }
        }
        size_t used = 0, avail = 0;
        if (!m_zdbManager.GetFileStatus(zdbFileId, used, avail)) {
            SetLastError(std::string("revert: GetFileStatus: ") + m_zdbManager.LastError());
            return false;
        }
        if (!m_metadataDb.UpsertZdbFile(zdbFileId, ZChatIM::ZDB_FILE_SIZE, static_cast<uint64_t>(used))) {
            SetLastError(std::string("revert: UpsertZdbFile: ") + m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::PutDataBlockBlobUnlocked(
        const std::vector<uint8_t>& dataId16,
        int32_t                     chunkIdx,
        const std::vector<uint8_t>& blob)
    {
        if (dataId16.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("PutDataBlockBlob: dataId must be MESSAGE_ID_SIZE bytes");
            return false;
        }
        if (chunkIdx < 0) {
            SetLastError("PutDataBlockBlob: chunkIdx invalid");
            return false;
        }
        if (blob.size() > ZChatIM::ZDB_MAX_WRITE_SIZE) {
            SetLastError("blob exceeds ZDB_MAX_WRITE_SIZE");
            return false;
        }

        if (m_metadataDb.DataBlockExists(dataId16, chunkIdx)) {
            std::string oldZdb;
            uint64_t    oldOff = 0;
            uint64_t    oldLen = 0;
            uint8_t     oldSha[ZChatIM::SHA256_SIZE]{};
            if (!m_metadataDb.GetDataBlock(dataId16, chunkIdx, oldZdb, oldOff, oldLen, oldSha)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            if (!m_zdbManager.OpenFile(oldZdb)) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
            if (oldLen > static_cast<uint64_t>(SIZE_MAX)) {
                SetLastError("stored blob length invalid");
                return false;
            }
            if (!m_zdbManager.DeleteData(oldZdb, oldOff, static_cast<size_t>(oldLen))) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
        }

        const std::string          dataIdBin(dataId16.begin(), dataId16.end());
        std::string                zdbFileId;
        uint64_t                   offset = 0;
        const uint8_t*             p      = blob.empty() ? nullptr : blob.data();
        if (!m_zdbManager.WriteData(dataIdBin, p, blob.size(), zdbFileId, offset)) {
            SetLastError(m_zdbManager.LastError());
            return false;
        }

        size_t used = 0, avail = 0;
        if (!m_zdbManager.GetFileStatus(zdbFileId, used, avail)) {
            SetLastError("GetFileStatus failed after write");
            (void)RevertFailedPutDataBlockUnlocked(dataId16, chunkIdx, zdbFileId, offset, blob.size());
            return false;
        }
        if (!m_metadataDb.UpsertZdbFile(zdbFileId, ZChatIM::ZDB_FILE_SIZE, static_cast<uint64_t>(used))) {
            SetLastError(m_metadataDb.LastError());
            (void)RevertFailedPutDataBlockUnlocked(dataId16, chunkIdx, zdbFileId, offset, blob.size());
            return false;
        }

        uint8_t sha[ZChatIM::SHA256_SIZE]{};
        if (!m_storageIntegrityManager.ComputeSha256(p, blob.size(), sha)) {
            SetLastError(m_storageIntegrityManager.LastError());
            (void)RevertFailedPutDataBlockUnlocked(dataId16, chunkIdx, zdbFileId, offset, blob.size());
            return false;
        }
        const uint32_t ciu = static_cast<uint32_t>(chunkIdx);
        if (!m_storageIntegrityManager.RecordDataBlockHash(dataId16, ciu, zdbFileId, offset,
                static_cast<uint64_t>(blob.size()), sha)) {
            SetLastError(m_storageIntegrityManager.LastError());
            (void)RevertFailedPutDataBlockUnlocked(dataId16, chunkIdx, zdbFileId, offset, blob.size());
            return false;
        }
        return true;
    }

    bool MM2::GetDataBlockBlobUnlocked(
        const std::vector<uint8_t>& dataId16,
        int32_t                     chunkIdx,
        std::vector<uint8_t>&       outBlob)
    {
        outBlob.clear();
        if (dataId16.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("GetDataBlockBlob: dataId must be MESSAGE_ID_SIZE bytes");
            return false;
        }
        if (chunkIdx < 0) {
            SetLastError("GetDataBlockBlob: chunkIdx invalid");
            return false;
        }

        std::string zdbFileId;
        uint64_t    off = 0;
        uint64_t    len = 0;
        uint8_t     storedSha[ZChatIM::SHA256_SIZE]{};
        if (!m_metadataDb.GetDataBlock(dataId16, chunkIdx, zdbFileId, off, len, storedSha)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (len > static_cast<uint64_t>(SIZE_MAX)) {
            SetLastError("blob length invalid");
            return false;
        }

        if (!m_zdbManager.OpenFile(zdbFileId)) {
            SetLastError(m_zdbManager.LastError());
            return false;
        }
        outBlob.resize(static_cast<size_t>(len));
        if (!m_zdbManager.ReadData(zdbFileId, off, outBlob.data(), outBlob.size())) {
            SetLastError(m_zdbManager.LastError());
            outBlob.clear();
            return false;
        }

        uint8_t computed[ZChatIM::SHA256_SIZE]{};
        if (!m_storageIntegrityManager.ComputeSha256(outBlob.data(), outBlob.size(), computed)) {
            SetLastError(m_storageIntegrityManager.LastError());
            outBlob.clear();
            return false;
        }
        const uint32_t ciu = static_cast<uint32_t>(chunkIdx);
        bool           match = false;
        if (!m_storageIntegrityManager.VerifyDataBlockHash(dataId16, ciu, computed, match) || !match) {
            SetLastError("GetDataBlockBlob: sha256 verify mismatch or db error");
            outBlob.clear();
            return false;
        }
        return true;
    }

    bool MM2::StoreMessage(
        const std::vector<uint8_t>& sessionId,
        const std::vector<uint8_t>& payload,
        std::vector<uint8_t>&       outMessageId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        outMessageId.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("StoreMessage: sessionId must be USER_ID_SIZE (16) bytes");
            return false;
        }
        if (!EnsureMessageCryptoReadyUnlocked()) {
            return false;
        }

        constexpr size_t kOverhead = ZChatIM::NONCE_SIZE + ZChatIM::AUTH_TAG_SIZE;
        if (payload.size() > ZChatIM::ZDB_MAX_WRITE_SIZE - kOverhead) {
            SetLastError("StoreMessage: payload too large for single encrypted blob");
            return false;
        }

        std::vector<uint8_t> messageId = Crypto::GenerateSecureRandom(ZChatIM::MESSAGE_ID_SIZE);
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("GenerateSecureRandom failed");
            return false;
        }
        std::vector<uint8_t> nonce = Crypto::GenerateNonce(ZChatIM::NONCE_SIZE);
        if (nonce.size() != ZChatIM::NONCE_SIZE) {
            SetLastError("GenerateNonce failed");
            return false;
        }

        std::vector<uint8_t> ciphertext;
        uint8_t              tag[ZChatIM::AUTH_TAG_SIZE]{};
        if (!Crypto::EncryptMessage(
                payload.empty() ? nullptr : payload.data(),
                payload.size(),
                m_messageStorageKey.data(),
                m_messageStorageKey.size(),
                nonce.data(),
                nonce.size(),
                ciphertext,
                tag,
                sizeof(tag))) {
            SetLastError("Crypto::EncryptMessage failed");
            return false;
        }

        std::vector<uint8_t> blob;
        blob.reserve(nonce.size() + ciphertext.size() + sizeof(tag));
        blob.insert(blob.end(), nonce.begin(), nonce.end());
        blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());
        blob.insert(blob.end(), tag, tag + sizeof(tag));

        if (!PutDataBlockBlobUnlocked(messageId, 0, blob)) {
            return false;
        }
        if (!m_metadataDb.InsertImMessage(sessionId, messageId)) {
            const std::string imErr = m_metadataDb.LastError();
            std::string       zf;
            uint64_t          off = 0;
            uint64_t          len = 0;
            uint8_t           rowSha[ZChatIM::SHA256_SIZE]{};
            if (m_metadataDb.GetDataBlock(messageId, 0, zf, off, len, rowSha)
                && len <= static_cast<uint64_t>(SIZE_MAX)) {
                if (!RevertFailedPutDataBlockUnlocked(messageId, 0, zf, off, static_cast<size_t>(len))) {
                    const std::string revErr = m_lastError;
                    SetLastError(imErr + " | compensation failed: " + revErr);
                    return false;
                }
                SetLastError(imErr + " (im_messages insert failed; zdb tail scrubbed + data_blocks row dropped)");
            } else {
                SetLastError(imErr + " (im_messages insert failed; could not locate data_block for compensation)");
            }
            return false;
        }
        outMessageId = std::move(messageId);
        return true;
    }

    bool MM2::RetrieveMessage(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& outPayload)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        outPayload.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("RetrieveMessage: messageId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        if (!EnsureMessageCryptoReadyUnlocked()) {
            return false;
        }

        // 必须存在 im_messages 行，避免仅依赖 data_blocks 时误读非消息卷或陈旧行。
        std::vector<uint8_t> sess;
        if (!m_metadataDb.GetImMessageSession(messageId, sess)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (sess.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("RetrieveMessage: invalid session_id in im_messages");
            return false;
        }

        std::vector<uint8_t> blob;
        if (!GetDataBlockBlobUnlocked(messageId, 0, blob)) {
            return false;
        }
        constexpr size_t kOverhead = ZChatIM::NONCE_SIZE + ZChatIM::AUTH_TAG_SIZE;
        if (blob.size() < kOverhead) {
            SetLastError("RetrieveMessage: stored blob too small");
            return false;
        }

        const uint8_t* nonce = blob.data();
        const uint8_t* tag   = blob.data() + blob.size() - ZChatIM::AUTH_TAG_SIZE;
        const size_t   ctLen = blob.size() - ZChatIM::NONCE_SIZE - ZChatIM::AUTH_TAG_SIZE;
        const uint8_t* ct    = blob.data() + ZChatIM::NONCE_SIZE;

        if (!Crypto::DecryptMessage(
                ct,
                ctLen,
                m_messageStorageKey.data(),
                m_messageStorageKey.size(),
                nonce,
                ZChatIM::NONCE_SIZE,
                tag,
                ZChatIM::AUTH_TAG_SIZE,
                outPayload)) {
            SetLastError("Crypto::DecryptMessage failed");
            outPayload.clear();
            return false;
        }
        return true;
    }

    bool MM2::RecordDataBlockHash(
        const std::string& dataId,
        uint32_t           chunkIndex,
        const std::string& fileId,
        uint64_t           offset,
        uint64_t           length,
        const uint8_t      sha256[32])
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (dataId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("RecordDataBlockHash: dataId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        std::vector<uint8_t> did(dataId.begin(), dataId.end());
        if (!m_storageIntegrityManager.RecordDataBlockHash(did, chunkIndex, fileId, offset, length, sha256)) {
            SetLastError(m_storageIntegrityManager.LastError());
            return false;
        }
        return true;
    }

    bool MM2::VerifyDataBlockHash(
        const std::string& dataId,
        uint32_t           chunkIndex,
        const uint8_t      sha256[32],
        bool&              outMatch)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (dataId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            return false;
        }
        std::vector<uint8_t> did(dataId.begin(), dataId.end());
        if (!m_storageIntegrityManager.VerifyDataBlockHash(did, chunkIndex, sha256, outMatch)) {
            SetLastError(m_storageIntegrityManager.LastError());
            return false;
        }
        return true;
    }

    StorageIntegrityManager& MM2::GetStorageIntegrityManager()
    {
        return m_storageIntegrityManager;
    }

    MessageQueryManager& MM2::GetMessageQueryManager()
    {
        return m_messageQueryManager;
    }

    std::vector<std::vector<uint8_t>> MM2::InternalListMessagesForQueryManager(const std::vector<uint8_t>& sessionId, int count)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        std::vector<std::vector<uint8_t>> outRows;
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return outRows;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("ListMessages: userId/sessionId must be USER_ID_SIZE (16) bytes");
            return outRows;
        }
        if (count <= 0) {
            return outRows;
        }
        const size_t lim = (count > INT_MAX) ? static_cast<size_t>(INT_MAX) : static_cast<size_t>(count);
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> msgs;
        if (!GetSessionMessages(sessionId, lim, msgs)) {
            return outRows;
        }
        for (auto& pr : msgs) {
            std::vector<uint8_t> row = EncodeQueryMessageRow(pr.first, pr.second);
            if (row.empty()) {
                SetLastError("ListMessages: encode row failed");
                outRows.clear();
                return outRows;
            }
            outRows.push_back(std::move(row));
        }
        return outRows;
    }

    std::vector<std::vector<uint8_t>> MM2::InternalListMessagesSinceMessageIdForQueryManager(
        const std::vector<uint8_t>& sessionId,
        const std::vector<uint8_t>& lastMsgId,
        int                         count)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        std::vector<std::vector<uint8_t>> outRows;
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return outRows;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("ListMessagesSinceMessageId: userId/sessionId must be USER_ID_SIZE (16) bytes");
            return outRows;
        }
        if (count <= 0) {
            return outRows;
        }
        if (!lastMsgId.empty() && lastMsgId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("ListMessagesSinceMessageId: lastMsgId must be empty or MESSAGE_ID_SIZE (16) bytes");
            return outRows;
        }
        if (!EnsureMessageCryptoReadyUnlocked()) {
            return outRows;
        }
        const size_t lim = (count > INT_MAX) ? static_cast<size_t>(INT_MAX) : static_cast<size_t>(count);
        std::vector<std::vector<uint8_t>> ids;
        if (lastMsgId.empty()) {
            if (!m_metadataDb.ListImMessageIdsForSessionChronological(sessionId, lim, ids)) {
                SetLastError(m_metadataDb.LastError());
                return outRows;
            }
        } else {
            if (!m_metadataDb.ListImMessageIdsForSessionAfterMessageId(sessionId, lastMsgId, lim, ids)) {
                SetLastError(m_metadataDb.LastError());
                return outRows;
            }
        }
        for (const auto& mid : ids) {
            std::vector<uint8_t> pl;
            if (!RetrieveMessage(mid, pl)) {
                outRows.clear();
                return outRows;
            }
            std::vector<uint8_t> row = EncodeQueryMessageRow(mid, pl);
            if (row.empty()) {
                SetLastError("ListMessagesSinceMessageId: encode row failed");
                outRows.clear();
                return outRows;
            }
            outRows.push_back(std::move(row));
        }
        return outRows;
    }

    std::vector<std::vector<uint8_t>> MM2::InternalListMessagesSinceTimestampForQueryManager(
        const std::vector<uint8_t>& sessionId,
        uint64_t                    /*sinceTimestampMs*/,
        int                         count)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (count <= 0) {
            return {};
        }
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return {};
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("ListMessagesSinceTimestamp: userId/sessionId must be USER_ID_SIZE (16) bytes");
            return {};
        }
        SetLastError("ListMessagesSinceTimestamp: not supported (im_messages has no server timestamp column)");
        return {};
    }

    bool MM2::GetStorageStatus(size_t& totalSpace, size_t& usedSpace, size_t& availableSpace)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        totalSpace     = m_zdbManager.GetTotalSpace();
        usedSpace      = m_zdbManager.GetTotalUsedSpace();
        availableSpace = m_zdbManager.GetTotalAvailableSpace();
        return true;
    }

    size_t MM2::GetFileCount()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return 0;
        }
        return m_zdbManager.GetFileList().size();
    }

    size_t MM2::GetMessageCount()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return 0;
        }
        m_lastError.clear();
        return m_metadataDb.CountImMessages();
    }

    bool MM2::CleanupAllData()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        const std::string              dd  = m_dataDir;
        const std::filesystem::path    dbp = m_metadataDbPath;
        const std::filesystem::path    keyp =
            m_indexDir.empty() ? std::filesystem::path{} : (std::filesystem::path(m_indexDir) / "mm2_message_key.bin");
        CleanupUnlocked();

        std::error_code ec;
        if (!dd.empty()) {
            std::filesystem::directory_iterator it(dd, ec);
            if (!ec) {
                for (const auto& entry : it) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const std::string fname = entry.path().filename().string();
                    if (fname.size() >= 4 && fname.compare(fname.size() - 4, 4, ".zdb") == 0) {
                        std::filesystem::remove(entry.path(), ec);
                    }
                }
            }
        }
        if (!dbp.empty()) {
            std::filesystem::remove(dbp, ec);
        }
        if (!keyp.empty()) {
            std::filesystem::remove(keyp, ec);
        }
        m_dataDir.clear();
        m_indexDir.clear();
        m_metadataDbPath.clear();
        return true;
    }

    uint64_t MM2::GetNextSequence()
    {
        return s_sequence.fetch_add(1, std::memory_order_relaxed);
    }

    bool MM2::DeleteMessageImplUnlocked(const std::vector<uint8_t>& messageId)
    {
        const bool hasBlock = m_metadataDb.DataBlockExists(messageId, 0);
        if (hasBlock) {
            std::string zdbFileId;
            uint64_t    off = 0;
            uint64_t    len = 0;
            uint8_t     sha[ZChatIM::SHA256_SIZE]{};
            if (!m_metadataDb.GetDataBlock(messageId, 0, zdbFileId, off, len, sha)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            if (len > static_cast<uint64_t>(SIZE_MAX)) {
                SetLastError("DeleteMessage: stored length invalid");
                return false;
            }
            if (!m_zdbManager.OpenFile(zdbFileId)) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
            if (!m_zdbManager.DeleteData(zdbFileId, off, static_cast<size_t>(len))) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
        }

        if (!m_metadataDb.DeleteMessageMetadataTransaction(messageId, hasBlock)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::EraseAllChunksForLogicalFileUnlocked(const std::string& logicalFileId)
    {
        if (logicalFileId.empty()) {
            SetLastError("EraseAllChunksForLogicalFile: empty fileId");
            return false;
        }
        constexpr uint32_t kMaxChunks = 1000000U;
        for (uint32_t i = 0; i < kMaxChunks; ++i) {
            const std::vector<uint8_t> dataId = MakeFileChunkDataId(logicalFileId, i);
            if (dataId.size() != ZChatIM::MESSAGE_ID_SIZE) {
                SetLastError("file chunk dataId derivation failed");
                return false;
            }
            const int32_t cidx = static_cast<int32_t>(i);
            if (!m_metadataDb.DataBlockExists(dataId, cidx)) {
                break;
            }
            std::string zdbFileId;
            uint64_t    off = 0;
            uint64_t    len = 0;
            uint8_t     sha[ZChatIM::SHA256_SIZE]{};
            if (!m_metadataDb.GetDataBlock(dataId, cidx, zdbFileId, off, len, sha)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            if (len > static_cast<uint64_t>(SIZE_MAX)) {
                SetLastError("EraseAllChunksForLogicalFile: chunk length invalid");
                return false;
            }
            if (!m_zdbManager.OpenFile(zdbFileId)) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
            if (!m_zdbManager.DeleteData(zdbFileId, off, static_cast<size_t>(len))) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
            if (!m_metadataDb.DeleteDataBlock(dataId, cidx)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            size_t used = 0, avail = 0;
            if (!m_zdbManager.GetFileStatus(zdbFileId, used, avail)) {
                SetLastError(m_zdbManager.LastError());
                return false;
            }
            if (!m_metadataDb.UpsertZdbFile(zdbFileId, ZChatIM::ZDB_FILE_SIZE, static_cast<uint64_t>(used))) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
        }
        return true;
    }

    bool MM2::DeleteMessage(const std::vector<uint8_t>& messageId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("DeleteMessage: messageId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        if (!m_metadataDb.ImMessageExists(messageId)) {
            SetLastError("DeleteMessage: message not found (no im_messages row)");
            return false;
        }
        return DeleteMessageImplUnlocked(messageId);
    }

    bool MM2::MarkMessageRead(const std::vector<uint8_t>& messageId, uint64_t readTimestampMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("MarkMessageRead: messageId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        if (readTimestampMs > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            SetLastError("MarkMessageRead: readTimestampMs exceeds int64_t range for SQLite binding");
            return false;
        }
        if (!m_metadataDb.MarkImMessageRead(messageId, static_cast<int64_t>(readTimestampMs))) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetUnreadSessionMessages(
        const std::vector<uint8_t>& sessionId,
        size_t                      limit,
        std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outUnreadMessages)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outUnreadMessages.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("GetUnreadSessionMessages: sessionId must be USER_ID_SIZE (16) bytes");
            return false;
        }
        if (limit == 0) {
            return true;
        }
        std::vector<std::vector<uint8_t>> ids;
        if (!m_metadataDb.ListUnreadImMessageIdsForSession(sessionId, limit, ids)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        outUnreadMessages.reserve(ids.size());
        for (auto& id : ids) {
            outUnreadMessages.emplace_back(std::move(id), 0ULL);
        }
        return true;
    }

    bool MM2::StoreMessageReplyRelation(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& repliedMsgId,
        const std::vector<uint8_t>& repliedSenderId,
        const std::vector<uint8_t>& repliedContentDigest)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE || repliedMsgId.size() != ZChatIM::MESSAGE_ID_SIZE
            || repliedSenderId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("StoreMessageReplyRelation: invalid id length");
            return false;
        }
        if (!m_metadataDb.ImMessageExists(messageId)) {
            SetLastError("StoreMessageReplyRelation: message not found (no im_messages row)");
            return false;
        }
        if (!m_metadataDb.UpsertImMessageReply(messageId, repliedMsgId, repliedSenderId, repliedContentDigest)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetMessageReplyRelation(
        const std::vector<uint8_t>& messageId,
        std::vector<uint8_t>& outRepliedMsgId,
        std::vector<uint8_t>& outRepliedSenderId,
        std::vector<uint8_t>& outRepliedContentDigest)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outRepliedMsgId.clear();
        outRepliedSenderId.clear();
        outRepliedContentDigest.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("GetMessageReplyRelation: messageId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        if (!m_metadataDb.GetImMessageReply(messageId, outRepliedMsgId, outRepliedSenderId, outRepliedContentDigest)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::EditMessage(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& newEncryptedContent,
        uint64_t editTimestampSeconds,
        uint32_t newEditCount)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("EditMessage: messageId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        if (newEncryptedContent.size() > ZChatIM::ZDB_MAX_WRITE_SIZE) {
            SetLastError("EditMessage: newEncryptedContent exceeds ZDB_MAX_WRITE_SIZE");
            return false;
        }
        if (newEditCount < 1U || newEditCount > 3U) {
            SetLastError("EditMessage: newEditCount must be in [1,3]");
            return false;
        }
        if (!m_metadataDb.ImMessageExists(messageId)) {
            SetLastError("EditMessage: message not found (no im_messages row)");
            return false;
        }
        if (!PutDataBlockBlobUnlocked(messageId, 0, newEncryptedContent)) {
            return false;
        }
        if (!m_metadataDb.UpdateImMessageEditState(messageId, newEditCount, editTimestampSeconds)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetMessageEditState(
        const std::vector<uint8_t>& messageId,
        uint32_t& outEditCount,
        uint64_t& outLastEditTimeSeconds)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outEditCount           = 0;
        outLastEditTimeSeconds = 0;
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("GetMessageEditState: messageId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        if (!m_metadataDb.GetImMessageEditState(messageId, outEditCount, outLastEditTimeSeconds)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::StoreMessages(
        const std::vector<uint8_t>& sessionId,
        const std::vector<std::vector<uint8_t>>& payloads,
        std::vector<std::vector<uint8_t>>& outMessageIds)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outMessageIds.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("StoreMessages: sessionId must be USER_ID_SIZE (16) bytes");
            return false;
        }
        if (payloads.empty()) {
            return true;
        }
        outMessageIds.reserve(payloads.size());
        for (const auto& pl : payloads) {
            std::vector<uint8_t> mid;
            if (!StoreMessage(sessionId, pl, mid)) {
                const std::string batchFailReason = LastError();
                for (auto rit = outMessageIds.rbegin(); rit != outMessageIds.rend(); ++rit) {
                    (void)DeleteMessageImplUnlocked(*rit);
                }
                outMessageIds.clear();
                SetLastError(batchFailReason.empty() ? "StoreMessages: batch failed after partial success" : batchFailReason);
                return false;
            }
            outMessageIds.push_back(std::move(mid));
        }
        return true;
    }

    bool MM2::RetrieveMessages(
        const std::vector<std::vector<uint8_t>>& messageIds,
        std::vector<std::vector<uint8_t>>& outPayloads)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outPayloads.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageIds.empty()) {
            return true;
        }
        if (!EnsureMessageCryptoReadyUnlocked()) {
            return false;
        }
        outPayloads.reserve(messageIds.size());
        for (const auto& mid : messageIds) {
            std::vector<uint8_t> pl;
            if (!RetrieveMessage(mid, pl)) {
                outPayloads.clear();
                return false;
            }
            outPayloads.push_back(std::move(pl));
        }
        return true;
    }

    bool MM2::CompleteFile(const std::string& fileId, const uint8_t* sha256)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (fileId.empty()) {
            SetLastError("empty fileId");
            return false;
        }
        if (sha256 == nullptr) {
            SetLastError("CompleteFile: sha256 is null");
            return false;
        }
        crypto::Sha256Hasher hasher;
        constexpr uint32_t kMaxChunks = 1000000U;
        uint32_t           chunkCount = 0;
        for (uint32_t i = 0; i < kMaxChunks; ++i) {
            std::vector<uint8_t> chunk;
            if (!GetFileChunk(fileId, i, chunk)) {
                if (i == 0) {
                    return false;
                }
                break;
            }
            if (!hasher.Update(chunk.data(), chunk.size())) {
                SetLastError("CompleteFile: Sha256Hasher::Update failed");
                return false;
            }
            ++chunkCount;
        }
        if (chunkCount == 0) {
            SetLastError("CompleteFile: no chunks stored for fileId");
            return false;
        }
        uint8_t digest[ZChatIM::SHA256_SIZE]{};
        if (!hasher.Final(digest)) {
            SetLastError("CompleteFile: Sha256Hasher::Final failed");
            return false;
        }
        if (std::memcmp(digest, sha256, ZChatIM::SHA256_SIZE) != 0) {
            SetLastError("CompleteFile: sha256 mismatch");
            return false;
        }
        if (!m_metadataDb.SetFileTransferComplete(fileId, sha256)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        m_lastError.clear();
        return true;
    }

    bool MM2::CancelFile(const std::string& fileId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (fileId.empty()) {
            SetLastError("empty fileId");
            return false;
        }
        if (!EraseAllChunksForLogicalFileUnlocked(fileId)) {
            return false;
        }
        (void)m_metadataDb.DeleteFileTransferMeta(fileId);
        m_lastError.clear();
        return true;
    }

    bool MM2::StoreTransferResumeChunkIndex(const std::string& fileId, uint32_t chunkIndex)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertFileTransferResume(fileId, chunkIndex)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetTransferResumeChunkIndex(const std::string& fileId, uint32_t& outChunkIndex)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outChunkIndex = 0;
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetFileTransferResumeChunk(fileId, outChunkIndex)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::CleanupTransferResumeChunkIndex(const std::string& fileId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteFileTransferMeta(fileId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::StoreFriendRequest(
        const std::vector<uint8_t>& fromUserId,
        const std::vector<uint8_t>& toUserId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519,
        std::vector<uint8_t>&       outRequestId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outRequestId.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        std::vector<uint8_t> rid = Crypto::GenerateSecureRandom(ZChatIM::MESSAGE_ID_SIZE);
        if (rid.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("StoreFriendRequest: GenerateSecureRandom failed");
            return false;
        }
        if (!m_metadataDb.InsertFriendRequest(rid, fromUserId, toUserId, timestampSeconds, signatureEd25519)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        outRequestId = std::move(rid);
        return true;
    }

    bool MM2::UpdateFriendRequestStatus(
        const std::vector<uint8_t>& requestId,
        bool                        accept,
        const std::vector<uint8_t>& responderId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& /*signatureEd25519*/)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        const int32_t st = accept ? 1 : 2;
        if (!m_metadataDb.UpdateFriendRequestStatus(requestId, st, responderId, timestampSeconds)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::DeleteFriendRequest(const std::vector<uint8_t>& requestId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteFriendRequest(requestId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::CleanupExpiredFriendRequests(uint64_t nowMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        const uint64_t     nowSec = nowMs / 1000ULL;
        constexpr uint64_t kTtlSec = 30ULL * 86400ULL;
        if (!m_metadataDb.DeleteExpiredPendingFriendRequests(nowSec, kTtlSec)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::UpdateGroupName(
        const std::vector<uint8_t>& groupId,
        const std::string&          newGroupName,
        uint64_t                    updateTimeSeconds,
        const std::vector<uint8_t>& updateBy)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertGroupDisplayName(groupId, newGroupName, updateTimeSeconds, updateBy)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetGroupName(const std::vector<uint8_t>& groupId, std::string& outGroupName)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outGroupName.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetGroupDisplayName(groupId, outGroupName)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetSessionMessages(
        const std::vector<uint8_t>& sessionId,
        size_t                      limit,
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& outMessages)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outMessages.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("GetSessionMessages: sessionId must be USER_ID_SIZE (16) bytes");
            return false;
        }
        if (limit == 0) {
            return true;
        }
        if (!EnsureMessageCryptoReadyUnlocked()) {
            return false;
        }

        std::vector<std::vector<uint8_t>> ids;
        if (!m_metadataDb.ListImMessageIdsForSession(sessionId, limit, ids)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        outMessages.reserve(ids.size());
        for (const auto& mid : ids) {
            std::vector<uint8_t> pl;
            if (!RetrieveMessage(mid, pl)) {
                outMessages.clear();
                return false;
            }
            outMessages.emplace_back(mid, std::move(pl));
        }
        return true;
    }

    bool MM2::CleanupSessionMessages(const std::vector<uint8_t>& sessionId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("CleanupSessionMessages: sessionId must be USER_ID_SIZE (16) bytes");
            return false;
        }
        std::vector<std::vector<uint8_t>> ids;
        if (!m_metadataDb.ListAllImMessageIdsForSession(sessionId, ids)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        for (const auto& mid : ids) {
            if (!DeleteMessageImplUnlocked(mid)) {
                return false;
            }
        }
        return true;
    }

    bool MM2::CleanupExpiredData()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        using namespace std::chrono;
        const uint64_t     nowMs  = static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        constexpr uint64_t kTtlSec = 30ULL * 86400ULL;
        const uint64_t     nowSec = nowMs / 1000ULL;
        if (!m_metadataDb.DeleteExpiredPendingFriendRequests(nowSec, kTtlSec)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::OptimizeStorage()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.RunVacuum()) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    std::vector<uint8_t> MM2::GenerateMessageId()
    {
        return {};
    }
    std::string MM2::GenerateFileId()
    {
        return {};
    }
    bool MM2::IsMessageExpired(uint64_t)
    {
        return false;
    }
    bool MM2::IsFileExpired(uint64_t)
    {
        return false;
    }
    bool MM2::DestroyData(const std::string& fileId, uint64_t offset, size_t length)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_zdbManager.OpenFile(fileId)) {
            SetLastError(m_zdbManager.LastError());
            return false;
        }
        if (!m_zdbManager.DeleteData(fileId, offset, length)) {
            SetLastError(m_zdbManager.LastError());
            return false;
        }
        return true;
    }

} // namespace ZChatIM::mm2
