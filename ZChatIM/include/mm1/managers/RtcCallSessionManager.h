#pragma once

// 音视频会议：**进程内** 呼叫状态机（**无 RTP/WebRTC 媒体面**）。供 App 与 Java **`WebRTC` / `CameraX`** 等对齐 **callId** 与状态。
// 持久化、信令下发由**上层**负责。

#include <cstdint>
#include <memory>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {

        constexpr int32_t RTC_CALL_KIND_AUDIO = 0;
        constexpr int32_t RTC_CALL_KIND_VIDEO = 1;

        constexpr int32_t RTC_CALL_STATE_INVALID   = 0;
        constexpr int32_t RTC_CALL_STATE_RINGING = 1;
        constexpr int32_t RTC_CALL_STATE_CONNECTED = 2;
        constexpr int32_t RTC_CALL_STATE_ENDED   = 3;
        constexpr int32_t RTC_CALL_STATE_REJECTED = 4;

        class RtcCallSessionManager {
        public:
            RtcCallSessionManager();
            ~RtcCallSessionManager();

            RtcCallSessionManager(RtcCallSessionManager&&) noexcept            = delete;
            RtcCallSessionManager& operator=(RtcCallSessionManager&&) noexcept = delete;
            RtcCallSessionManager(const RtcCallSessionManager&)                = delete;
            RtcCallSessionManager& operator=(const RtcCallSessionManager&)     = delete;

            void ClearAll();

            std::vector<uint8_t> StartCall(
                const std::vector<uint8_t>& initiatorUserId,
                const std::vector<uint8_t>& peerUserId,
                int32_t                     callKind);

            bool AcceptCall(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId);
            bool RejectCall(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId);
            bool EndCall(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId);

            bool IsInitiator(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId);

            int32_t GetCallState(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId);
            int32_t GetCallKind(const std::vector<uint8_t>& principalUserId, const std::vector<uint8_t>& callId);

            // 每行 **40B**：**callId(16) ‖ counterpartyUserId(16) ‖ kind(BE32) ‖ state(BE32)**（与 **`RtcGetCallState`** 常量一致）。
            std::vector<std::vector<uint8_t>> ListCallsForUser(const std::vector<uint8_t>& principalUserId);
        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
        };
    } // namespace mm1
} // namespace ZChatIM
