#include "mm1/managers/MediaCallCoordinator.h"
#include "mm1/managers/RtcCallSessionManager.h"
#include "Types.h"

#include <chrono>

namespace ZChatIM::mm1 {
    namespace {

        uint64_t UnixEpochMs()
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }

        void AppendU32Be(std::vector<uint8_t>& v, uint32_t x)
        {
            v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>(x & 0xFF));
        }

        void AppendU64Be(std::vector<uint8_t>& v, uint64_t x)
        {
            for (int s = 56; s >= 0; s -= 8) {
                v.push_back(static_cast<uint8_t>((x >> s) & 0xFF));
            }
        }

        constexpr size_t kRtcListRowBytes = MESSAGE_ID_SIZE + USER_ID_SIZE + 4 + 4;

    } // namespace

    MediaCallCoordinator::MediaCallCoordinator()  = default;
    MediaCallCoordinator::~MediaCallCoordinator() = default;

    std::vector<uint8_t> MediaCallCoordinator::StartCall(
        const std::vector<uint8_t>& ownerUserId16,
        const std::vector<uint8_t>& remoteUserId16,
        int32_t                     mediaKind)
    {
        if (!rtc_ || ownerUserId16.size() != USER_ID_SIZE || remoteUserId16.size() != USER_ID_SIZE) {
            return {};
        }
        if (mediaKind != MEDIA_CALL_KIND_AUDIO && mediaKind != MEDIA_CALL_KIND_VIDEO) {
            return {};
        }

        const std::vector<std::vector<uint8_t>> existing = rtc_->ListCallsForUser(ownerUserId16);
        constexpr size_t                        kMaxLegsPerOwner = 4;
        if (existing.size() >= kMaxLegsPerOwner) {
            return {};
        }

        std::vector<uint8_t> callId = rtc_->StartCall(ownerUserId16, remoteUserId16, mediaKind);
        if (callId.size() != MESSAGE_ID_SIZE) {
            return {};
        }
        const std::lock_guard<std::mutex> lk(mutex_);
        startedMsByCallId_[callId] = UnixEpochMs();
        return callId;
    }

    bool MediaCallCoordinator::EndCall(const std::vector<uint8_t>& ownerUserId16, const std::vector<uint8_t>& callId16)
    {
        if (!rtc_ || ownerUserId16.size() != USER_ID_SIZE || callId16.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        if (!rtc_->EndCall(ownerUserId16, callId16)) {
            return false;
        }
        const std::lock_guard<std::mutex> lk(mutex_);
        startedMsByCallId_.erase(callId16);
        return true;
    }

    std::vector<std::vector<uint8_t>> MediaCallCoordinator::ListCalls(const std::vector<uint8_t>& ownerUserId16)
    {
        std::vector<std::vector<uint8_t>> out;
        if (!rtc_ || ownerUserId16.size() != USER_ID_SIZE) {
            return out;
        }
        const std::vector<std::vector<uint8_t>> base = rtc_->ListCallsForUser(ownerUserId16);
        const std::lock_guard<std::mutex>         lk(mutex_);
        for (const auto& row : base) {
            if (row.size() < kRtcListRowBytes) {
                continue;
            }
            std::vector<uint8_t> callId(row.begin(), row.begin() + static_cast<std::ptrdiff_t>(MESSAGE_ID_SIZE));
            std::vector<uint8_t> remote(
                row.begin() + static_cast<std::ptrdiff_t>(MESSAGE_ID_SIZE),
                row.begin() + static_cast<std::ptrdiff_t>(MESSAGE_ID_SIZE + USER_ID_SIZE));
            const uint32_t kind = (static_cast<uint32_t>(row[MESSAGE_ID_SIZE + USER_ID_SIZE]) << 24)
                | (static_cast<uint32_t>(row[MESSAGE_ID_SIZE + USER_ID_SIZE + 1]) << 16)
                | (static_cast<uint32_t>(row[MESSAGE_ID_SIZE + USER_ID_SIZE + 2]) << 8)
                | static_cast<uint32_t>(row[MESSAGE_ID_SIZE + USER_ID_SIZE + 3]);
            uint64_t startedMs = 0;
            const auto it      = startedMsByCallId_.find(callId);
            if (it != startedMsByCallId_.end()) {
                startedMs = it->second;
            }
            std::vector<uint8_t> packed;
            packed.reserve(44);
            packed.insert(packed.end(), callId.begin(), callId.end());
            packed.insert(packed.end(), remote.begin(), remote.end());
            AppendU32Be(packed, kind);
            AppendU64Be(packed, startedMs);
            out.push_back(std::move(packed));
        }
        return out;
    }

    void MediaCallCoordinator::ClearAll()
    {
        const std::lock_guard<std::mutex> lk(mutex_);
        startedMsByCallId_.clear();
    }

} // namespace ZChatIM::mm1
