#include "mm1/managers/RtcCallSessionManager.h"
#include "Types.h"
#include "mm2/storage/Crypto.h"

#include <array>
#include <cstring>
#include <map>
#include <mutex>

namespace ZChatIM::mm1 {

    struct RtcCallSessionManager::Impl {
        std::mutex mutex;
        struct Entry {
            std::array<uint8_t, USER_ID_SIZE> initiator{};
            std::array<uint8_t, USER_ID_SIZE> peer{};
            int32_t                           kind   = RTC_CALL_KIND_AUDIO;
            int32_t                           state  = RTC_CALL_STATE_RINGING;
        };
        std::map<std::vector<uint8_t>, Entry> byCallId;
    };

    RtcCallSessionManager::RtcCallSessionManager()
        : impl_(std::make_unique<Impl>())
    {
    }

    RtcCallSessionManager::~RtcCallSessionManager() = default;

    void RtcCallSessionManager::ClearAll()
    {
        if (!impl_) {
            return;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        impl_->byCallId.clear();
    }

    static bool SameUser(const std::vector<uint8_t>& a, const std::array<uint8_t, USER_ID_SIZE>& b)
    {
        if (a.size() != USER_ID_SIZE) {
            return false;
        }
        return std::memcmp(a.data(), b.data(), USER_ID_SIZE) == 0;
    }

    std::vector<uint8_t> RtcCallSessionManager::StartCall(
        const std::vector<uint8_t>& initiatorUserId,
        const std::vector<uint8_t>& peerUserId,
        int32_t                     callKind)
    {
        std::vector<uint8_t> empty;
        if (!impl_) {
            return empty;
        }
        if (initiatorUserId.size() != USER_ID_SIZE || peerUserId.size() != USER_ID_SIZE) {
            return empty;
        }
        if (callKind != RTC_CALL_KIND_AUDIO && callKind != RTC_CALL_KIND_VIDEO) {
            return empty;
        }
        if (std::memcmp(initiatorUserId.data(), peerUserId.data(), USER_ID_SIZE) == 0) {
            return empty;
        }

        std::vector<uint8_t> callId = mm2::Crypto::GenerateSecureRandom(MESSAGE_ID_SIZE);
        if (callId.size() != MESSAGE_ID_SIZE) {
            return empty;
        }

        Impl::Entry e{};
        std::memcpy(e.initiator.data(), initiatorUserId.data(), USER_ID_SIZE);
        std::memcpy(e.peer.data(), peerUserId.data(), USER_ID_SIZE);
        e.kind  = callKind;
        e.state = RTC_CALL_STATE_RINGING;

        std::lock_guard<std::mutex> lk(impl_->mutex);
        if (impl_->byCallId.size() >= 100000) {
            return empty;
        }
        impl_->byCallId[callId] = std::move(e);
        return callId;
    }

    bool RtcCallSessionManager::AcceptCall(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId)
    {
        if (!impl_ || callId.size() != MESSAGE_ID_SIZE || principalUserId.size() != USER_ID_SIZE) {
            return false;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        const auto it = impl_->byCallId.find(callId);
        if (it == impl_->byCallId.end()) {
            return false;
        }
        if (it->second.state != RTC_CALL_STATE_RINGING) {
            return false;
        }
        if (!SameUser(principalUserId, it->second.peer)) {
            return false;
        }
        it->second.state = RTC_CALL_STATE_CONNECTED;
        return true;
    }

    bool RtcCallSessionManager::RejectCall(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId)
    {
        if (!impl_ || callId.size() != MESSAGE_ID_SIZE || principalUserId.size() != USER_ID_SIZE) {
            return false;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        const auto it = impl_->byCallId.find(callId);
        if (it == impl_->byCallId.end()) {
            return false;
        }
        if (it->second.state != RTC_CALL_STATE_RINGING) {
            return false;
        }
        if (!SameUser(principalUserId, it->second.peer) && !SameUser(principalUserId, it->second.initiator)) {
            return false;
        }
        it->second.state = RTC_CALL_STATE_REJECTED;
        return true;
    }

    bool RtcCallSessionManager::EndCall(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId)
    {
        if (!impl_ || callId.size() != MESSAGE_ID_SIZE || principalUserId.size() != USER_ID_SIZE) {
            return false;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        const auto it = impl_->byCallId.find(callId);
        if (it == impl_->byCallId.end()) {
            return false;
        }
        if (!SameUser(principalUserId, it->second.peer) && !SameUser(principalUserId, it->second.initiator)) {
            return false;
        }
        if (it->second.state == RTC_CALL_STATE_ENDED || it->second.state == RTC_CALL_STATE_REJECTED) {
            return false;
        }
        it->second.state = RTC_CALL_STATE_ENDED;
        return true;
    }

    int32_t RtcCallSessionManager::GetCallState(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId)
    {
        if (!impl_ || callId.size() != MESSAGE_ID_SIZE || principalUserId.size() != USER_ID_SIZE) {
            return RTC_CALL_STATE_INVALID;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        const auto it = impl_->byCallId.find(callId);
        if (it == impl_->byCallId.end()) {
            return RTC_CALL_STATE_INVALID;
        }
        if (!SameUser(principalUserId, it->second.peer) && !SameUser(principalUserId, it->second.initiator)) {
            return RTC_CALL_STATE_INVALID;
        }
        return it->second.state;
    }

    int32_t RtcCallSessionManager::GetCallKind(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId)
    {
        if (!impl_ || callId.size() != MESSAGE_ID_SIZE || principalUserId.size() != USER_ID_SIZE) {
            return -1;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        const auto it = impl_->byCallId.find(callId);
        if (it == impl_->byCallId.end()) {
            return -1;
        }
        if (!SameUser(principalUserId, it->second.peer) && !SameUser(principalUserId, it->second.initiator)) {
            return -1;
        }
        return it->second.kind;
    }

} // namespace ZChatIM::mm1
