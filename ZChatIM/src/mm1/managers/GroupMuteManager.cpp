// GroupMuteManager：群禁言 **`mm2_group_mute`**（**MM2 / SqliteMetadataDb**）。
#include "mm1/managers/GroupMuteManager.h"

#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <climits>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr int32_t kRoleMember = 0;
        constexpr int32_t kRoleAdmin  = 1;
        constexpr int32_t kRoleOwner  = 2;

        constexpr size_t kMaxReasonBytes = 4096;

        bool IsOwnerOrAdminRole(int32_t r)
        {
            return r == kRoleAdmin || r == kRoleOwner;
        }

        bool UserIdsEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
        {
            if (a.size() != USER_ID_SIZE || b.size() != USER_ID_SIZE) {
                return false;
            }
            return common::Memory::ConstantTimeCompare(a.data(), b.data(), USER_ID_SIZE);
        }

        bool ActorCanMuteTarget(int32_t actorRole, int32_t targetRole)
        {
            if (actorRole == kRoleOwner) {
                return targetRole != kRoleOwner;
            }
            if (actorRole == kRoleAdmin) {
                return targetRole == kRoleMember;
            }
            return false;
        }

        bool ValidateTimedMuteEnd(int64_t startMs, int64_t durationS, std::string& errOut)
        {
            errOut.clear();
            if (durationS <= 0) {
                errOut = "durationSeconds must be -1 (permanent) or positive";
                return false;
            }
            if (durationS > static_cast<int64_t>(INT64_MAX / 1000)) {
                errOut = "durationSeconds too large";
                return false;
            }
            const int64_t addMs = durationS * 1000;
            if (addMs / 1000 != durationS) {
                errOut = "durationSeconds * 1000 overflow";
                return false;
            }
            if (startMs > 0 && addMs > INT64_MAX - startMs) {
                errOut = "start_ms + duration overflow";
                return false;
            }
            return true;
        }

    } // namespace

    bool GroupMuteManager::MuteMember(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& mutedBy,
        uint64_t                    startTimeMs,
        int64_t                     durationSeconds,
        const std::vector<uint8_t>& reason)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE || mutedBy.size() != USER_ID_SIZE) {
            return false;
        }
        if (UserIdsEqual(mutedBy, userId)) {
            return false;
        }
        if (reason.size() > kMaxReasonBytes) {
            return false;
        }
        if (startTimeMs > static_cast<uint64_t>(INT64_MAX)) {
            return false;
        }
        const int64_t startMs = static_cast<int64_t>(startTimeMs);
        if (durationSeconds == -1) {
            // permanent
        } else if (durationSeconds <= 0) {
            return false;
        } else {
            std::string errV;
            if (!ValidateTimedMuteEnd(startMs, durationSeconds, errV)) {
                return false;
            }
        }

        int32_t actorRole = 0;
        int64_t jActor    = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, mutedBy, actorRole, jActor)) {
            return false;
        }
        if (!IsOwnerOrAdminRole(actorRole)) {
            return false;
        }

        bool actorInGroup = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, mutedBy, actorInGroup) || !actorInGroup) {
            return false;
        }

        bool targetInGroup = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, userId, targetInGroup) || !targetInGroup) {
            return false;
        }

        int32_t targetRole = 0;
        int64_t jTarget    = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, userId, targetRole, jTarget)) {
            return false;
        }
        if (!ActorCanMuteTarget(actorRole, targetRole)) {
            return false;
        }

        return mm2::MM2::Instance().UpsertGroupMuteForMm1(
            groupId, userId, startMs, durationSeconds, mutedBy, reason);
    }

    bool GroupMuteManager::IsMuted(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        uint64_t                    nowMs,
        int64_t&                    outRemainingSeconds) const
    {
        outRemainingSeconds = 0;
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE) {
            return false;
        }

        bool                 exists = false;
        int64_t              stMs   = 0;
        int64_t              durS   = 0;
        std::vector<uint8_t> mb, rs;
        if (!mm2::MM2::Instance().GetGroupMuteRowForMm1(groupId, userId, exists, stMs, durS, mb, rs)) {
            return false;
        }
        if (!exists) {
            return false;
        }

        const int64_t nowI = static_cast<int64_t>(nowMs > static_cast<uint64_t>(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(nowMs));
        if (nowI < stMs) {
            return false;
        }

        if (durS == -1) {
            outRemainingSeconds = -1;
            return true;
        }
        if (durS < 0) {
            return false; // 非法持久化值；不按禁言处理
        }
        const int64_t addMs = durS * 1000;
        const int64_t endMs = stMs + addMs;
        if (nowI >= endMs) {
            return false;
        }
        const int64_t remMs = endMs - nowI;
        outRemainingSeconds = (remMs + 999) / 1000;
        if (outRemainingSeconds < 1) {
            outRemainingSeconds = 1;
        }
        return true;
    }

    bool GroupMuteManager::UnmuteMember(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& unmutedBy)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE || unmutedBy.size() != USER_ID_SIZE) {
            return false;
        }

        int32_t actorRole = 0;
        int64_t jActor    = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, unmutedBy, actorRole, jActor)) {
            return false;
        }
        if (!IsOwnerOrAdminRole(actorRole)) {
            return false;
        }

        bool inGroup = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, unmutedBy, inGroup) || !inGroup) {
            return false;
        }

        bool exists = false;
        int64_t st0 = 0, d0 = 0;
        std::vector<uint8_t> mb0, r0;
        if (!mm2::MM2::Instance().GetGroupMuteRowForMm1(groupId, userId, exists, st0, d0, mb0, r0)) {
            return false;
        }
        if (!exists) {
            return false;
        }

        return mm2::MM2::Instance().DeleteGroupMuteForMm1(groupId, userId);
    }

    void GroupMuteManager::CleanupExpiredMutes(uint64_t nowMs)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        const int64_t nowI =
            static_cast<int64_t>(nowMs > static_cast<uint64_t>(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(nowMs));
        (void)mm2::MM2::Instance().DeleteExpiredGroupMutesForMm1(nowI);
    }

} // namespace ZChatIM::mm1
