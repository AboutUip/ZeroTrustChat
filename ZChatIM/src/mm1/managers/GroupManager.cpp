// GroupManager：群成员与群密钥信封 **`ZGK1`** 经 MM2 → SQLite + `.zdb`（**`group_members`**、**`group_data`**）。
// 首包群显示名随 **`CreateGroupSeedForMm1`** 写入 **`mm2_group_display`**；后续改名见 **`GroupNameManager`**。
#include "mm1/managers/GroupManager.h"

#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "mm2/storage/Crypto.h"
#include "Types.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr int32_t kRoleMember = 0;
        constexpr int32_t kRoleAdmin  = 1;
        constexpr int32_t kRoleOwner  = 2;

        // 群显示名 UTF-8 上限（与 JNI/产品层一致前先做 DoS 边界）。
        constexpr size_t kMaxGroupDisplayNameUtf8Bytes = 2048;

        bool IsOwnerOrAdminRole(int32_t r)
        {
            return r == kRoleAdmin || r == kRoleOwner;
        }

        int64_t NowUnixSeconds()
        {
            return static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        uint64_t NowUnixSecondsU64()
        {
            return static_cast<uint64_t>(NowUnixSeconds());
        }

        bool UserIdsEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
        {
            if (a.size() != USER_ID_SIZE || b.size() != USER_ID_SIZE) {
                return false;
            }
            return common::Memory::ConstantTimeCompare(a.data(), b.data(), USER_ID_SIZE);
        }

    } // namespace

    std::vector<uint8_t> GroupManager::CreateGroup(const std::vector<uint8_t>& creatorId, const std::string& name)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (creatorId.size() != USER_ID_SIZE || name.empty() || name.size() > kMaxGroupDisplayNameUtf8Bytes) {
            return {};
        }
        std::vector<uint8_t> gid = mm2::Crypto::GenerateSecureRandom(MESSAGE_ID_SIZE);
        if (gid.size() != MESSAGE_ID_SIZE) {
            return {};
        }
        const uint64_t now = NowUnixSecondsU64();
        if (!mm2::MM2::Instance().CreateGroupSeedForMm1(gid, creatorId, name, now)) {
            return {};
        }
        return gid;
    }

    bool GroupManager::InviteMember(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& inviterUserId)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE || inviterUserId.size() != USER_ID_SIZE) {
            return false;
        }
        int32_t inviterRole = 0;
        int64_t jInv        = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, inviterUserId, inviterRole, jInv)) {
            return false;
        }
        if (!IsOwnerOrAdminRole(inviterRole)) {
            return false;
        }
        if (UserIdsEqual(inviterUserId, userId)) {
            return false;
        }
        std::vector<std::vector<uint8_t>> peers;
        if (!mm2::MM2::Instance().ListAcceptedFriendUserIdsForMm1(inviterUserId, peers)) {
            return false;
        }
        bool friendEdge = false;
        for (const auto& p : peers) {
            if (p.size() == USER_ID_SIZE && UserIdsEqual(p, userId)) {
                friendEdge = true;
                break;
            }
        }
        if (!friendEdge) {
            return false;
        }
        bool inviteeExists = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, userId, inviteeExists)) {
            return false;
        }
        if (inviteeExists) {
            return false;
        }
        return mm2::MM2::Instance().UpsertGroupMemberForMm1(groupId, userId, kRoleMember, NowUnixSeconds());
    }

    bool GroupManager::RemoveMember(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& actorUserId)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE || actorUserId.size() != USER_ID_SIZE) {
            return false;
        }
        if (UserIdsEqual(userId, actorUserId)) {
            return false;
        }
        int32_t actorRole = 0;
        int64_t jActor    = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, actorUserId, actorRole, jActor)) {
            return false;
        }
        if (actorRole != kRoleOwner) {
            return false;
        }
        return mm2::MM2::Instance().DeleteGroupMemberForMm1(groupId, userId);
    }

    bool GroupManager::LeaveGroup(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE) {
            return false;
        }
        // 仅用「是否有行」判断成员身份，避免 **role** 列损坏时用户无法退群。
        bool isMember = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, userId, isMember)) {
            return false;
        }
        if (!isMember) {
            return false;
        }
        return mm2::MM2::Instance().DeleteGroupMemberForMm1(groupId, userId);
    }

    std::vector<std::vector<uint8_t>> GroupManager::GetGroupMembers(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& viewerUserId)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        std::vector<std::vector<uint8_t>> out;
        if (groupId.size() != USER_ID_SIZE || viewerUserId.size() != USER_ID_SIZE) {
            return out;
        }
        bool viewerMember = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, viewerUserId, viewerMember)) {
            return out;
        }
        if (!viewerMember) {
            return out;
        }
        if (!mm2::MM2::Instance().ListGroupMemberUserIdsForMm1(groupId, out)) {
            out.clear();
        }
        return out;
    }

    bool GroupManager::UpdateGroupKey(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& actorUserId)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || actorUserId.size() != USER_ID_SIZE) {
            return false;
        }
        int32_t ar = 0;
        int64_t ja = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, actorUserId, ar, ja)) {
            return false;
        }
        if (!IsOwnerOrAdminRole(ar)) {
            return false;
        }
        return mm2::MM2::Instance().UpsertGroupKeyEnvelopeForMm1(groupId, NowUnixSecondsU64());
    }

    bool GroupManager::IsGroupMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE) {
            return false;
        }
        bool exists = false;
        if (!mm2::MM2::Instance().GetGroupMemberExistsForMm1(groupId, userId, exists)) {
            return false;
        }
        return exists;
    }

    bool GroupManager::IsGroupOwnerOrAdmin(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE) {
            return false;
        }
        int32_t r = 0;
        int64_t j = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, userId, r, j)) {
            return false;
        }
        return IsOwnerOrAdminRole(r);
    }

} // namespace ZChatIM::mm1
