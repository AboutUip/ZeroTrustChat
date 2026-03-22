#pragma once

// 语音/视频通话：**进程内信令占位**（**无** WebRTC / 媒体面）。
// 供 App 绑定 UI 与会话；真实音视频须 **Java/Kotlin WebRTC** 等 + 服务端信令。

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {
        class RtcCallManager {
        public:
            RtcCallManager();
            ~RtcCallManager();

            RtcCallManager(RtcCallManager&&) noexcept            = delete;
            RtcCallManager& operator=(RtcCallManager&&) noexcept = delete;
            RtcCallManager(const RtcCallManager&)                = delete;
            RtcCallManager& operator=(const RtcCallManager&)     = delete;

            // **`callType`**：1=语音，2=视频；返回 **16B `callId`**，失败空向量。
            std::vector<uint8_t> StartCall(
                const std::vector<uint8_t>& ownerUserId,
                const std::vector<uint8_t>& imSessionId,
                const std::vector<uint8_t>& peerUserId,
                int32_t                     callType);

            // 仅 **`ownerUserId`** 与登记一致时可结束。
            bool EndCall(const std::vector<uint8_t>& ownerUserId, const std::vector<uint8_t>& callId);

            // 0=无此呼叫；1=进行中（已 **`StartCall`** 且未 **`EndCall`**）。
            int32_t GetCallState(const std::vector<uint8_t>& ownerUserId, const std::vector<uint8_t>& callId) const;

            void ClearAll();

        private:
            struct Entry {
                std::vector<uint8_t> ownerUserId;
                std::vector<uint8_t> imSessionId;
                std::vector<uint8_t> peerUserId;
                int32_t              callType = 0;
            };

            mutable std::mutex                        mutex_;
            std::map<std::vector<uint8_t>, Entry>     activeByCallId_;
        };
    } // namespace mm1
} // namespace ZChatIM
