#pragma once

#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // Stubs: MM1_manager_stubs.cpp; JniSecurityPolicy item 8.
        class SystemControl {
        public:
            // MM1 wipe + JniBridge::NotifyExternalTrustedZoneWipeHandled (pure MM1 wipe does not touch bridge).
            void EmergencyWipe();

            std::map<std::string, std::string> GetStatus();

            bool RotateKeys();
        };
    } // namespace mm1
} // namespace ZChatIM

