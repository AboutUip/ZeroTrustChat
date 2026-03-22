#include "mm1/managers/UserDataManager.h"

#include "Types.h"
#include "mm2/MM2.h"

#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace ZChatIM::mm1 {

    struct UserDataManager::Impl {
        std::mutex                           mutex;
        std::map<std::string, std::vector<uint8_t>> rows;
    };

    static std::string MakeRowKey(const std::vector<uint8_t>& userId, int32_t type)
    {
        std::string k;
        k.reserve(userId.size() + sizeof(type));
        k.assign(reinterpret_cast<const char*>(userId.data()), userId.size());
        k.append(reinterpret_cast<const char*>(&type), sizeof(type));
        return k;
    }

    UserDataManager::UserDataManager()
        : impl_(std::make_unique<Impl>())
    {
    }

    UserDataManager::~UserDataManager() = default;

    UserDataManager::UserDataManager(UserDataManager&& other) noexcept            = default;
    UserDataManager& UserDataManager::operator=(UserDataManager&& other) noexcept = default;

    bool UserDataManager::StoreUserData(
        const std::vector<uint8_t>& userId,
        int32_t                     type,
        const std::vector<uint8_t>& data)
    {
        if (!impl_ || userId.size() != USER_ID_SIZE) {
            return false;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (m2.IsInitialized()) {
            return m2.StoreMm1UserDataBlob(userId, type, data);
        }
        const std::string key = MakeRowKey(userId, type);
        std::lock_guard<std::mutex> lk(impl_->mutex);
        impl_->rows[key] = data;
        return true;
    }

    std::vector<uint8_t> UserDataManager::GetUserData(const std::vector<uint8_t>& userId, int32_t type)
    {
        if (!impl_ || userId.size() != USER_ID_SIZE) {
            return {};
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (m2.IsInitialized()) {
            std::vector<uint8_t> out;
            if (!m2.GetMm1UserDataBlob(userId, type, out)) {
                return {};
            }
            return out;
        }
        const std::string key = MakeRowKey(userId, type);
        std::lock_guard<std::mutex> lk(impl_->mutex);
        const auto                  it = impl_->rows.find(key);
        if (it == impl_->rows.end()) {
            return {};
        }
        return it->second;
    }

    bool UserDataManager::DeleteUserData(const std::vector<uint8_t>& userId, int32_t type)
    {
        if (!impl_ || userId.size() != USER_ID_SIZE) {
            return false;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (m2.IsInitialized()) {
            return m2.DeleteMm1UserDataBlob(userId, type);
        }
        const std::string key = MakeRowKey(userId, type);
        std::lock_guard<std::mutex> lk(impl_->mutex);
        return impl_->rows.erase(key) > 0;
    }

} // namespace ZChatIM::mm1
