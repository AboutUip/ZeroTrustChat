#pragma once

#include "../Types.h"
#include <string>
#include <vector>

namespace ZChatIM
{
	namespace jni
	{
		class JniUtils {
		public:
			static std::string ByteArrayToString(const std::vector<uint8_t>& bytes);

			static std::vector<uint8_t> StringToByteArray(const std::string& str);

			static std::vector<uint8_t> IntToByteArray(int value);
			static std::vector<uint8_t> LongToByteArray(int64_t value);

			static int ByteArrayToInt(const std::vector<uint8_t>& bytes);
			static int64_t ByteArrayToLong(const std::vector<uint8_t>& bytes);

			static std::vector<uint8_t> CombineByteArrays(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

			static std::vector<std::vector<uint8_t>> SplitByteArray(const std::vector<uint8_t>& bytes, size_t chunkSize);

			static std::vector<uint8_t> CopyByteArray(const std::vector<uint8_t>& bytes);

			static std::string GenerateRandomString(size_t length);

			static std::string GenerateFileId();

			static std::vector<uint8_t> GenerateMessageId();

			static int64_t GetCurrentTimestamp();

			static std::string TimestampToString(int64_t timestamp);

			static std::string FormatErrorMessage(const std::string& message, int errorCode);

			static void LogError(const std::string& message);

			static void LogWarning(const std::string& message);

			static void LogInfo(const std::string& message);

			static bool SecureCompare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

			static void SecureClear(std::vector<uint8_t>& bytes);

			static bool ValidateDataLength(const std::vector<uint8_t>& data, size_t minLength, size_t maxLength);

			static bool ValidateStringFormat(const std::string& str, const std::string& pattern);

		private:
			JniUtils() = delete;
			~JniUtils() = delete;
		};

	} // namespace jni
} // namespace ZChatIM
