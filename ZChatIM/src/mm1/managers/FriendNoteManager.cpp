// FriendNoteManager：验签 + **accepted 好友**；备注持久化 **`mm1_user_kv`**（**type=`0x464E424E` 'FNBN'**）单 BLOB 打包多好友条目。
#include "mm1/managers/FriendNoteManager.h"

#include "common/Ed25519.h"
#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/crypto/Sha256.h"
#include "Types.h"

#include <cstring>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr size_t  kEd25519PublicKeyBytes  = 32;
        constexpr size_t  kEd25519SignatureBytes  = 64;
        constexpr int32_t kUserDataEd25519PubkeyType = 0x45444A31;
        constexpr int32_t kMm1KvFriendNotesBundle  = 0x464E424E; // 'FNBN'
        constexpr char    kFriendNoteSigDomain[]     = "ZChatIM|UpdateFriendNote|v1";
        constexpr char    kBundleMagic[]             = "ZFN1";
        constexpr size_t  kMaxNoteBytes             = 64U * 1024U;

        void AppendU64Be(std::vector<uint8_t>& out, uint64_t v)
        {
            for (int s = 56; s >= 0; s -= 8) {
                out.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
            }
        }

        bool BuildFriendNoteCanonical(
            const std::vector<uint8_t>& userId,
            const std::vector<uint8_t>& friendId,
            uint64_t                    updateTimestampSeconds,
            const uint8_t               noteDigest[32],
            std::vector<uint8_t>&       out)
        {
            out.clear();
            const size_t prefixLen = sizeof(kFriendNoteSigDomain) - 1U;
            out.reserve(prefixLen + USER_ID_SIZE * 2 + 8 + 32);
            out.insert(out.end(), kFriendNoteSigDomain, kFriendNoteSigDomain + prefixLen);
            out.insert(out.end(), userId.begin(), userId.end());
            out.insert(out.end(), friendId.begin(), friendId.end());
            AppendU64Be(out, updateTimestampSeconds);
            out.insert(out.end(), noteDigest, noteDigest + 32);
            return true;
        }

        bool ParseBundle(
            const std::vector<uint8_t>& raw,
            std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& outEntries)
        {
            outEntries.clear();
            const size_t mag = sizeof(kBundleMagic) - 1U;
            if (raw.size() < mag) {
                return true;
            }
            if (std::memcmp(raw.data(), kBundleMagic, mag) != 0) {
                return false;
            }
            size_t off = mag;
            while (off + USER_ID_SIZE + 4 <= raw.size()) {
                std::vector<uint8_t> fid(
                    raw.begin() + static_cast<std::ptrdiff_t>(off),
                    raw.begin() + static_cast<std::ptrdiff_t>(off + USER_ID_SIZE));
                off += USER_ID_SIZE;
                const uint32_t n = (static_cast<uint32_t>(raw[off]) << 24)
                    | (static_cast<uint32_t>(raw[off + 1]) << 16)
                    | (static_cast<uint32_t>(raw[off + 2]) << 8) | static_cast<uint32_t>(raw[off + 3]);
                off += 4;
                if (n > kMaxNoteBytes || off + n > raw.size()) {
                    return false;
                }
                std::vector<uint8_t> note(
                    raw.begin() + static_cast<std::ptrdiff_t>(off),
                    raw.begin() + static_cast<std::ptrdiff_t>(off + n));
                off += n;
                outEntries.emplace_back(std::move(fid), std::move(note));
            }
            return off == raw.size();
        }

        void SerializeBundle(
            const std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& entries,
            std::vector<uint8_t>& out)
        {
            out.clear();
            const size_t mag = sizeof(kBundleMagic) - 1U;
            out.reserve(mag + entries.size() * (USER_ID_SIZE + 4 + kMaxNoteBytes));
            out.insert(out.end(), kBundleMagic, kBundleMagic + mag);
            for (const auto& pr : entries) {
                const auto& fid  = pr.first;
                const auto& note = pr.second;
                if (fid.size() != USER_ID_SIZE || note.size() > kMaxNoteBytes) {
                    continue;
                }
                out.insert(out.end(), fid.begin(), fid.end());
                const uint32_t n = static_cast<uint32_t>(note.size());
                out.push_back(static_cast<uint8_t>((n >> 24) & 0xFF));
                out.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
                out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
                out.push_back(static_cast<uint8_t>(n & 0xFF));
                out.insert(out.end(), note.begin(), note.end());
            }
        }

        bool IsAcceptedFriend(const std::vector<uint8_t>& userId, const std::vector<uint8_t>& friendId)
        {
            const std::vector<std::vector<uint8_t>> friends =
                MM1::Instance().GetFriendManager().GetFriends(userId);
            for (const auto& f : friends) {
                if (f.size() == USER_ID_SIZE
                    && common::Memory::ConstantTimeCompare(f.data(), friendId.data(), USER_ID_SIZE)) {
                    return true;
                }
            }
            return false;
        }

    } // namespace

    bool FriendNoteManager::UpdateFriendNote(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& friendId,
        const std::vector<uint8_t>& newEncryptedNote,
        uint64_t                    updateTimestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (userId.size() != USER_ID_SIZE || friendId.size() != USER_ID_SIZE) {
            return false;
        }
        if (common::Memory::ConstantTimeCompare(userId.data(), friendId.data(), USER_ID_SIZE)) {
            return false;
        }
        if (newEncryptedNote.size() > kMaxNoteBytes) {
            return false;
        }
        if (signatureEd25519.size() != kEd25519SignatureBytes) {
            return false;
        }

        if (!IsAcceptedFriend(userId, friendId)) {
            return false;
        }

        const std::vector<uint8_t> pubkey =
            MM1::Instance().GetUserDataManager().GetUserData(userId, kUserDataEd25519PubkeyType);
        if (pubkey.size() != kEd25519PublicKeyBytes) {
            return false;
        }

        uint8_t digest[32]{};
        if (!crypto::Sha256(
                newEncryptedNote.empty() ? nullptr : newEncryptedNote.data(),
                newEncryptedNote.size(),
                digest)) {
            return false;
        }

        std::vector<uint8_t> payload;
        BuildFriendNoteCanonical(userId, friendId, updateTimestampSeconds, digest, payload);

        if (!common::Ed25519VerifyDetached(
                payload.data(),
                payload.size(),
                signatureEd25519.data(),
                pubkey.data())) {
            return false;
        }

        std::vector<uint8_t> raw =
            MM1::Instance().GetUserDataManager().GetUserData(userId, kMm1KvFriendNotesBundle);
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> entries;
        if (!raw.empty() && !ParseBundle(raw, entries)) {
            return false;
        }

        bool replaced = false;
        for (auto& pr : entries) {
            if (pr.first.size() == USER_ID_SIZE
                && common::Memory::ConstantTimeCompare(pr.first.data(), friendId.data(), USER_ID_SIZE)) {
                pr.second = newEncryptedNote;
                replaced    = true;
                break;
            }
        }
        if (!replaced) {
            entries.emplace_back(friendId, newEncryptedNote);
        }

        std::vector<uint8_t> bundle;
        SerializeBundle(entries, bundle);
        return MM1::Instance().GetUserDataManager().StoreUserData(userId, kMm1KvFriendNotesBundle, bundle);
    }

} // namespace ZChatIM::mm1
