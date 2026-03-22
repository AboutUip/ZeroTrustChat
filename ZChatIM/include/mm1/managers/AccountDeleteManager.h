#pragma once

#include <cstdint>
#include <vector>

namespace ZChatIM {
	namespace mm1 {
		class AccountDeleteManager {
		public:
			bool DeleteAccount(
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& reauthToken,
				const std::vector<uint8_t>& secondConfirmToken);

			bool IsAccountDeleted(const std::vector<uint8_t>& userId) const;
		};
	} // namespace mm1
} // namespace ZChatIM

