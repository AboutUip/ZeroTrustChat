#include "mm1/managers/MediaCallCoordinator.h"
#include "Types.h"
#include "common/Memory.h"

#include <chrono>

#include <openssl/rand.h>

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

    } // namespace

    MediaCallCoordinator::MediaCallCoordinator()  = default;
    MediaCallCoordinator::~MediaCallCoordinator() = default;

    std::vector<uint8_t> MediaCallCoordinator::StartCall(
        const std::vector<uint8_t>& ownerUserId16,
        const std::vector<uint8_t>& remoteUserId16,
        int32_t                     mediaKind)
    {
        if (ownerUserId16.size() != USER_ID_SIZE || remoteUserId16.size() != USER_ID_SIZE) {
            return {};
        }
        if (mediaKind != MEDIA_CALL_KIND_AUDIO && mediaKind != MEDIA_CALL_KIND_VIDEO) {
            return {};
        }

        const std::lock_guard<std::mutex> lk(mutex_);

        size_t nForOwner = 0;
        for (const auto& pr : byCallId_) {
            if (pr.second.ownerUserId.size() == USER_ID_SIZE
                && common::Memory::ConstantTimeCompare(
                    pr.second.ownerUserId.data(), ownerUserId16.data(), USER_ID_SIZE)) {
                ++nForOwner;
            }
        }
        constexpr size_t kMaxLegsPerOwner = 4;
        if (nForOwner >= kMaxLegsPerOwner) {
            return {};
        }

        std::vector<uint8_t> callId;
        callId.resize(USER_ID_SIZE);
        bool okId = false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (RAND_bytes(callId.data(), static_cast<int>(callId.size())) != 1) {
                return {};
            }
            if (byCallId_.find(callId) == byCallId_.end()) {
                okId = true;
                break;
            }
        }
        if (!okId) {
            return {};
        }

        Entry e;
        e.ownerUserId  = ownerUserId16;
        e.remoteUserId = remoteUserId16;
        e.mediaKind    = mediaKind;
        e.startedMs    = UnixEpochMs();

        std::vector<uint8_t> ret = callId;
        byCallId_.emplace(std::move(callId), std::move(e));
        return ret;
    }

    bool MediaCallCoordinator::EndCall(const std::vector<uint8_t>& ownerUserId16, const std::vector<uint8_t>& callId16)
    {
        if (ownerUserId16.size() != USER_ID_SIZE || callId16.size() != USER_ID_SIZE) {
            return false;
        }
        const std::lock_guard<std::mutex> lk(mutex_);
        const auto                         it = byCallId_.find(callId16);
        if (it == byCallId_.end()) {
            return false;
        }
        if (it->second.ownerUserId.size() != USER_ID_SIZE
            || !common::Memory::ConstantTimeCompare(
                it->second.ownerUserId.data(), ownerUserId16.data(), USER_ID_SIZE)) {
            return false;
        }
        byCallId_.erase(it);
        return true;
    }

    std::vector<std::vector<uint8_t>> MediaCallCoordinator::ListCalls(const std::vector<uint8_t>& ownerUserId16)
    {
        std::vector<std::vector<uint8_t>> out;
        if (ownerUserId16.size() != USER_ID_SIZE) {
            return out;
        }
        const std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& pr : byCallId_) {
            const Entry& e = pr.second;
            if (e.ownerUserId.size() != USER_ID_SIZE
                || !common::Memory::ConstantTimeCompare(
                    e.ownerUserId.data(), ownerUserId16.data(), USER_ID_SIZE)) {
                continue;
            }
            std::vector<uint8_t> row;
            row.reserve(16 + 16 + 4 + 8);
            row.insert(row.end(), pr.first.begin(), pr.first.end());
            row.insert(row.end(), e.remoteUserId.begin(), e.remoteUserId.end());
            AppendU32Be(row, static_cast<uint32_t>(e.mediaKind));
            AppendU64Be(row, e.startedMs);
            out.push_back(std::move(row));
        }
        return out;
    }

    void MediaCallCoordinator::ClearAll()
    {
        const std::lock_guard<std::mutex> lk(mutex_);
        byCallId_.clear();
    }

} // namespace ZChatIM::mm1
