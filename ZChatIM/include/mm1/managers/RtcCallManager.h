#pragma once

// 语音/视频通话：**进程内信令占位**（**无** WebRTC / 媒体面）。
// 供 App 绑定 UI 与会话；真实音视频须 **Java/Kotlin WebRTC** 等 + 服务端信令。
// 呼叫状态与 **`RtcStartCall` JNI** 一致，委托 **`RtcCallSessionManager`**；本地保留 **imSessionId** 元数据。

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {

        class RtcCallSessionManager;

        class RtcCallManager {
        public:
            RtcCallManager();
            ~RtcCallManager();

            RtcCallManager(RtcCallManager&&) noexcept            = delete;
            RtcCallManager& operator=(RtcCallManager&&) noexcept = delete;
            RtcCallManager(const RtcCallManager&)                = delete;
            RtcCallManager& operator=(const RtcCallManager&)     = delete;

            void AttachRtcSessionManager(RtcCallSessionManager* rtc) noexcept { rtc_ = rtc; }

            // **`callType`**：1=语音，2=视频；返回 **16B `callId`**，失败空向量。
            std::vector<uint8_t> StartCall(
                const std::vector<uint8_t>& ownerUserId,
                const std::vector<uint8_t>& imSessionId,
                const std::vector<uint8_t>& peerUserId,
                int32_t                     callType);

            // 仅 **`ownerUserId`** 与登记一致时可结束。
            bool EndCall(const std::vector<uint8_t>& ownerUserId, const std::vector<uint8_t>& callId);

            // **简化状态 API**（**与 JNI `RtcGetCallState` 使用的 `RtcCallSessionManager` 完整枚举不同**）：
            // **0**：无此腿 / 非 owner / **`RtcCallSessionManager`** 侧 **`INVALID`/`ENDED`/`REJECTED`**；
            // **1**：**`RINGING`** 或 **`CONNECTED`**（未终态且本 **`RtcCallManager`** 表内仍有登记）。
            int32_t GetCallState(const std::vector<uint8_t>& ownerUserId, const std::vector<uint8_t>& callId);

            void ClearAll();

        private:
            struct Entry {
                std::vector<uint8_t> ownerUserId;
                std::vector<uint8_t> imSessionId;
                std::vector<uint8_t> peerUserId;
                int32_t              callType = 0;
            };

            RtcCallSessionManager*                rtc_{nullptr};
            mutable std::mutex                    mutex_;
            std::map<std::vector<uint8_t>, Entry> activeByCallId_;
        };
    } // namespace mm1
} // namespace ZChatIM
