#pragma once

#include "../Types.h"
#include <cstdint>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            class SideChannel {
            public:
                SideChannel();
                ~SideChannel();

                bool ConstantTimeCompare(const uint8_t* a, const uint8_t* b, size_t size);

                bool ConstantTimeCompare(uint64_t a, uint64_t b);

                bool ConstantTimeCompare(const char* a, const char* b, size_t size);

                void SecureZero(void* ptr, size_t size);

                void SecureCopy(void* dest, const void* src, size_t size);

                void SecureFill(void* ptr, uint8_t value, size_t size);

                void AntiTimingDelay(size_t operations);

                void RandomDelay();

                void FlushCache();

                void PreventCacheSideChannel();

                bool IsSideChannelProtectionEnabled();

                void EnableSideChannelProtection();

                void DisableSideChannelProtection();

            private:
                SideChannel(const SideChannel&) = delete;
                SideChannel& operator=(const SideChannel&) = delete;

                bool m_protectionEnabled;
            };

        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
