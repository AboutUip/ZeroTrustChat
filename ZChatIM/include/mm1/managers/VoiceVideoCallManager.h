#pragma once

// 语音 / 视频通话：**进程内信令占位**（分配 **16B callId**、记录发起方与对端）。
// **不包含** RTP/WebRTC/系统电话栈；上层用 **callId** 对接 **`ConnectionService` / WebRTC` 等**。
// 实现上委托 **`RtcCallSessionManager`**，与 JNI **`Rtc*`** 共用同一状态机。

#include <cstdint>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {

        class RtcCallSessionManager;

        class VoiceVideoCallManager {
        public:
            VoiceVideoCallManager();
            ~VoiceVideoCallManager();

            VoiceVideoCallManager(const VoiceVideoCallManager&)            = delete;
            VoiceVideoCallManager& operator=(const VoiceVideoCallManager&) = delete;
            VoiceVideoCallManager(VoiceVideoCallManager&&) noexcept            = delete;
            VoiceVideoCallManager& operator=(VoiceVideoCallManager&&) noexcept = delete;

            void AttachRtcSessionManager(RtcCallSessionManager* rtc) noexcept { rtc_ = rtc; }

            void ClearAllCalls();

            // 发起方 **principal** 须为 **16B**；**peerUserId** **16B**。成功返回 **16B callId**，失败空向量。
            std::vector<uint8_t> StartVoiceCall(
                const std::vector<uint8_t>& initiatorUserId,
                const std::vector<uint8_t>& peerUserId);

            std::vector<uint8_t> StartVideoCall(
                const std::vector<uint8_t>& initiatorUserId,
                const std::vector<uint8_t>& peerUserId);

            // 仅 **发起方** 可结束；**callId** **16B**。
            bool EndCall(
                const std::vector<uint8_t>& initiatorUserId,
                const std::vector<uint8_t>& callId);

        private:
            RtcCallSessionManager* rtc_{nullptr};
        };
    } // namespace mm1
} // namespace ZChatIM
