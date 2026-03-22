// GroupNameManager：群显示名 **`mm2_group_display`**（经 **MM2::UpdateGroupName / GetGroupName**）。
#include "mm1/managers/GroupNameManager.h"

#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <mutex>
#include <string>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr int32_t kRoleAdmin = 1;
        constexpr int32_t kRoleOwner = 2;
        // 与 **`GroupManager::CreateGroup`** / **`SqliteMetadataDb::UpsertGroupDisplayName`** 一致。
        constexpr size_t kMaxGroupDisplayNameUtf8Bytes = 2048;

        bool IsOwnerOrAdminRole(int32_t r)
        {
            return r == kRoleAdmin || r == kRoleOwner;
        }

    } // namespace

    bool GroupNameManager::UpdateGroupName(
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& updaterId,
        const std::string&          newGroupName,
        uint64_t                    nowMs)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (groupId.size() != USER_ID_SIZE || updaterId.size() != USER_ID_SIZE) {
            return false;
        }
        if (newGroupName.empty() || newGroupName.size() > kMaxGroupDisplayNameUtf8Bytes) {
            return false;
        }
        int32_t role   = 0;
        int64_t joined = 0;
        if (!mm2::MM2::Instance().GetGroupMemberRoleForMm1(groupId, updaterId, role, joined)) {
            return false;
        }
        if (!IsOwnerOrAdminRole(role)) {
            return false;
        }
        // JNI **`nowMs`**；元库 **`mm2_group_display.updated_s`** 为**秒**（与 **`CreateGroupSeedForMm1`** 一致）。
        const uint64_t updateSeconds = nowMs / 1000ULL;
        return mm2::MM2::Instance().UpdateGroupName(groupId, newGroupName, updateSeconds, updaterId);
    }

} // namespace ZChatIM::mm1
