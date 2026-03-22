// AccountDeleteManager：本地 **注销墓碑**（**`mm1_user_kv` type=`0x41434431` 'ACD1'**）；**不**自动清库（避免多账户同机误伤）。**`reauthToken` 与 `secondConfirmToken` 须相同且 ≥16B**（双确认载荷由产品生成）。
#include "mm1/managers/AccountDeleteManager.h"

#include "common/Memory.h"
#include "mm1/MM1.h"
#include "Types.h"

#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr int32_t kMm1KvAccountDeletedMarker = 0x41434431; // 'ACD1'
        constexpr size_t  kMinConfirmBytes           = 16;

    } // namespace

    bool AccountDeleteManager::DeleteAccount(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& reauthToken,
        const std::vector<uint8_t>& secondConfirmToken)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (userId.size() != USER_ID_SIZE) {
            return false;
        }
        if (reauthToken.size() < kMinConfirmBytes || secondConfirmToken.size() < kMinConfirmBytes) {
            return false;
        }
        if (reauthToken.size() != secondConfirmToken.size()) {
            return false;
        }
        if (!common::Memory::ConstantTimeCompare(
                reauthToken.data(),
                secondConfirmToken.data(),
                reauthToken.size())) {
            return false;
        }

        static const std::vector<uint8_t> kMarker = {1};
        return MM1::Instance().GetUserDataManager().StoreUserData(userId, kMm1KvAccountDeletedMarker, kMarker);
    }

    bool AccountDeleteManager::IsAccountDeleted(const std::vector<uint8_t>& userId) const
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (userId.size() != USER_ID_SIZE) {
            return false;
        }
        const std::vector<uint8_t> d =
            MM1::Instance().GetUserDataManager().GetUserData(userId, kMm1KvAccountDeletedMarker);
        return !d.empty() && d[0] != 0;
    }

} // namespace ZChatIM::mm1
