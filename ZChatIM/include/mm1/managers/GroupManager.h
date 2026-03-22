#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 群组密钥 / 成员管理器契约
        // =============================================================
        class GroupManager {
        public:
            // createGroup(creatorId, name) -> groupId/null
            std::vector<uint8_t> CreateGroup(const std::vector<uint8_t>& creatorId, const std::string& name);

            // inviteMember：**inviterUserId** 须为 **owner/admin**；**`ListAcceptedFriendPeerUserIds(inviterUserId)`** 须含 **userId**（**不可**自邀）。JNI 传 **principal** 为邀请者。
            bool InviteMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& inviterUserId);
            // removeMember：**actorUserId** 须为 **owner**；不可移除自己（用 **LeaveGroup**）。
            bool RemoveMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& actorUserId);
            bool LeaveGroup(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);

            // getGroupMembers：**viewerUserId** 须已是群成员。
            std::vector<std::vector<uint8_t>> GetGroupMembers(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& viewerUserId);

            // updateGroupKey：**actorUserId** 须为 **owner/admin**；写入 **`ZGK1`** 信封至 **`.zdb` + `group_data`**（见 **`MM2::UpsertGroupKeyEnvelopeForMm1`** / **`03-Storage.md`**）。
            bool UpdateGroupKey(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& actorUserId);

            // =============================================================
            // @提及权限校验所需能力
            // =============================================================
            // IsGroupMember：以 **`group_members` 是否存在该行** 为准（与 **LeaveGroup / GetGroupMembers** 一致）；持 **MM1 `m_apiRecursiveMutex`**。
            bool IsGroupMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const;

            // IsGroupOwnerOrAdmin：**role 须为 1 或 2**（非法/篡改 role 由 SQLite 读路径拒绝）；持 **MM1 `m_apiRecursiveMutex`**。
            bool IsGroupOwnerOrAdmin(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const;
        };
    } // namespace mm1
} // namespace ZChatIM

