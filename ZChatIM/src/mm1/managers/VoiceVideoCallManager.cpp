#include "mm1/managers/VoiceVideoCallManager.h"
#include "mm1/managers/RtcCallSessionManager.h"
#include "Types.h"

namespace ZChatIM::mm1 {

    VoiceVideoCallManager::VoiceVideoCallManager()  = default;
    VoiceVideoCallManager::~VoiceVideoCallManager() = default;

    void VoiceVideoCallManager::ClearAllCalls()
    {
        if (rtc_) {
            rtc_->ClearAll();
        }
    }

    std::vector<uint8_t> VoiceVideoCallManager::StartVoiceCall(
        const std::vector<uint8_t>& initiatorUserId,
        const std::vector<uint8_t>& peerUserId)
    {
        if (!rtc_) {
            return {};
        }
        return rtc_->StartCall(initiatorUserId, peerUserId, RTC_CALL_KIND_AUDIO);
    }

    std::vector<uint8_t> VoiceVideoCallManager::StartVideoCall(
        const std::vector<uint8_t>& initiatorUserId,
        const std::vector<uint8_t>& peerUserId)
    {
        if (!rtc_) {
            return {};
        }
        return rtc_->StartCall(initiatorUserId, peerUserId, RTC_CALL_KIND_VIDEO);
    }

    bool VoiceVideoCallManager::EndCall(
        const std::vector<uint8_t>& initiatorUserId,
        const std::vector<uint8_t>& callId)
    {
        if (!rtc_ || initiatorUserId.size() != USER_ID_SIZE || callId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        if (!rtc_->IsInitiator(initiatorUserId, callId)) {
            return false;
        }
        return rtc_->EndCall(initiatorUserId, callId);
    }

} // namespace ZChatIM::mm1
