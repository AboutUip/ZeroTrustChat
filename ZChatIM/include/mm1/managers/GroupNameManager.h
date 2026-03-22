#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace ZChatIM {
    namespace mm1 {
        class GroupNameManager {
        public:
            // Owner/admin; name UTF-8 ≤2048; MM2::UpdateGroupName (mm2_group_display).
            bool UpdateGroupName(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& updaterId,
                const std::string& newGroupName,
                uint64_t nowMs);
        };
    } // namespace mm1
} // namespace ZChatIM

