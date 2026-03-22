#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace ZChatIM
{
	namespace common
	{
		// Thread-safe. mt19937 for non-secure APIs; RAND_bytes for secure (no mt fallback).
		class Random {
		public:
			static std::vector<uint8_t> GenerateBytes(size_t length);

			static int32_t GenerateInt(int32_t min, int32_t max);

			static uint32_t GenerateUInt(uint32_t min, uint32_t max);

			static bool GenerateBool();

			static std::vector<uint8_t> GenerateSecureBytes(size_t length);

			static int32_t GenerateSecureInt(int32_t min, int32_t max);

			static uint32_t GenerateSecureUInt(uint32_t min, uint32_t max);

			static std::vector<uint8_t> GenerateMessageId();

			static std::vector<uint8_t> GenerateSessionId();

			static std::string GenerateFileId();

			static std::string GenerateRandomString(size_t length);

		private:
			Random() = delete;
			~Random() = delete;

			static void Init();
		};

	} // namespace common
} // namespace ZChatIM
