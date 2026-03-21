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

            // inviteMember/removeMember/leaveGroup -> result
            bool InviteMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);
            bool RemoveMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);
            bool LeaveGroup(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);

            // getGroupMembers(groupId) -> array
            std::vector<std::vector<uint8_t>> GetGroupMembers(const std::vector<uint8_t>& groupId);

            // updateGroupKey(groupId) -> result
            bool UpdateGroupKey(const std::vector<uint8_t>& groupId);

            // =============================================================
            // @提及权限校验所需能力
            // =============================================================
            // IsGroupMember(groupId, userId) -> true/false
            bool IsGroupMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const;

            // IsGroupOwnerOrAdmin(groupId, userId) -> true/false
            bool IsGroupOwnerOrAdmin(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const;
        };
    } // namespace mm1
} // namespace ZChatIM

