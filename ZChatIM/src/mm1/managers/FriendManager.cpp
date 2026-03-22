// FriendManager：验签（FriendVerificationManager）后委托 MM2::friend_requests；好友列表为 status=1 的边。
#include "mm1/managers/FriendManager.h"

#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <mutex>
#include <vector>

namespace ZChatIM::mm1 {

    std::vector<uint8_t> FriendManager::SendFriendRequest(
        const std::vector<uint8_t>& fromUserId,
        const std::vector<uint8_t>& toUserId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (fromUserId.size() != USER_ID_SIZE || toUserId.size() != USER_ID_SIZE) {
            return {};
        }
        if (common::Memory::ConstantTimeCompare(fromUserId.data(), toUserId.data(), USER_ID_SIZE)) {
            return {};
        }
        if (!MM1::Instance().GetFriendVerificationManager().VerifyFriendRequestSignature(
                fromUserId, toUserId, timestampSeconds, signatureEd25519)) {
            return {};
        }
        std::vector<uint8_t> rid;
        if (!mm2::MM2::Instance().StoreFriendRequest(
                fromUserId, toUserId, timestampSeconds, signatureEd25519, rid)) {
            return {};
        }
        return rid;
    }

    bool FriendManager::RespondFriendRequest(
        const std::vector<uint8_t>& requestId,
        bool                        accept,
        const std::vector<uint8_t>& responderId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (requestId.size() != MESSAGE_ID_SIZE || responderId.size() != USER_ID_SIZE) {
            return false;
        }
        std::vector<uint8_t> fromU, toU;
        int32_t              st = -1;
        if (!mm2::MM2::Instance().GetFriendRequestRowForMm1(requestId, fromU, toU, st)) {
            return false;
        }
        if (st != 0) {
            return false;
        }
        if (!common::Memory::ConstantTimeCompare(responderId.data(), toU.data(), USER_ID_SIZE)) {
            return false;
        }
        if (!MM1::Instance().GetFriendVerificationManager().VerifyFriendResponseSignature(
                requestId, accept, responderId, timestampSeconds, signatureEd25519)) {
            return false;
        }
        return mm2::MM2::Instance().UpdateFriendRequestStatus(
            requestId, accept, responderId, timestampSeconds, signatureEd25519);
    }

    bool FriendManager::DeleteFriend(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& friendId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (userId.size() != USER_ID_SIZE || friendId.size() != USER_ID_SIZE) {
            return false;
        }
        if (common::Memory::ConstantTimeCompare(userId.data(), friendId.data(), USER_ID_SIZE)) {
            return false;
        }
        if (!MM1::Instance().GetFriendVerificationManager().VerifyDeleteFriendSignature(
                userId, friendId, timestampSeconds, signatureEd25519)) {
            return false;
        }
        return mm2::MM2::Instance().DeleteAcceptedFriendshipBetweenForMm1(userId, friendId);
    }

    std::vector<std::vector<uint8_t>> FriendManager::GetFriends(const std::vector<uint8_t>& userId)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        std::vector<std::vector<uint8_t>> out;
        if (userId.size() != USER_ID_SIZE) {
            return out;
        }
        if (!mm2::MM2::Instance().ListAcceptedFriendUserIdsForMm1(userId, out)) {
            out.clear();
        }
        return out;
    }

} // namespace ZChatIM::mm1
