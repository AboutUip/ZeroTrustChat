#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace ZChatIM {
    namespace mm1 {
        class GroupManager {
        public:
            std::vector<uint8_t> CreateGroup(const std::vector<uint8_t>& creatorId, const std::string& name);

            bool InviteMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& inviterUserId);
            bool RemoveMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& actorUserId);
            bool LeaveGroup(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);

            // getGroupMembers：**viewerUserId** 须已是群成员。
            std::vector<std::vector<uint8_t>> GetGroupMembers(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& viewerUserId);

            bool UpdateGroupKey(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& actorUserId);

            bool IsGroupMember(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const;

            bool IsGroupOwnerOrAdmin(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId) const;
        };
    } // namespace mm1
} // namespace ZChatIM

