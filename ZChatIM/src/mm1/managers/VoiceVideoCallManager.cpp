#include "mm1/managers/VoiceVideoCallManager.h"
#include "Types.h"
#include "common/Memory.h"
#include "mm2/storage/Crypto.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace ZChatIM::mm1 {
    namespace {

        constexpr size_t kMaxCalls = 4096;

        struct CallRecord {
            std::vector<uint8_t> initiator;
            std::vector<uint8_t> peer;
            bool                 video   = false;
            uint64_t             startMs = 0;
        };

        uint64_t NowUnixMs()
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }

    } // namespace

    struct VoiceVideoCallManager::Impl {
        std::mutex                        mutex;
        std::map<std::vector<uint8_t>, CallRecord> byCallId;
    };

    VoiceVideoCallManager::VoiceVideoCallManager()
        : impl_(std::make_unique<Impl>())
    {
    }

    VoiceVideoCallManager::~VoiceVideoCallManager() = default;

    void VoiceVideoCallManager::ClearAllCalls()
    {
        if (!impl_) {
            return;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        impl_->byCallId.clear();
    }

    std::vector<uint8_t> VoiceVideoCallManager::StartVoiceCall(
        const std::vector<uint8_t>& initiatorUserId,
        const std::vector<uint8_t>& peerUserId)
    {
        if (!impl_) {
            return {};
        }
        if (initiatorUserId.size() != USER_ID_SIZE || peerUserId.size() != USER_ID_SIZE) {
            return {};
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        if (impl_->byCallId.size() >= kMaxCalls) {
            return {};
        }
        std::vector<uint8_t> callId = mm2::Crypto::GenerateSecureRandom(MESSAGE_ID_SIZE);
        if (callId.size() != MESSAGE_ID_SIZE) {
            return {};
        }
        if (impl_->byCallId.find(callId) != impl_->byCallId.end()) {
            return {};
        }
        CallRecord rec;
        rec.initiator = initiatorUserId;
        rec.peer      = peerUserId;
        rec.video     = false;
        rec.startMs   = NowUnixMs();
        const std::vector<uint8_t> outId = callId;
        impl_->byCallId.emplace(std::move(callId), std::move(rec));
        return outId;
    }

    std::vector<uint8_t> VoiceVideoCallManager::StartVideoCall(
        const std::vector<uint8_t>& initiatorUserId,
        const std::vector<uint8_t>& peerUserId)
    {
        if (!impl_) {
            return {};
        }
        if (initiatorUserId.size() != USER_ID_SIZE || peerUserId.size() != USER_ID_SIZE) {
            return {};
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        if (impl_->byCallId.size() >= kMaxCalls) {
            return {};
        }
        std::vector<uint8_t> callId = mm2::Crypto::GenerateSecureRandom(MESSAGE_ID_SIZE);
        if (callId.size() != MESSAGE_ID_SIZE) {
            return {};
        }
        if (impl_->byCallId.find(callId) != impl_->byCallId.end()) {
            return {};
        }
        CallRecord rec;
        rec.initiator = initiatorUserId;
        rec.peer      = peerUserId;
        rec.video     = true;
        rec.startMs   = NowUnixMs();
        const std::vector<uint8_t> out = callId;
        impl_->byCallId.emplace(std::move(callId), std::move(rec));
        return out;
    }

    bool VoiceVideoCallManager::EndCall(
        const std::vector<uint8_t>& initiatorUserId,
        const std::vector<uint8_t>& callId)
    {
        if (!impl_) {
            return false;
        }
        if (initiatorUserId.size() != USER_ID_SIZE || callId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        std::lock_guard<std::mutex> lk(impl_->mutex);
        const auto it = impl_->byCallId.find(callId);
        if (it == impl_->byCallId.end()) {
            return false;
        }
        if (it->second.initiator.size() != USER_ID_SIZE
            || !common::Memory::ConstantTimeCompare(
                it->second.initiator.data(), initiatorUserId.data(), USER_ID_SIZE)) {
            return false;
        }
        impl_->byCallId.erase(it);
        return true;
    }

} // namespace ZChatIM::mm1
