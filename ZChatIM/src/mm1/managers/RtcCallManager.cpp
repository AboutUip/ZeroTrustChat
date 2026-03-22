#include "mm1/managers/RtcCallManager.h"
#include "mm1/managers/RtcCallSessionManager.h"
#include "Types.h"
#include "common/Memory.h"

namespace ZChatIM::mm1 {

    RtcCallManager::RtcCallManager()  = default;
    RtcCallManager::~RtcCallManager() = default;

    std::vector<uint8_t> RtcCallManager::StartCall(
        const std::vector<uint8_t>& ownerUserId,
        const std::vector<uint8_t>& imSessionId,
        const std::vector<uint8_t>& peerUserId,
        int32_t                     callType)
    {
        if (!rtc_ || ownerUserId.size() != USER_ID_SIZE || imSessionId.size() != USER_ID_SIZE
            || peerUserId.size() != USER_ID_SIZE) {
            return {};
        }
        if (callType != 1 && callType != 2) {
            return {};
        }
        const int32_t kind = (callType == 1) ? RTC_CALL_KIND_AUDIO : RTC_CALL_KIND_VIDEO;
        std::vector<uint8_t> callId = rtc_->StartCall(ownerUserId, peerUserId, kind);
        if (callId.size() != MESSAGE_ID_SIZE) {
            return {};
        }
        Entry e;
        e.ownerUserId = ownerUserId;
        e.imSessionId = imSessionId;
        e.peerUserId  = peerUserId;
        e.callType    = callType;
        const std::lock_guard<std::mutex> lk(mutex_);
        activeByCallId_[callId] = std::move(e);
        return callId;
    }

    bool RtcCallManager::EndCall(const std::vector<uint8_t>& ownerUserId, const std::vector<uint8_t>& callId)
    {
        if (!rtc_ || ownerUserId.size() != USER_ID_SIZE || callId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        if (!rtc_->EndCall(ownerUserId, callId)) {
            return false;
        }
        const std::lock_guard<std::mutex> lk(mutex_);
        activeByCallId_.erase(callId);
        return true;
    }

    // 见 **`RtcCallManager.h`**：0/1 简化语义，**非** **`RtcCallSessionManager::RTC_CALL_STATE_*`** 全量枚举。
    int32_t RtcCallManager::GetCallState(
        const std::vector<uint8_t>& ownerUserId,
        const std::vector<uint8_t>& callId)
    {
        if (!rtc_ || ownerUserId.size() != USER_ID_SIZE || callId.size() != MESSAGE_ID_SIZE) {
            return 0;
        }
        {
            const std::lock_guard<std::mutex> lk(mutex_);
            const auto it = activeByCallId_.find(callId);
            if (it == activeByCallId_.end()) {
                return 0;
            }
            if (!common::Memory::ConstantTimeCompare(
                    it->second.ownerUserId.data(),
                    ownerUserId.data(),
                    USER_ID_SIZE)) {
                return 0;
            }
        }
        const int32_t st = rtc_->GetCallState(ownerUserId, callId);
        if (st == RTC_CALL_STATE_INVALID || st == RTC_CALL_STATE_ENDED || st == RTC_CALL_STATE_REJECTED) {
            return 0;
        }
        return 1;
    }

    void RtcCallManager::ClearAll()
    {
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> ownerAndCallId;
        {
            const std::lock_guard<std::mutex> lk(mutex_);
            ownerAndCallId.reserve(activeByCallId_.size());
            for (const auto& pr : activeByCallId_) {
                ownerAndCallId.emplace_back(pr.second.ownerUserId, pr.first);
            }
        }
        if (rtc_) {
            for (const auto& p : ownerAndCallId) {
                const auto& owner = p.first;
                const auto& cid  = p.second;
                if (owner.size() == USER_ID_SIZE && cid.size() == MESSAGE_ID_SIZE) {
                    (void)rtc_->EndCall(owner, cid);
                }
            }
        }
        const std::lock_guard<std::mutex> lk(mutex_);
        activeByCallId_.clear();
    }

} // namespace ZChatIM::mm1
