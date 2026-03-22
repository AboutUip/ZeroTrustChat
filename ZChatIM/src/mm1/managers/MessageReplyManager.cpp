// MessageReplyManager: MM1 安全入口 → 会话绑定 + Ed25519 验签 → MM2 落库。
// 文档：仓库根 `docs/02-Core/05-ZChatIM-Implementation-Status.md` §3、`ZChatIM/docs/JNI-API-Documentation.md`。

#include "mm1/managers/MessageReplyManager.h"

#include "common/Ed25519.h"
#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr size_t kEd25519PublicKeyBytes  = 32;
        constexpr size_t kEd25519SignatureBytes  = 64;
        constexpr char   kReplySigDomain[]       = "ZChatIM|StoreMessageReplyRelation|v1";

        bool buildReplyCanonicalPayload(
            const std::vector<uint8_t>& messageId,
            const std::vector<uint8_t>& repliedMsgId,
            const std::vector<uint8_t>& repliedSenderId,
            const std::vector<uint8_t>& repliedContentDigest,
            const std::vector<uint8_t>& senderId,
            std::vector<uint8_t>&       out)
        {
            out.clear();
            const size_t prefixLen = sizeof(kReplySigDomain) - 1U;
            out.reserve(prefixLen + MESSAGE_ID_SIZE * 2 + USER_ID_SIZE * 2 + SHA256_SIZE);
            out.insert(out.end(), kReplySigDomain, kReplySigDomain + prefixLen);
            out.insert(out.end(), messageId.begin(), messageId.end());
            out.insert(out.end(), repliedMsgId.begin(), repliedMsgId.end());
            out.insert(out.end(), repliedSenderId.begin(), repliedSenderId.end());
            out.insert(out.end(), repliedContentDigest.begin(), repliedContentDigest.end());
            out.insert(out.end(), senderId.begin(), senderId.end());
            return true;
        }

    } // namespace

    bool MessageReplyManager::StoreMessageReplyRelation(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& senderEd25519PublicKey,
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& repliedMsgId,
        const std::vector<uint8_t>& repliedSenderId,
        const std::vector<uint8_t>& repliedContentDigest,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint8_t>& signatureEd25519)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (callerSessionId.size() != JNI_AUTH_SESSION_TOKEN_BYTES) {
            return false;
        }
        if (senderEd25519PublicKey.size() != kEd25519PublicKeyBytes) {
            return false;
        }
        if (messageId.size() != MESSAGE_ID_SIZE || repliedMsgId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        if (repliedSenderId.size() != USER_ID_SIZE || senderId.size() != USER_ID_SIZE) {
            return false;
        }
        if (repliedContentDigest.size() != SHA256_SIZE) {
            return false;
        }
        if (signatureEd25519.size() != kEd25519SignatureBytes) {
            return false;
        }

        std::vector<uint8_t> principal;
        if (!MM1::Instance().GetAuthSessionManager().TryGetSessionUserId(callerSessionId, principal)) {
            return false;
        }
        if (principal.size() != USER_ID_SIZE) {
            return false;
        }
        if (!common::Memory::ConstantTimeCompare(principal.data(), senderId.data(), USER_ID_SIZE)) {
            return false;
        }

        std::vector<uint8_t> payload;
        buildReplyCanonicalPayload(messageId, repliedMsgId, repliedSenderId, repliedContentDigest, senderId, payload);

        if (!common::Ed25519VerifyDetached(
                payload.data(),
                payload.size(),
                signatureEd25519.data(),
                senderEd25519PublicKey.data())) {
            return false;
        }

        return mm2::MM2::Instance().StoreMessageReplyRelation(
            messageId,
            repliedMsgId,
            repliedSenderId,
            repliedContentDigest);
    }

} // namespace ZChatIM::mm1
