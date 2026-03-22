// MessageQueryManager：委托 MM2（**RAM IM** 列表 + `RetrieveMessage`），编码见 `MessageQueryManager.h`。

#include "mm2/storage/MessageQueryManager.h"
#include "mm2/MM2.h"

namespace ZChatIM::mm2 {

    std::vector<std::vector<uint8_t>> MessageQueryManager::ListMessages(const std::vector<uint8_t>& userId, int count)
    {
        if (owner_ == nullptr) {
            return {};
        }
        return owner_->InternalListMessagesForQueryManager(userId, count);
    }

    std::vector<std::vector<uint8_t>> MessageQueryManager::ListMessagesSinceTimestamp(
        const std::vector<uint8_t>& userId,
        uint64_t                    sinceTimestampMs,
        int                         count)
    {
        if (owner_ == nullptr) {
            return {};
        }
        return owner_->InternalListMessagesSinceTimestampForQueryManager(userId, sinceTimestampMs, count);
    }

    std::vector<std::vector<uint8_t>> MessageQueryManager::ListMessagesSinceMessageId(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& lastMsgId,
        int                         count)
    {
        if (owner_ == nullptr) {
            return {};
        }
        return owner_->InternalListMessagesSinceMessageIdForQueryManager(userId, lastMsgId, count);
    }

} // namespace ZChatIM::mm2
