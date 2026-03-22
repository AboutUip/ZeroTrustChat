// MM2 实现：文件分片等路径打通 ZdbManager + SqliteMetadataDb + StorageIntegrityManager（见 MM2.h 注释）。
// IM（StoreMessage / List* / MarkRead / 回复 / 编辑）：仅进程内 RAM（ImRam）；元库无 im_messages / im_message_reply（user_version=11）。
//
// StoreFileChunk：WriteData 成功后若 GetFileStatus / UpsertZdbFile / ComputeSha256 / RecordDataBlockHash 失败，
// 调用 RevertFailedPutDataBlockUnlocked（与 PutDataBlockBlobUnlocked 一致），减少孤儿尾块与索引不一致。

#include "mm2/MM2.h"
#include "mm2/crypto/Sha256.h"
#include "mm2/storage/Crypto.h"
#include "common/Memory.h"

#include <chrono>
#include "Types.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <fstream>
#include <filesystem>
#include <string>
#include <utility>
#include <string_view>

namespace ZChatIM::mm2::detail::message_key_passphrase {
    bool IsZmkpV1(const std::vector<uint8_t>& raw);
    bool ParseZmkpV1(const std::vector<uint8_t>& raw, std::string_view passphraseUtf8, std::vector<uint8_t>& outMaster32, std::string& errOut);
    bool BuildZmkpV1Blob(const uint8_t master32[ZChatIM::CRYPTO_KEY_SIZE], std::string_view passphraseUtf8, std::vector<uint8_t>& outBlob, std::string& errOut);
} // namespace ZChatIM::mm2::detail::message_key_passphrase

#if !defined(_WIN32)
#    include <unistd.h>
#endif

#if defined(__APPLE__)
namespace ZChatIM::mm2::detail {
    bool Mm2DarwinGetOrCreateMessageWrapKey(const std::string& indexDirUtf8, std::vector<uint8_t>& out32, std::string& err);
    bool Mm2DarwinDeleteMessageWrapKey(const std::string& indexDirUtf8, std::string& err);
}
#endif

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

        static bool ParseMessageKeyFromRawWin32(const std::vector<uint8_t>& raw, std::vector<uint8_t>& outKey, std::string& errOut)
        {
            outKey.clear();
            if (raw.size() == ZChatIM::CRYPTO_KEY_SIZE) {
                outKey = raw;
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

        bool ReadMessageKeyFileWin32(const std::filesystem::path& keyPath, std::vector<uint8_t>& outKey, std::string& errOut)
        {
            std::vector<uint8_t> raw;
            if (!ReadAllBytesMessageKeyFileWin32(keyPath, raw, errOut)) {
                return false;
            }
            return ParseMessageKeyFromRawWin32(raw, outKey, errOut);
        }

        static bool WriteRawBytesMessageKeyFileWin32(const std::filesystem::path& keyPath, const std::vector<uint8_t>& blob, std::string& errOut)
        {
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
            const size_t w = std::fwrite(blob.data(), 1, blob.size(), f);
            std::fclose(f);
            if (w != blob.size()) {
                errOut = "writing mm2_message_key.bin failed (incomplete write)";
                return false;
            }
            return true;
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
#endif // _WIN32

#if !defined(_WIN32)
        // Unix：**ZMK2**（魔数 **ZMK\2**）= **SHA256(machine-id ‖ uid ‖ indexDir)** 派生 **32B 封装密钥** + **AES-256-GCM** 保护消息主密钥。
        // Apple：**ZMK3**（**ZMK\3**）= **Keychain** 随机 **32B 封装密钥** + **AES-256-GCM**（见 **`MM2_message_key_darwin.cpp`**）。
        // 载荷：**nonce(12) ‖ ciphertext(32) ‖ tag(16)**，与 **`Crypto::EncryptMessage`** 线格式一致。
        static constexpr uint8_t kMm2MessageKeyMagicZmk1[4] = {'Z', 'M', 'K', 1};
        static constexpr uint8_t kMm2MessageKeyMagicZmk2[4] = {'Z', 'M', 'K', 2};
        static constexpr uint8_t kMm2MessageKeyMagicZmk3[4] = {'Z', 'M', 'K', 3};
        static constexpr uint32_t kZmk23InnerPayloadBytes =
            static_cast<uint32_t>(ZChatIM::NONCE_SIZE + ZChatIM::CRYPTO_KEY_SIZE + ZChatIM::AUTH_TAG_SIZE);

        static bool ReadAllBytesMessageKeyFilePosix(const std::filesystem::path& keyPath, std::vector<uint8_t>& fileBytes, std::string& errOut)
        {
            fileBytes.clear();
            std::ifstream in(keyPath, std::ios::binary);
            if (!in) {
                errOut = "cannot open mm2_message_key.bin for read";
                return false;
            }
            in.seekg(0, std::ios::end);
            const std::streamoff sz = in.tellg();
            if (sz < 0) {
                errOut = "mm2_message_key.bin size query failed";
                return false;
            }
            in.seekg(0, std::ios::beg);
            if (sz == 0) {
                errOut = "mm2_message_key.bin is empty";
                return false;
            }
            fileBytes.resize(static_cast<size_t>(sz));
            in.read(reinterpret_cast<char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
            if (in.gcount() != static_cast<std::streamsize>(fileBytes.size())) {
                errOut = "mm2_message_key.bin read incomplete";
                fileBytes.clear();
                return false;
            }
            return true;
        }

        static void ReadMachineIdLine(std::string& out)
        {
            out.clear();
            static const char* paths[] = {"/etc/machine-id", "/var/lib/dbus/machine-id"};
            for (const char* p : paths) {
                std::ifstream f(p);
                if (!f) {
                    continue;
                }
                if (!std::getline(f, out)) {
                    out.clear();
                    continue;
                }
                while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
                    out.pop_back();
                }
                if (!out.empty()) {
                    return;
                }
            }
            out = "ZChatIM|no-machine-id";
        }

        static bool DeriveUnixWrapKeyFromMachineAndPath(const std::string& indexDirUtf8, uint8_t outWrap32[ZChatIM::CRYPTO_KEY_SIZE], std::string& errOut)
        {
            errOut.clear();
            std::string mid;
            ReadMachineIdLine(mid);
            const uint32_t uid32 = static_cast<uint32_t>(getuid());
            std::vector<uint8_t> material;
            static const char dom[] = "ZChatIM|MM2|UnixMessageKeyWrap|v2|";
            material.insert(material.end(), dom, dom + sizeof(dom) - 1U);
            material.insert(material.end(), mid.begin(), mid.end());
            material.push_back('|');
            material.push_back(static_cast<uint8_t>((uid32 >> 24) & 0xFFU));
            material.push_back(static_cast<uint8_t>((uid32 >> 16) & 0xFFU));
            material.push_back(static_cast<uint8_t>((uid32 >> 8) & 0xFFU));
            material.push_back(static_cast<uint8_t>(uid32 & 0xFFU));
            material.push_back('|');
            material.insert(material.end(), indexDirUtf8.begin(), indexDirUtf8.end());
            std::vector<uint8_t> h = Crypto::HashSha256(material.data(), material.size());
            if (h.size() != ZChatIM::CRYPTO_KEY_SIZE) {
                errOut = "DeriveUnixWrapKey: HashSha256 failed";
                return false;
            }
            std::memcpy(outWrap32, h.data(), ZChatIM::CRYPTO_KEY_SIZE);
            return true;
        }

        static bool BuildZmk23File(
            const uint8_t                     magic[4],
            const uint8_t                     wrapKey[ZChatIM::CRYPTO_KEY_SIZE],
            const uint8_t                     master32[ZChatIM::CRYPTO_KEY_SIZE],
            std::vector<uint8_t>&             fileOut,
            std::string&                      errOut)
        {
            fileOut.clear();
            errOut.clear();
            std::vector<uint8_t> nonce = Crypto::GenerateNonce(ZChatIM::NONCE_SIZE);
            if (nonce.size() != ZChatIM::NONCE_SIZE) {
                errOut = "BuildZmk23File: GenerateNonce failed";
                return false;
            }
            std::vector<uint8_t> ciphertext;
            uint8_t              tag[ZChatIM::AUTH_TAG_SIZE]{};
            if (!Crypto::EncryptMessage(
                    master32,
                    ZChatIM::CRYPTO_KEY_SIZE,
                    wrapKey,
                    ZChatIM::CRYPTO_KEY_SIZE,
                    nonce.data(),
                    ZChatIM::NONCE_SIZE,
                    ciphertext,
                    tag,
                    ZChatIM::AUTH_TAG_SIZE)) {
                errOut = "BuildZmk23File: EncryptMessage failed";
                return false;
            }
            if (ciphertext.size() != ZChatIM::CRYPTO_KEY_SIZE) {
                errOut = "BuildZmk23File: ciphertext size mismatch";
                return false;
            }
            fileOut.reserve(8U + ZChatIM::NONCE_SIZE + ZChatIM::CRYPTO_KEY_SIZE + ZChatIM::AUTH_TAG_SIZE);
            fileOut.insert(fileOut.end(), magic, magic + 4U);
            const uint32_t le = kZmk23InnerPayloadBytes;
            fileOut.push_back(static_cast<uint8_t>(le & 0xFFU));
            fileOut.push_back(static_cast<uint8_t>((le >> 8U) & 0xFFU));
            fileOut.push_back(static_cast<uint8_t>((le >> 16U) & 0xFFU));
            fileOut.push_back(static_cast<uint8_t>((le >> 24U) & 0xFFU));
            fileOut.insert(fileOut.end(), nonce.begin(), nonce.end());
            fileOut.insert(fileOut.end(), ciphertext.begin(), ciphertext.end());
            fileOut.insert(fileOut.end(), tag, tag + ZChatIM::AUTH_TAG_SIZE);
            return true;
        }

        static bool ParseAndDecryptZmk23(
            const std::vector<uint8_t>& raw,
            const uint8_t               expectMagic[4],
            const uint8_t               wrapKey[ZChatIM::CRYPTO_KEY_SIZE],
            std::vector<uint8_t>&       outMaster32,
            std::string&                errOut)
        {
            outMaster32.clear();
            errOut.clear();
            const size_t need = 8U + static_cast<size_t>(kZmk23InnerPayloadBytes);
            if (raw.size() != need) {
                errOut = "mm2_message_key.bin: ZMK2/3 file size mismatch";
                return false;
            }
            if (std::memcmp(raw.data(), expectMagic, 4U) != 0) {
                errOut = "mm2_message_key.bin: magic mismatch";
                return false;
            }
            const uint32_t inner = static_cast<uint32_t>(raw[4]) | (static_cast<uint32_t>(raw[5]) << 8U)
                | (static_cast<uint32_t>(raw[6]) << 16U) | (static_cast<uint32_t>(raw[7]) << 24U);
            if (inner != kZmk23InnerPayloadBytes) {
                errOut = "mm2_message_key.bin: inner payload length invalid";
                return false;
            }
            const uint8_t* p     = raw.data() + 8U;
            const uint8_t* nonce = p;
            const uint8_t* ct    = p + ZChatIM::NONCE_SIZE;
            const uint8_t* tg    = p + ZChatIM::NONCE_SIZE + ZChatIM::CRYPTO_KEY_SIZE;
            if (!Crypto::DecryptMessage(
                    ct,
                    ZChatIM::CRYPTO_KEY_SIZE,
                    wrapKey,
                    ZChatIM::CRYPTO_KEY_SIZE,
                    nonce,
                    ZChatIM::NONCE_SIZE,
                    tg,
                    ZChatIM::AUTH_TAG_SIZE,
                    outMaster32)) {
                errOut = "mm2_message_key.bin: decrypt failed (wrong key, corrupt file, or wrong platform)";
                return false;
            }
            if (outMaster32.size() != ZChatIM::CRYPTO_KEY_SIZE) {
                errOut = "mm2_message_key.bin: decrypted length invalid";
                outMaster32.clear();
                return false;
            }
            return true;
        }

        static bool WriteMessageKeyFilePosix(
            const std::filesystem::path& keyPath,
            const std::string&           indexDirUtf8,
            const uint8_t*               master32,
            std::string&                 errOut)
        {
            errOut.clear();
            std::vector<uint8_t> fileBlob;
#if defined(__APPLE__)
            std::vector<uint8_t> wrap;
            if (!detail::Mm2DarwinGetOrCreateMessageWrapKey(indexDirUtf8, wrap, errOut)) {
                return false;
            }
            if (wrap.size() != ZChatIM::CRYPTO_KEY_SIZE) {
                errOut = "Keychain wrap key invalid size";
                return false;
            }
            if (!BuildZmk23File(kMm2MessageKeyMagicZmk3, wrap.data(), master32, fileBlob, errOut)) {
                return false;
            }
#else
            uint8_t wrap[ZChatIM::CRYPTO_KEY_SIZE]{};
            if (!DeriveUnixWrapKeyFromMachineAndPath(indexDirUtf8, wrap, errOut)) {
                return false;
            }
            if (!BuildZmk23File(kMm2MessageKeyMagicZmk2, wrap, master32, fileBlob, errOut)) {
                return false;
            }
#endif
            std::ofstream out(keyPath, std::ios::binary | std::ios::trunc);
            if (!out) {
                errOut = "cannot open mm2_message_key.bin for write";
                return false;
            }
            out.write(reinterpret_cast<const char*>(fileBlob.data()), static_cast<std::streamsize>(fileBlob.size()));
            if (!out) {
                errOut = "writing mm2_message_key.bin failed";
                return false;
            }
            return true;
        }

        static bool ParseMessageKeyFromRawPosix(
            const std::vector<uint8_t>& raw,
            const std::string&          indexDirUtf8,
            std::vector<uint8_t>&       outKey,
            std::string&                errOut)
        {
            outKey.clear();
            errOut.clear();
            if (raw.size() == ZChatIM::CRYPTO_KEY_SIZE) {
                outKey = raw;
                return true;
            }
            if (raw.size() >= 4U && std::memcmp(raw.data(), kMm2MessageKeyMagicZmk1, 4U) == 0) {
                errOut = "mm2_message_key.bin: ZMK1 (Windows DPAPI) cannot be read on this platform";
                return false;
            }
            if (raw.size() >= 4U && std::memcmp(raw.data(), kMm2MessageKeyMagicZmk3, 4U) == 0) {
#if defined(__APPLE__)
                std::vector<uint8_t> wrap;
                if (!detail::Mm2DarwinGetOrCreateMessageWrapKey(indexDirUtf8, wrap, errOut)) {
                    return false;
                }
                if (wrap.size() != ZChatIM::CRYPTO_KEY_SIZE) {
                    errOut = "Keychain wrap key invalid size";
                    return false;
                }
                return ParseAndDecryptZmk23(raw, kMm2MessageKeyMagicZmk3, wrap.data(), outKey, errOut);
#else
                errOut = "mm2_message_key.bin: ZMK3 requires macOS/iOS Keychain";
                return false;
#endif
            }
            if (raw.size() >= 4U && std::memcmp(raw.data(), kMm2MessageKeyMagicZmk2, 4U) == 0) {
                uint8_t wrap[ZChatIM::CRYPTO_KEY_SIZE]{};
                if (!DeriveUnixWrapKeyFromMachineAndPath(indexDirUtf8, wrap, errOut)) {
                    return false;
                }
                return ParseAndDecryptZmk23(raw, kMm2MessageKeyMagicZmk2, wrap, outKey, errOut);
            }
            errOut = "mm2_message_key.bin: unknown format (expected 32-byte legacy or ZMK2/ZMK3)";
            return false;
        }

        static bool ReadMessageKeyFilePosix(
            const std::filesystem::path& keyPath,
            const std::string&           indexDirUtf8,
            std::vector<uint8_t>&        outKey,
            std::string&                 errOut)
        {
            std::vector<uint8_t> raw;
            if (!ReadAllBytesMessageKeyFilePosix(keyPath, raw, errOut)) {
                return false;
            }
            return ParseMessageKeyFromRawPosix(raw, indexDirUtf8, outKey, errOut);
        }

        static bool WriteRawBytesMessageKeyFilePosix(const std::filesystem::path& keyPath, const std::vector<uint8_t>& blob, std::string& errOut)
        {
            errOut.clear();
            std::ofstream out(keyPath, std::ios::binary | std::ios::trunc);
            if (!out) {
                errOut = "cannot open mm2_message_key.bin for write";
                return false;
            }
            out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
            if (!out) {
                errOut = "writing mm2_message_key.bin failed";
                return false;
            }
            return true;
        }
#endif // !defined(_WIN32)

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

        bool BytesEq(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
        {
            return a.size() == b.size()
                && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
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
        ImRamClearUnlocked();
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

    void MM2::ImRamClearUnlocked()
    {
        m_imRamBySession.clear();
        m_imRamMsgToSession.clear();
        m_imRamReplies.clear();
    }

    void MM2::ImRamClearSessionUnlocked(const std::vector<uint8_t>& sessionId)
    {
        auto it = m_imRamBySession.find(sessionId);
        if (it == m_imRamBySession.end()) {
            return;
        }
        for (const auto& row : it->second) {
            m_imRamMsgToSession.erase(row.messageId);
            m_imRamReplies.erase(row.messageId);
        }
        m_imRamBySession.erase(it);
    }

    bool MM2::ImRamExistsUnlocked(const std::vector<uint8_t>& messageId)
    {
        return m_imRamMsgToSession.find(messageId) != m_imRamMsgToSession.end();
    }

    bool MM2::ImRamLocateUnlocked(const std::vector<uint8_t>& messageId, ImRamMessageRow** outRow)
    {
        if (outRow == nullptr) {
            return false;
        }
        *outRow = nullptr;
        auto itS = m_imRamMsgToSession.find(messageId);
        if (itS == m_imRamMsgToSession.end()) {
            return false;
        }
        auto itV = m_imRamBySession.find(itS->second);
        if (itV == m_imRamBySession.end()) {
            return false;
        }
        for (auto& row : itV->second) {
            if (BytesEq(row.messageId, messageId)) {
                *outRow = &row;
                return true;
            }
        }
        return false;
    }

    bool MM2::ImRamInsertRowUnlocked(const std::vector<uint8_t>& sessionId, ImRamMessageRow row)
    {
        if (m_imRamMsgToSession.find(row.messageId) != m_imRamMsgToSession.end()) {
            SetLastError("ImRamInsertRow: duplicate message_id");
            return false;
        }
        const std::vector<uint8_t> midCopy = row.messageId;
        m_imRamBySession[sessionId].push_back(std::move(row));
        m_imRamMsgToSession[midCopy] = sessionId;
        return true;
    }

    bool MM2::ImRamEraseUnlocked(const std::vector<uint8_t>& messageId)
    {
        auto itMap = m_imRamMsgToSession.find(messageId);
        if (itMap == m_imRamMsgToSession.end()) {
            return false;
        }
        const std::vector<uint8_t> sessionId = itMap->second;

        auto vit = m_imRamBySession.find(sessionId);
        if (vit == m_imRamBySession.end()) {
            // Stale msg→session entry (session bucket already gone).
            m_imRamMsgToSession.erase(itMap);
            m_imRamReplies.erase(messageId);
            return true;
        }
        auto& vec = vit->second;
        for (size_t i = 0; i < vec.size(); ++i) {
            if (BytesEq(vec[i].messageId, messageId)) {
                vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(i));
                if (vec.empty()) {
                    m_imRamBySession.erase(vit);
                }
                m_imRamMsgToSession.erase(messageId);
                m_imRamReplies.erase(messageId);
                return true;
            }
        }
        // Map pointed to a session that does not contain this message_id — drop stale map entry only.
        m_imRamMsgToSession.erase(itMap);
        m_imRamReplies.erase(messageId);
        return true;
    }

    bool MM2::ImRamListIdsLastNUnlocked(
        const std::vector<uint8_t>& sessionId,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outIds)
    {
        outIds.clear();
        auto it = m_imRamBySession.find(sessionId);
        if (it == m_imRamBySession.end() || limit == 0) {
            return true;
        }
        const auto& vec = it->second;
        const size_t n    = vec.size();
        const size_t take = (n > limit) ? limit : n;
        const size_t start = n - take;
        outIds.reserve(take);
        for (size_t i = start; i < n; ++i) {
            outIds.push_back(vec[i].messageId);
        }
        return true;
    }

    bool MM2::ImRamListIdsChronologicalFirstNUnlocked(
        const std::vector<uint8_t>& sessionId,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outIds)
    {
        outIds.clear();
        auto it = m_imRamBySession.find(sessionId);
        if (it == m_imRamBySession.end() || limit == 0) {
            return true;
        }
        const auto& vec = it->second;
        const size_t take = (vec.size() > limit) ? limit : vec.size();
        outIds.reserve(take);
        for (size_t i = 0; i < take; ++i) {
            outIds.push_back(vec[i].messageId);
        }
        return true;
    }

    bool MM2::ImRamListIdsSinceStoredAtUnlocked(
        const std::vector<uint8_t>& sessionId,
        int64_t                     sinceStoredAtMsInclusive,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outIds)
    {
        outIds.clear();
        auto it = m_imRamBySession.find(sessionId);
        if (it == m_imRamBySession.end() || limit == 0) {
            return true;
        }
        for (const auto& row : it->second) {
            if (row.stored_at_ms < sinceStoredAtMsInclusive) {
                continue;
            }
            outIds.push_back(row.messageId);
            if (outIds.size() >= limit) {
                break;
            }
        }
        return true;
    }

    bool MM2::ImRamListIdsAfterMessageIdUnlocked(
        const std::vector<uint8_t>& sessionId,
        const std::vector<uint8_t>& afterMessageId,
        size_t                      limit,
        std::vector<std::vector<uint8_t>>& outIds)
    {
        outIds.clear();
        auto it = m_imRamBySession.find(sessionId);
        if (it == m_imRamBySession.end() || limit == 0) {
            return true;
        }
        const auto& vec = it->second;
        size_t       j  = vec.size();
        for (size_t i = 0; i < vec.size(); ++i) {
            if (BytesEq(vec[i].messageId, afterMessageId)) {
                j = i + 1;
                break;
            }
        }
        if (j >= vec.size()) {
            return true;
        }
        for (size_t i = j; i < vec.size() && outIds.size() < limit; ++i) {
            outIds.push_back(vec[i].messageId);
        }
        return true;
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

    bool MM2::IsInitialized() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_initialized;
    }

    std::string MM2::GetDataDirUtf8() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_dataDir;
    }

    std::string MM2::GetIndexDirUtf8() const
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        return m_indexDir;
    }

    bool MM2::StoreMm1UserDataBlob(
        const std::vector<uint8_t>& userId,
        int32_t                     type,
        const std::vector<uint8_t>& data)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertMm1UserKvBlob(userId, type, data)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        m_lastError.clear();
        return true;
    }

    bool MM2::GetMm1UserDataBlob(
        const std::vector<uint8_t>& userId,
        int32_t                     type,
        std::vector<uint8_t>&       outData)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outData.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetMm1UserKvBlob(userId, type, outData)) {
            SetLastError(m_metadataDb.LastError());
            outData.clear();
            return false;
        }
        m_lastError.clear();
        return true;
    }

    bool MM2::DeleteMm1UserDataBlob(const std::vector<uint8_t>& userId, int32_t type)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        const bool removed = m_metadataDb.DeleteMm1UserKvBlob(userId, type);
        if (!removed && !m_metadataDb.LastError().empty()) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        m_lastError.clear();
        return removed;
    }

    bool MM2::Initialize(const std::string& dataDir, const std::string& indexDir)
    {
        return Initialize(dataDir, indexDir, nullptr);
    }

    bool MM2::Initialize(const std::string& dataDir, const std::string& indexDir, const char* messageKeyPassphraseUtf8)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
#if !defined(ZCHATIM_USE_SQLCIPHER)
        if (messageKeyPassphraseUtf8 != nullptr) {
            SetLastError("Initialize: message key passphrase requires ZCHATIM_USE_SQLCIPHER=ON");
            return false;
        }
#endif
        if (messageKeyPassphraseUtf8 != nullptr && messageKeyPassphraseUtf8[0] == '\0') {
            SetLastError("Initialize: empty passphrase not allowed");
            return false;
        }
        return InitializeImplUnlocked(dataDir, indexDir, messageKeyPassphraseUtf8);
    }

    bool MM2::InitializeImplUnlocked(
        const std::string& dataDir,
        const std::string& indexDir,
        const char*       optionalMessageKeyPassphraseUtf8)
    {
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
#if defined(ZCHATIM_USE_SQLCIPHER)
        // 元数据 SQLCipher 密钥由消息主密钥域分离派生；须先 **`Crypto::Init`** + **`mm2_message_key.bin`**（与 **`03-Storage.md`** 第4.2节 一致）。
        if (!Crypto::Init()) {
            SetLastError("Crypto::Init failed (required for SQLCipher metadata key setup)");
            m_zdbManager.Cleanup();
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        if (!LoadOrCreateMessageStorageKeyUnlocked(optionalMessageKeyPassphraseUtf8)) {
            if (LastError().empty()) {
                SetLastError("message storage key setup failed (SQLCipher metadata)");
            }
            m_zdbManager.Cleanup();
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        std::vector<uint8_t> sqlcipherMetaKey;
        if (!DeriveMetadataSqlcipherKeyFromMessageMaster(m_messageStorageKey, sqlcipherMetaKey)) {
            SetLastError("DeriveMetadataSqlcipherKeyFromMessageMaster failed");
            m_zdbManager.Cleanup();
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        if (!m_metadataDb.Open(m_metadataDbPath, sqlcipherMetaKey)) {
            SetLastError(m_metadataDb.LastError());
            SecureZeroBytes(sqlcipherMetaKey.data(), sqlcipherMetaKey.size());
            m_zdbManager.Cleanup();
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
        SecureZeroBytes(sqlcipherMetaKey.data(), sqlcipherMetaKey.size());
        sqlcipherMetaKey.clear();
#else
        if (!m_metadataDb.Open(m_metadataDbPath)) {
            SetLastError(m_metadataDb.LastError());
            m_zdbManager.Cleanup();
            m_dataDir.clear();
            m_indexDir.clear();
            m_metadataDbPath.clear();
            return false;
        }
#endif
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
        // 元库 **无** `im_messages`（IM 仅 RAM）；无需 purge/drop 迁移。
        ImRamClearUnlocked();
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

    bool MM2::LoadOrCreateMessageStorageKeyUnlocked(const char* optionalMessageKeyPassphraseUtf8)
    {
        if (optionalMessageKeyPassphraseUtf8 != nullptr && optionalMessageKeyPassphraseUtf8[0] == '\0') {
            SetLastError("LoadOrCreateMessageStorageKey: empty passphrase not allowed");
            return false;
        }
        m_messageStorageKey.clear();
        if (m_indexDir.empty()) {
            SetLastError("LoadOrCreateMessageStorageKey: indexDir empty");
            return false;
        }
        namespace fs = std::filesystem;
        const fs::path     keyPath        = fs::path(m_indexDir) / "mm2_message_key.bin";
        const std::string indexDirUtf8 =
            fs::path(m_indexDir).lexically_normal().generic_string();
        std::error_code ec;
        const bool      keyFilePresent = fs::exists(keyPath, ec);
        if (ec) {
            SetLastError(std::string("LoadOrCreateMessageStorageKey: exists(keyPath) failed: ") + ec.message());
            return false;
        }
        if (keyFilePresent) {
            std::vector<uint8_t> raw;
            std::string          readErr;
#ifdef _WIN32
            if (!ReadAllBytesMessageKeyFileWin32(keyPath, raw, readErr)) {
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + readErr);
                return false;
            }
#else
            if (!ReadAllBytesMessageKeyFilePosix(keyPath, raw, readErr)) {
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + readErr);
                return false;
            }
#endif
            if (detail::message_key_passphrase::IsZmkpV1(raw)) {
                if (optionalMessageKeyPassphraseUtf8 == nullptr) {
                    SetLastError(
                        "LoadOrCreateMessageStorageKey: ZMKP key file requires passphrase (use MM2::Initialize with passphrase)");
                    return false;
                }
                std::string zerr;
                if (!detail::message_key_passphrase::ParseZmkpV1(
                        raw,
                        std::string_view(optionalMessageKeyPassphraseUtf8),
                        m_messageStorageKey,
                        zerr)) {
                    SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + zerr);
                    return false;
                }
                return true;
            }
            if (optionalMessageKeyPassphraseUtf8 != nullptr) {
                SetLastError(
                    "LoadOrCreateMessageStorageKey: passphrase provided but key file is not ZMKP (use Initialize without passphrase for ZMK1/2/3)");
                return false;
            }
#ifdef _WIN32
            std::error_code      ecFsz;
            const std::uintmax_t fszBefore = fs::file_size(keyPath, ecFsz);
            if (!ParseMessageKeyFromRawWin32(raw, m_messageStorageKey, readErr)) {
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + readErr);
                return false;
            }
            if (!ecFsz && fszBefore == ZChatIM::CRYPTO_KEY_SIZE) {
                std::string migErr;
                (void)WriteMessageKeyFileWin32(keyPath, m_messageStorageKey.data(), migErr);
            }
#else
            std::error_code      ecFsz;
            const std::uintmax_t fszBefore = fs::file_size(keyPath, ecFsz);
            if (!ParseMessageKeyFromRawPosix(raw, indexDirUtf8, m_messageStorageKey, readErr)) {
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + readErr);
                return false;
            }
            if (!ecFsz && fszBefore == ZChatIM::CRYPTO_KEY_SIZE) {
                std::string migErr;
                (void)WriteMessageKeyFilePosix(keyPath, indexDirUtf8, m_messageStorageKey.data(), migErr);
            }
#endif
            return true;
        }
        std::vector<uint8_t> key = Crypto::GenerateKey(ZChatIM::CRYPTO_KEY_SIZE);
        if (key.size() != ZChatIM::CRYPTO_KEY_SIZE) {
            SetLastError("LoadOrCreateMessageStorageKey: Crypto::GenerateKey failed (secure random)");
            return false;
        }
        std::string writeErr;
        if (optionalMessageKeyPassphraseUtf8 != nullptr) {
            std::vector<uint8_t> zmkp;
            if (!detail::message_key_passphrase::BuildZmkpV1Blob(
                    key.data(),
                    std::string_view(optionalMessageKeyPassphraseUtf8),
                    zmkp,
                    writeErr)) {
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + writeErr);
                return false;
            }
#ifdef _WIN32
            if (!WriteRawBytesMessageKeyFileWin32(keyPath, zmkp, writeErr)) {
#else
            if (!WriteRawBytesMessageKeyFilePosix(keyPath, zmkp, writeErr)) {
#endif
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + writeErr);
                return false;
            }
        } else {
#ifdef _WIN32
            if (!WriteMessageKeyFileWin32(keyPath, key.data(), writeErr)) {
#else
            if (!WriteMessageKeyFilePosix(keyPath, indexDirUtf8, key.data(), writeErr)) {
#endif
                SetLastError(std::string("LoadOrCreateMessageStorageKey: ") + writeErr);
                return false;
            }
        }
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
        if (!LoadOrCreateMessageStorageKeyUnlocked(nullptr)) {
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

    bool MM2::WriteGroupKeyEnvelopeUnlocked(const std::vector<uint8_t>& groupId, uint64_t epochSeconds)
    {
        if (groupId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("WriteGroupKeyEnvelope: invalid groupId size");
            return false;
        }
        std::vector<uint8_t> key = Crypto::GenerateSecureRandom(ZChatIM::CRYPTO_KEY_SIZE);
        if (key.size() != ZChatIM::CRYPTO_KEY_SIZE) {
            SetLastError("WriteGroupKeyEnvelope: GenerateSecureRandom failed");
            return false;
        }
        std::vector<uint8_t> blob;
        blob.reserve(4U + 8U + ZChatIM::CRYPTO_KEY_SIZE);
        blob.push_back(static_cast<uint8_t>('Z'));
        blob.push_back(static_cast<uint8_t>('G'));
        blob.push_back(static_cast<uint8_t>('K'));
        blob.push_back(static_cast<uint8_t>('1'));
        uint64_t e = epochSeconds;
        for (int shift = 56; shift >= 0; shift -= 8) {
            blob.push_back(static_cast<uint8_t>((e >> shift) & 0xFFULL));
        }
        blob.insert(blob.end(), key.begin(), key.end());
        SecureZeroBytes(key.data(), key.size());

        if (!PutDataBlockBlobUnlocked(groupId, 0, blob)) {
            return false;
        }
        std::string zf;
        uint64_t    off = 0;
        uint64_t    len = 0;
        uint8_t     sh[ZChatIM::SHA256_SIZE]{};
        if (!m_metadataDb.GetDataBlock(groupId, 0, zf, off, len, sh)) {
            const std::string errSave = m_metadataDb.LastError();
            // Put 已成功：`data_blocks` 与 `.zdb` 区间可能已提交；尽力回滚，减少孤儿块。
            if (m_metadataDb.DataBlockExists(groupId, 0)) {
                std::string zfR;
                uint64_t    offR = 0;
                uint64_t    lenR = 0;
                uint8_t     shR[ZChatIM::SHA256_SIZE]{};
                if (m_metadataDb.GetDataBlock(groupId, 0, zfR, offR, lenR, shR)) {
                    (void)RevertFailedPutDataBlockUnlocked(groupId, 0, zfR, offR, blob.size());
                }
            }
            SetLastError(errSave.empty() ? "WriteGroupKeyEnvelope: GetDataBlock after Put failed" : errSave);
            return false;
        }
        if (!m_metadataDb.UpsertGroupData(groupId, zf, off, len, sh)) {
            SetLastError(m_metadataDb.LastError());
            (void)RevertFailedPutDataBlockUnlocked(groupId, 0, zf, off, blob.size());
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
        const std::vector<uint8_t>& senderUserId,
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
        if (senderUserId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("StoreMessage: senderUserId must be USER_ID_SIZE (16) bytes");
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

        const int64_t storedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
        ImRamMessageRow row;
        row.messageId       = messageId;
        row.senderUserId    = senderUserId;
        row.blob            = std::move(blob);
        row.stored_at_ms    = storedAtMs;
        row.has_read        = false;
        row.read_at_ms      = 0;
        row.edit_count      = 0;
        row.last_edit_time_s = 0;
        if (!ImRamInsertRowUnlocked(sessionId, std::move(row))) {
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

        ImRamMessageRow* row = nullptr;
        if (!ImRamLocateUnlocked(messageId, &row) || row == nullptr) {
            SetLastError("RetrieveMessage: message not found (not in memory IM store)");
            return false;
        }

        const std::vector<uint8_t>& blob = row->blob;
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
            if (!ImRamListIdsChronologicalFirstNUnlocked(sessionId, lim, ids)) {
                return outRows;
            }
        } else {
            if (!ImRamListIdsAfterMessageIdUnlocked(sessionId, lastMsgId, lim, ids)) {
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
        uint64_t                    sinceTimestampMs,
        int                         count)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        std::vector<std::vector<uint8_t>> outRows;
        if (count <= 0) {
            return outRows;
        }
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return outRows;
        }
        if (sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("ListMessagesSinceTimestamp: userId/sessionId must be USER_ID_SIZE (16) bytes");
            return outRows;
        }
        if (!EnsureMessageCryptoReadyUnlocked()) {
            return outRows;
        }
        const int64_t sinceI64 = (sinceTimestampMs > static_cast<uint64_t>(INT64_MAX))
            ? INT64_MAX
            : static_cast<int64_t>(sinceTimestampMs);
        const size_t lim = (count > INT_MAX) ? static_cast<size_t>(INT_MAX) : static_cast<size_t>(count);
        std::vector<std::vector<uint8_t>> ids;
        if (!ImRamListIdsSinceStoredAtUnlocked(sessionId, sinceI64, lim, ids)) {
            return outRows;
        }
        for (const auto& mid : ids) {
            std::vector<uint8_t> pl;
            if (!RetrieveMessage(mid, pl)) {
                outRows.clear();
                return outRows;
            }
            std::vector<uint8_t> row = EncodeQueryMessageRow(mid, pl);
            if (row.empty()) {
                SetLastError("ListMessagesSinceTimestamp: encode row failed");
                outRows.clear();
                return outRows;
            }
            outRows.push_back(std::move(row));
        }
        return outRows;
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
        size_t n = 0;
        for (const auto& pr : m_imRamBySession) {
            n += pr.second.size();
        }
        return n;
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
#if defined(__APPLE__)
        if (!m_indexDir.empty()) {
            const std::string idxUtf8 =
                std::filesystem::path(m_indexDir).lexically_normal().generic_string();
            std::string       kcerr;
            (void)detail::Mm2DarwinDeleteMessageWrapKey(idxUtf8, kcerr);
        }
#endif
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
        // IM 仅内存：**仅**从 RAM 索引移除；磁盘 `data_blocks` 仅服务文件分片 / 群密钥等。
        if (!ImRamEraseUnlocked(messageId)) {
            SetLastError("DeleteMessageImplUnlocked: message not in IM RAM store");
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
        if (!ImRamExistsUnlocked(messageId)) {
            SetLastError("DeleteMessage: message not found (not in memory IM store)");
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
            SetLastError("MarkMessageRead: readTimestampMs exceeds int64_t range");
            return false;
        }
        ImRamMessageRow* row = nullptr;
        if (!ImRamLocateUnlocked(messageId, &row) || row == nullptr) {
            SetLastError("MarkMessageRead: message not found (not in memory IM store)");
            return false;
        }
        if (!row->has_read) {
            row->has_read   = true;
            row->read_at_ms = static_cast<int64_t>(readTimestampMs);
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
        auto it = m_imRamBySession.find(sessionId);
        if (it == m_imRamBySession.end()) {
            return true;
        }
        for (const auto& row : it->second) {
            if (row.has_read) {
                continue;
            }
            outUnreadMessages.emplace_back(row.messageId, 0ULL);
            if (outUnreadMessages.size() >= limit) {
                break;
            }
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
        if (repliedContentDigest.size() != ZChatIM::SHA256_SIZE) {
            SetLastError("StoreMessageReplyRelation: repliedContentDigest must be SHA256_SIZE (32) bytes");
            return false;
        }
        if (!ImRamExistsUnlocked(messageId)) {
            SetLastError("StoreMessageReplyRelation: message not found (not in memory IM store)");
            return false;
        }
        const auto itSessNew = m_imRamMsgToSession.find(messageId);
        const auto itSessOld = m_imRamMsgToSession.find(repliedMsgId);
        if (itSessNew == m_imRamMsgToSession.end() || itSessOld == m_imRamMsgToSession.end()) {
            SetLastError("StoreMessageReplyRelation: session mapping missing for message or replied message");
            return false;
        }
        if (itSessNew->second != itSessOld->second) {
            SetLastError("StoreMessageReplyRelation: cross-session reply not allowed");
            return false;
        }
        ImRamMessageRow* repliedRow = nullptr;
        if (!ImRamLocateUnlocked(repliedMsgId, &repliedRow) || repliedRow == nullptr) {
            SetLastError("StoreMessageReplyRelation: replied message row missing");
            return false;
        }
        if (repliedRow->senderUserId.size() != ZChatIM::USER_ID_SIZE
            || !common::Memory::ConstantTimeCompare(
                repliedRow->senderUserId.data(),
                repliedSenderId.data(),
                ZChatIM::USER_ID_SIZE)) {
            SetLastError("StoreMessageReplyRelation: repliedSenderId does not match stored sender");
            return false;
        }
        const std::vector<uint8_t>& imSessionId = itSessNew->second;
        bool                        isGroup    = false;
        if (!m_metadataDb.GroupIdHasAnyMemberRow(imSessionId, isGroup)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (isGroup) {
            ImRamMessageRow* newRow = nullptr;
            if (!ImRamLocateUnlocked(messageId, &newRow) || newRow == nullptr) {
                SetLastError("StoreMessageReplyRelation: new message row missing");
                return false;
            }
            bool existsReplyAuthor = false;
            if (!m_metadataDb.GetGroupMemberRowExists(imSessionId, newRow->senderUserId, existsReplyAuthor)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            bool existsRepliedAuthor = false;
            if (!m_metadataDb.GetGroupMemberRowExists(imSessionId, repliedSenderId, existsRepliedAuthor)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            if (!existsReplyAuthor || !existsRepliedAuthor) {
                SetLastError("StoreMessageReplyRelation: group membership check failed (SQL)");
                return false;
            }
        }
        ImRamReplyRow rr;
        rr.repliedMsgId     = repliedMsgId;
        rr.repliedSenderId  = repliedSenderId;
        rr.repliedDigest    = repliedContentDigest;
        m_imRamReplies[messageId] = std::move(rr);
        m_lastError.clear();
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
        auto itr = m_imRamReplies.find(messageId);
        if (itr == m_imRamReplies.end()) {
            SetLastError("GetMessageReplyRelation: no reply relation for message_id");
            return false;
        }
        outRepliedMsgId          = itr->second.repliedMsgId;
        outRepliedSenderId       = itr->second.repliedSenderId;
        outRepliedContentDigest  = itr->second.repliedDigest;
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
        if (!EnsureMessageCryptoReadyUnlocked()) {
            return false;
        }
        constexpr size_t kOverhead = ZChatIM::NONCE_SIZE + ZChatIM::AUTH_TAG_SIZE;
        if (newEncryptedContent.size() > ZChatIM::ZDB_MAX_WRITE_SIZE - kOverhead) {
            SetLastError("EditMessage: new content too large for single encrypted blob");
            return false;
        }
        if (newEditCount < 1U || newEditCount > 3U) {
            SetLastError("EditMessage: newEditCount must be in [1,3]");
            return false;
        }
        ImRamMessageRow* row = nullptr;
        if (!ImRamLocateUnlocked(messageId, &row) || row == nullptr) {
            SetLastError("EditMessage: message not found (not in memory IM store)");
            return false;
        }
        // 与 **`StoreMessage`** 一致：**`row->blob`** = nonce‖ciphertext‖tag，**`RetrieveMessage`** 才可解密。
        std::vector<uint8_t> nonce = Crypto::GenerateNonce(ZChatIM::NONCE_SIZE);
        if (nonce.size() != ZChatIM::NONCE_SIZE) {
            SetLastError("EditMessage: GenerateNonce failed");
            return false;
        }
        std::vector<uint8_t> ciphertext;
        uint8_t              tag[ZChatIM::AUTH_TAG_SIZE]{};
        if (!Crypto::EncryptMessage(
                newEncryptedContent.empty() ? nullptr : newEncryptedContent.data(),
                newEncryptedContent.size(),
                m_messageStorageKey.data(),
                m_messageStorageKey.size(),
                nonce.data(),
                nonce.size(),
                ciphertext,
                tag,
                sizeof(tag))) {
            SetLastError("EditMessage: Crypto::EncryptMessage failed");
            return false;
        }
        std::vector<uint8_t> blob;
        blob.reserve(nonce.size() + ciphertext.size() + sizeof(tag));
        blob.insert(blob.end(), nonce.begin(), nonce.end());
        blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());
        blob.insert(blob.end(), tag, tag + sizeof(tag));
        row->blob             = std::move(blob);
        row->edit_count       = newEditCount;
        row->last_edit_time_s = editTimestampSeconds;
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
        ImRamMessageRow* row = nullptr;
        if (!ImRamLocateUnlocked(messageId, &row) || row == nullptr) {
            SetLastError("GetMessageEditState: message not found (not in memory IM store)");
            return false;
        }
        outEditCount           = row->edit_count;
        outLastEditTimeSeconds = row->last_edit_time_s;
        return true;
    }

    bool MM2::GetMessageSenderUserId(
        const std::vector<uint8_t>& messageId,
        std::vector<uint8_t>&       outSenderUserId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outSenderUserId.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (messageId.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("GetMessageSenderUserId: messageId must be MESSAGE_ID_SIZE (16) bytes");
            return false;
        }
        ImRamMessageRow* row = nullptr;
        if (!ImRamLocateUnlocked(messageId, &row) || row == nullptr) {
            SetLastError("GetMessageSenderUserId: message not found (not in memory IM store)");
            return false;
        }
        if (row->senderUserId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("GetMessageSenderUserId: sender_user_id invalid length");
            return false;
        }
        outSenderUserId = row->senderUserId;
        return true;
    }

    bool MM2::StoreMessages(
        const std::vector<uint8_t>& sessionId,
        const std::vector<uint8_t>& senderUserId,
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
        if (senderUserId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("StoreMessages: senderUserId must be USER_ID_SIZE (16) bytes");
            return false;
        }
        if (payloads.empty()) {
            return true;
        }
        outMessageIds.reserve(payloads.size());
        for (const auto& pl : payloads) {
            std::vector<uint8_t> mid;
            if (!StoreMessage(sessionId, senderUserId, pl, mid)) {
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

    bool MM2::GetFriendRequestRowForMm1(
        const std::vector<uint8_t>& requestId,
        std::vector<uint8_t>&       outFromUser,
        std::vector<uint8_t>&       outToUser,
        int32_t&                    outStatus)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetFriendRequestRow(requestId, outFromUser, outToUser, outStatus)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::ListAcceptedFriendUserIdsForMm1(
        const std::vector<uint8_t>& userId,
        std::vector<std::vector<uint8_t>>& outFriends)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.ListAcceptedFriendPeerUserIds(userId, outFriends)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::DeleteAcceptedFriendshipBetweenForMm1(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& friendId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteAcceptedFriendshipEdgesBetween(userId, friendId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::CreateGroupSeedForMm1(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& creatorId,
        const std::string&          nameUtf8,
        uint64_t                    nowSeconds)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (groupId.size() != ZChatIM::USER_ID_SIZE || creatorId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("CreateGroupSeedForMm1: invalid id size");
            return false;
        }
        if (nameUtf8.empty()) {
            SetLastError("CreateGroupSeedForMm1: empty name");
            return false;
        }
        constexpr int32_t kOwnerRole = 2;
        if (!m_metadataDb.UpsertGroupMember(
                groupId, creatorId, kOwnerRole, static_cast<int64_t>(nowSeconds))) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (!m_metadataDb.UpsertGroupDisplayName(groupId, nameUtf8, nowSeconds, creatorId)) {
            SetLastError(m_metadataDb.LastError());
            (void)m_metadataDb.DeleteGroupMember(groupId, creatorId);
            return false;
        }
        if (!WriteGroupKeyEnvelopeUnlocked(groupId, nowSeconds)) {
            if (m_lastError.empty()) {
                SetLastError("CreateGroupSeedForMm1: WriteGroupKeyEnvelope failed");
            }
            (void)m_metadataDb.DeleteGroupMember(groupId, creatorId);
            (void)m_metadataDb.DeleteGroupDisplayName(groupId);
            return false;
        }
        return true;
    }

    bool MM2::UpsertGroupMemberForMm1(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        int32_t                     role,
        int64_t                     joinedAtSeconds)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertGroupMember(groupId, userId, role, joinedAtSeconds)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::DeleteGroupMemberForMm1(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteGroupMute(groupId, userId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (!m_metadataDb.DeleteGroupMember(groupId, userId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::ListGroupMemberUserIdsForMm1(
        const std::vector<uint8_t>& groupId,
        std::vector<std::vector<uint8_t>>& outUserIds)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outUserIds.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.ListGroupMemberUserIds(groupId, outUserIds)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetGroupMemberRoleForMm1(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        int32_t&                    outRole,
        int64_t&                    outJoinedAt)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetGroupMember(groupId, userId, outRole, outJoinedAt)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetGroupMemberExistsForMm1(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        bool&                       outExists)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetGroupMemberRowExists(groupId, userId, outExists)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::UpsertGroupKeyEnvelopeForMm1(const std::vector<uint8_t>& groupId, uint64_t epochSeconds)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (groupId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("UpsertGroupKeyEnvelopeForMm1: invalid groupId size");
            return false;
        }
        return WriteGroupKeyEnvelopeUnlocked(groupId, epochSeconds);
    }

    bool MM2::TryGetGroupKeyEnvelopeForMm1(const std::vector<uint8_t>& groupId, std::vector<uint8_t>& outEnvelope)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outEnvelope.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (groupId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("TryGetGroupKeyEnvelopeForMm1: invalid groupId size");
            return false;
        }
        std::string zf;
        uint64_t    off = 0;
        uint64_t    len = 0;
        uint8_t     sh[ZChatIM::SHA256_SIZE]{};
        if (!m_metadataDb.GetGroupData(groupId, zf, off, len, sh)) {
            return false;
        }
        if (!GetDataBlockBlobUnlocked(groupId, 0, outEnvelope)) {
            return false;
        }
        constexpr size_t kZgk1Size = 4U + 8U + ZChatIM::CRYPTO_KEY_SIZE;
        if (outEnvelope.size() != kZgk1Size || outEnvelope[0] != 'Z' || outEnvelope[1] != 'G' || outEnvelope[2] != 'K'
            || outEnvelope[3] != '1') {
            outEnvelope.clear();
            SetLastError("TryGetGroupKeyEnvelopeForMm1: invalid ZGK1 envelope");
            return false;
        }
        return true;
    }

    bool MM2::SeedAcceptedFriendshipForSelfTest(
        const std::vector<uint8_t>& fromUserId,
        const std::vector<uint8_t>& toUserId,
        uint64_t                    nowSeconds)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (fromUserId.size() != ZChatIM::USER_ID_SIZE || toUserId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("SeedAcceptedFriendshipForSelfTest: invalid user id size");
            return false;
        }
        if (fromUserId == toUserId) {
            SetLastError("SeedAcceptedFriendshipForSelfTest: from and to must differ");
            return false;
        }
        std::vector<uint8_t> rid = Crypto::GenerateSecureRandom(ZChatIM::MESSAGE_ID_SIZE);
        if (rid.size() != ZChatIM::MESSAGE_ID_SIZE) {
            SetLastError("SeedAcceptedFriendshipForSelfTest: request id random failed");
            return false;
        }
        std::vector<uint8_t> sig(64, 0);
        if (!m_metadataDb.InsertFriendRequest(rid, fromUserId, toUserId, nowSeconds, sig)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (!m_metadataDb.UpdateFriendRequestStatus(rid, 1, toUserId, nowSeconds)) {
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

    bool MM2::UpsertGroupMuteForMm1(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        int64_t                     startMs,
        int64_t                     durationSeconds,
        const std::vector<uint8_t>& mutedBy,
        const std::vector<uint8_t>& reason)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertGroupMute(groupId, userId, startMs, durationSeconds, mutedBy, reason)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::DeleteGroupMuteForMm1(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteGroupMute(groupId, userId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::GetGroupMuteRowForMm1(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        bool&                       outExists,
        int64_t&                    outStartMs,
        int64_t&                    outDurationS,
        std::vector<uint8_t>&       outMutedBy,
        std::vector<uint8_t>&       outReason)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetGroupMuteRow(groupId, userId, outExists, outStartMs, outDurationS, outMutedBy, outReason)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::DeleteExpiredGroupMutesForMm1(int64_t nowMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteExpiredGroupMutes(nowMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1RegisterDeviceSession(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& deviceId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    loginTimeMs,
        uint64_t                    lastActiveMs,
        std::vector<uint8_t>&       outKickedSessionId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outKickedSessionId.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (userId.size() != ZChatIM::USER_ID_SIZE || deviceId.size() != ZChatIM::USER_ID_SIZE
            || sessionId.size() != ZChatIM::USER_ID_SIZE) {
            SetLastError("Mm1RegisterDeviceSession: invalid id length");
            return false;
        }
        if (!m_metadataDb.DeleteMm1DeviceSessionsWhereSessionId(sessionId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        for (;;) {
            std::vector<std::vector<uint8_t>> sids;
            std::vector<std::vector<uint8_t>> dids;
            std::vector<uint64_t>             logins;
            std::vector<uint64_t>             lasts;
            if (!m_metadataDb.ListMm1DeviceSessionsForUser(userId, sids, dids, logins, lasts)) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
            if (sids.size() < 2) {
                break;
            }
            size_t kickIdx = 0;
            for (size_t i = 1; i < logins.size(); ++i) {
                if (logins[i] < logins[kickIdx]) {
                    kickIdx = i;
                }
            }
            outKickedSessionId = sids[kickIdx];
            if (!m_metadataDb.DeleteMm1DeviceSessionByUserAndSession(userId, sids[kickIdx])) {
                SetLastError(m_metadataDb.LastError());
                return false;
            }
        }
        if (!m_metadataDb.InsertMm1DeviceSession(userId, sessionId, deviceId, loginTimeMs, lastActiveMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1UpdateDeviceSessionLastActive(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    lastActiveMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpdateMm1DeviceSessionLastActive(userId, sessionId, lastActiveMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1ListDeviceSessions(
        const std::vector<uint8_t>& userId,
        std::vector<std::vector<uint8_t>>& outSessionIds,
        std::vector<std::vector<uint8_t>>& outDeviceIds,
        std::vector<uint64_t>&             outLoginTimeMs,
        std::vector<uint64_t>&             outLastActiveMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        outSessionIds.clear();
        outDeviceIds.clear();
        outLoginTimeMs.clear();
        outLastActiveMs.clear();
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.ListMm1DeviceSessionsForUser(userId, outSessionIds, outDeviceIds, outLoginTimeMs, outLastActiveMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1CleanupExpiredDeviceSessions(uint64_t nowMs, uint64_t idleTimeoutMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteMm1DeviceSessionsIdleOlderThan(nowMs, idleTimeoutMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1ClearAllDeviceSessions()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            return true;
        }
        if (!m_metadataDb.DeleteAllMm1DeviceSessions()) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1TouchImSessionActivity(const std::vector<uint8_t>& imSessionId, uint64_t lastActiveMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertMm1ImSessionActivity(imSessionId, lastActiveMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1SelectImSessionLastActive(
        const std::vector<uint8_t>& imSessionId,
        uint64_t&                   outLastActiveMs,
        bool&                       outFound)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.SelectMm1ImSessionLastActive(imSessionId, outLastActiveMs, outFound)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1CleanupExpiredImSessionActivity(uint64_t nowMs, uint64_t idleTimeoutMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteMm1ImSessionActivityIdleOlderThan(nowMs, idleTimeoutMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1ClearAllImSessionActivity()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            return true;
        }
        if (!m_metadataDb.DeleteAllMm1ImSessionActivity()) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1CertPinningConfigure(
        const std::vector<uint8_t>& currentSpkiSha256,
        const std::vector<uint8_t>& standbySpkiSha256)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.SetMm1CertPinConfig(currentSpkiSha256, standbySpkiSha256)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1CertPinningVerify(const std::vector<uint8_t>& presentedSpkiSha256)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (presentedSpkiSha256.size() != ZChatIM::SHA256_SIZE) {
            return false;
        }
        std::vector<uint8_t> cur;
        std::vector<uint8_t> st;
        if (!m_metadataDb.GetMm1CertPinConfig(cur, st)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        const bool okCur =
            cur.size() == ZChatIM::SHA256_SIZE
            && common::Memory::ConstantTimeCompare(cur.data(), presentedSpkiSha256.data(), ZChatIM::SHA256_SIZE);
        const bool okSt =
            st.size() == ZChatIM::SHA256_SIZE
            && common::Memory::ConstantTimeCompare(st.data(), presentedSpkiSha256.data(), ZChatIM::SHA256_SIZE);
        return okCur || okSt;
    }

    bool MM2::Mm1CertPinningIsBanned(const std::vector<uint8_t>& clientId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        uint32_t fc = 0;
        bool     banned = false;
        bool     found  = false;
        if (!m_metadataDb.GetMm1CertPinClient(clientId, fc, banned, found)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return found && banned;
    }

    bool MM2::Mm1CertPinningRecordFailure(const std::vector<uint8_t>& clientId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        uint32_t fc    = 0;
        bool     banned = false;
        bool     found  = false;
        if (!m_metadataDb.GetMm1CertPinClient(clientId, fc, banned, found)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        if (!found) {
            fc = 0;
        }
        ++fc;
        if (fc >= 3U) {
            banned = true;
        }
        if (!m_metadataDb.UpsertMm1CertPinClient(clientId, fc, banned)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1CertPinningClearBan(const std::vector<uint8_t>& clientId)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.DeleteMm1CertPinClient(clientId)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1CertPinningResetAll()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            return true;
        }
        if (!m_metadataDb.DeleteAllMm1CertPinData()) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1UpsertUserStatus(const std::vector<uint8_t>& userId, bool online, uint64_t updatedMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertMm1UserStatus(userId, online, updatedMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1GetUserStatus(const std::vector<uint8_t>& userId, bool& outOnline, bool& outFound)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.GetMm1UserStatus(userId, outOnline, outFound)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1ClearAllUserStatus()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            return true;
        }
        if (!m_metadataDb.DeleteAllMm1UserStatus()) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1MentionAtAllLoadTimes(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& senderId,
        std::vector<uint64_t>& outTimesMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.SelectMm1MentionAtAllTimes(groupId, senderId, outTimesMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1MentionAtAllStoreTimes(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint64_t>& timesMs)
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            SetLastError("MM2 not initialized");
            return false;
        }
        if (!m_metadataDb.UpsertMm1MentionAtAllTimes(groupId, senderId, timesMs)) {
            SetLastError(m_metadataDb.LastError());
            return false;
        }
        return true;
    }

    bool MM2::Mm1ClearAllMentionAtAllWindows()
    {
        std::lock_guard<std::recursive_mutex> lk(m_stateMutex);
        m_lastError.clear();
        if (!m_initialized) {
            return true;
        }
        if (!m_metadataDb.DeleteAllMm1MentionAtAllWindows()) {
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
        if (!ImRamListIdsLastNUnlocked(sessionId, limit, ids)) {
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
        ImRamClearSessionUnlocked(sessionId);
        m_lastError.clear();
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
        const int64_t nowMsI64 =
            static_cast<int64_t>(nowMs > static_cast<uint64_t>(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(nowMs));
        if (!m_metadataDb.DeleteExpiredGroupMutes(nowMsI64)) {
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
