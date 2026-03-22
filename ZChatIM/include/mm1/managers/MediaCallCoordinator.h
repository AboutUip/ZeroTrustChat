#pragma once

// 音视频会议：**进程内**登记当前通话腿（callId / 对端 / 媒体类型 / 开始时间）。
// **不包含** RTP/SRTP/WebRTC；上层用 **callId** 与信令/ **`android.media.*`** / **WebRTC** 对齐。

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {
        class MediaCallCoordinator {
        public:
            MediaCallCoordinator();
            ~MediaCallCoordinator();

            MediaCallCoordinator(MediaCallCoordinator&&) noexcept            = delete;
            MediaCallCoordinator& operator=(MediaCallCoordinator&&) noexcept = delete;
            MediaCallCoordinator(const MediaCallCoordinator&)                = delete;
            MediaCallCoordinator& operator=(const MediaCallCoordinator&)     = delete;

            // 新建一通；**mediaKind**：**`MEDIA_CALL_KIND_AUDIO`** / **`MEDIA_CALL_KIND_VIDEO`**（见 **`Types.h`**）。
            // 失败返回空向量（参数非法、单 owner 并发腿数超限、随机数失败）。
            std::vector<uint8_t> StartCall(
                const std::vector<uint8_t>& ownerUserId16,
                const std::vector<uint8_t>& remoteUserId16,
                int32_t                     mediaKind);

            // 结束指定 **callId**（16B）；非本人或无此 id → false。
            bool EndCall(const std::vector<uint8_t>& ownerUserId16, const std::vector<uint8_t>& callId16);

            // 每行 **44B**：**callId(16) ‖ remoteUserId(16) ‖ mediaKind(i32 BE) ‖ startedMs(u64 BE)**。
            std::vector<std::vector<uint8_t>> ListCalls(const std::vector<uint8_t>& ownerUserId16);

            void ClearAll();

        private:
            struct Entry {
                std::vector<uint8_t> ownerUserId;
                std::vector<uint8_t> remoteUserId;
                int32_t              mediaKind   = 0;
                uint64_t             startedMs = 0;
            };

            std::mutex                       mutex_;
            std::map<std::vector<uint8_t>, Entry> byCallId_;
        };
    } // namespace mm1
} // namespace ZChatIM
