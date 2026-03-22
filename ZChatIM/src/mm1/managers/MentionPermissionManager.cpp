// MentionPermissionManager：Ed25519 + **MM2 → `group_members` SQL** 成员/角色校验；@ALL 限速在 **`mm1_mention_atall_window`**（**60s** 窗内最多 **3** 次，进程重启可恢复）。
#include "mm1/managers/MentionPermissionManager.h"

#include "common/Ed25519.h"
#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr size_t   kEd25519PublicKeyBytes  = 32;
        constexpr size_t   kEd25519SignatureBytes  = 64;
        constexpr int32_t  kUserDataEd25519PubkeyType = 0x45444A31;
        constexpr char     kMentionSigDomain[]        = "ZChatIM|MentionRequest|v1";
        constexpr int32_t  kRoleMember                = 0;
        constexpr int32_t  kRoleAdmin               = 1;
        constexpr int32_t  kRoleOwner               = 2;
        constexpr uint64_t kAtAllWindowMs           = 60'000ULL;
        constexpr size_t   kMaxAtAllPerWindow       = 3;

        void AppendI32Be(std::vector<uint8_t>& out, int32_t v)
        {
            out.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> 24) & 0xFF));
            out.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(static_cast<uint32_t>(v) & 0xFF));
        }

        void AppendU64Be(std::vector<uint8_t>& out, uint64_t v)
        {
            for (int s = 56; s >= 0; s -= 8) {
                out.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
            }
        }

        void PruneAtAllWindow(std::vector<uint64_t>& vec, uint64_t nowMs, uint64_t windowMs)
        {
            const uint64_t cut = nowMs > windowMs ? nowMs - windowMs : 0ULL;
            vec.erase(std::remove_if(vec.begin(), vec.end(), [cut](uint64_t t) { return t < cut; }), vec.end());
        }

    } // namespace

    bool MentionPermissionManager::ValidateMentionRequest(
        const std::vector<uint8_t>&              groupId,
        const std::vector<uint8_t>&              senderId,
        int32_t                                  mentionType,
        const std::vector<std::vector<uint8_t>>& mentionedUserIds,
        uint64_t                                 nowMs,
        const std::vector<uint8_t>&              signatureEd25519)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (groupId.size() != USER_ID_SIZE || senderId.size() != USER_ID_SIZE) {
            return false;
        }
        constexpr size_t kMaxMentionedUsers = 256U;
        if (mentionedUserIds.size() > kMaxMentionedUsers) {
            return false;
        }
        if (signatureEd25519.size() != kEd25519SignatureBytes) {
            return false;
        }

        const std::vector<uint8_t> pubkey =
            MM1::Instance().GetUserDataManager().GetUserData(senderId, kUserDataEd25519PubkeyType);
        if (pubkey.size() != kEd25519PublicKeyBytes) {
            return false;
        }

        std::vector<uint8_t> payload;
        {
            const size_t prefixLen = sizeof(kMentionSigDomain) - 1U;
            payload.reserve(prefixLen + USER_ID_SIZE * 2 + 4 + 8 + 4 + mentionedUserIds.size() * USER_ID_SIZE);
            payload.insert(payload.end(), kMentionSigDomain, kMentionSigDomain + prefixLen);
            payload.insert(payload.end(), groupId.begin(), groupId.end());
            payload.insert(payload.end(), senderId.begin(), senderId.end());
            AppendI32Be(payload, mentionType);
            AppendU64Be(payload, nowMs);
            AppendI32Be(payload, static_cast<int32_t>(mentionedUserIds.size()));
            for (const auto& uid : mentionedUserIds) {
                if (uid.size() != USER_ID_SIZE) {
                    return false;
                }
                payload.insert(payload.end(), uid.begin(), uid.end());
            }
        }

        if (!common::Ed25519VerifyDetached(
                payload.data(),
                payload.size(),
                signatureEd25519.data(),
                pubkey.data())) {
            return false;
        }

        bool senderIn = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, senderId, senderIn) || !senderIn) {
            return false;
        }

        if (mentionType == 1) {
            if (mentionedUserIds.empty()) {
                return false;
            }
            for (const auto& uid : mentionedUserIds) {
                bool in = false;
                if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, uid, in) || !in) {
                    return false;
                }
            }
            return true;
        }

        if (mentionType == 2) {
            int32_t role = kRoleMember;
            int64_t j    = 0;
            if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, senderId, role, j)) {
                return false;
            }
            if (role != kRoleAdmin && role != kRoleOwner) {
                return false;
            }
            std::vector<uint64_t> vec;
            if (!mm2::MM2::Instance().Mm1MentionAtAllLoadTimes(groupId, senderId, vec)) {
                return false;
            }
            PruneAtAllWindow(vec, nowMs, kAtAllWindowMs);
            return vec.size() < kMaxAtAllPerWindow;
        }

        return false;
    }

    bool MentionPermissionManager::RecordMentionAtAllUsage(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& senderId,
        uint64_t                    nowMs)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (groupId.size() != USER_ID_SIZE || senderId.size() != USER_ID_SIZE) {
            return false;
        }
        bool in = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, senderId, in) || !in) {
            return false;
        }

        std::vector<uint64_t> vec;
        if (!mm2::MM2::Instance().Mm1MentionAtAllLoadTimes(groupId, senderId, vec)) {
            return false;
        }
        PruneAtAllWindow(vec, nowMs, kAtAllWindowMs);
        if (vec.size() >= kMaxAtAllPerWindow) {
            return false;
        }
        vec.push_back(nowMs);
        return mm2::MM2::Instance().Mm1MentionAtAllStoreTimes(groupId, senderId, vec);
    }

    void MentionPermissionManager::ClearAtAllRateLimitState()
    {
        (void)mm2::MM2::Instance().Mm1ClearAllMentionAtAllWindows();
    }

} // namespace ZChatIM::mm1
