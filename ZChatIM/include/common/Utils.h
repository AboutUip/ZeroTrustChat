#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ZChatIM
{
	namespace common
	{
		class Utils {
		public:
			static std::string BytesToHex(const uint8_t* data, size_t length);

			static bool HexToBytes(const std::string& hex, uint8_t* output, size_t outputLen);

			static std::vector<uint8_t> StringToBytes(const std::string& str);

			static std::string BytesToString(const uint8_t* data, size_t length);

			static uint32_t BigEndianToLittleEndian(uint32_t value);

			static uint32_t LittleEndianToBigEndian(uint32_t value);

			static uint64_t BigEndianToLittleEndian64(uint64_t value);

			static uint64_t LittleEndianToBigEndian64(uint64_t value);

			static uint32_t CalculateCRC32(const uint8_t* data, size_t length);

			static uint32_t CalculateAdler32(const uint8_t* data, size_t length);

		private:
			Utils() = delete;
			~Utils() = delete;
		};

	} // namespace common
} // namespace ZChatIM
