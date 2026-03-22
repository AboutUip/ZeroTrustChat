#include "mm1/managers/RtcCallManager.h"
#include "Types.h"
#include "common/Memory.h"
#include "mm2/storage/Crypto.h"

#include <cstring>
#include <utility>

namespace ZChatIM::mm1 {

    RtcCallManager::RtcCallManager()  = default;
    RtcCallManager::~RtcCallManager() = default;

    std::vector<uint8_t> RtcCallManager::StartCall(
        const std::vector<uint8_t>& ownerUserId,
        const std::vector<uint8_t>& imSessionId,
        const std::vector<uint8_t>& peerUserId,
        int32_t                     callType)
    {
        if (ownerUserId.size() != USER_ID_SIZE || imSessionId.size() != USER_ID_SIZE
            || peerUserId.size() != USER_ID_SIZE) {
            return {};
        }
        if (callType != 1 && callType != 2) {
            return {};
        }
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<uint8_t>        callId;
        callId.resize(MESSAGE_ID_SIZE);
        bool                        okId = false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::vector<uint8_t> rnd = mm2::Crypto::GenerateSecureRandom(MESSAGE_ID_SIZE);
            if (rnd.size() != MESSAGE_ID_SIZE) {
                continue;
            }
            std::memcpy(callId.data(), rnd.data(), MESSAGE_ID_SIZE);
            if (activeByCallId_.find(callId) == activeByCallId_.end()) {
                okId = true;
                break;
            }
        }
        if (!okId) {
            return {};
        }
        Entry e;
        e.ownerUserId = ownerUserId;
        e.imSessionId = imSessionId;
        e.peerUserId  = peerUserId;
        e.callType    = callType;
        const std::vector<uint8_t> ret = callId;
        activeByCallId_.emplace(std::move(callId), std::move(e));
        return ret;
    }

    bool RtcCallManager::EndCall(const std::vector<uint8_t>& ownerUserId, const std::vector<uint8_t>& callId)
    {
        if (ownerUserId.size() != USER_ID_SIZE || callId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        const auto                  it = activeByCallId_.find(callId);
        if (it == activeByCallId_.end()) {
            return false;
        }
        if (!common::Memory::ConstantTimeCompare(
                it->second.ownerUserId.data(),
                ownerUserId.data(),
                USER_ID_SIZE)) {
            return false;
        }
        activeByCallId_.erase(it);
        return true;
    }

    int32_t RtcCallManager::GetCallState(
        const std::vector<uint8_t>& ownerUserId,
        const std::vector<uint8_t>& callId) const
    {
        if (ownerUserId.size() != USER_ID_SIZE || callId.size() != MESSAGE_ID_SIZE) {
            return 0;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        const auto                  it = activeByCallId_.find(callId);
        if (it == activeByCallId_.end()) {
            return 0;
        }
        if (!common::Memory::ConstantTimeCompare(
                it->second.ownerUserId.data(),
                ownerUserId.data(),
                USER_ID_SIZE)) {
            return 0;
        }
        return 1;
    }

    void RtcCallManager::ClearAll()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        activeByCallId_.clear();
    }

} // namespace ZChatIM::mm1
