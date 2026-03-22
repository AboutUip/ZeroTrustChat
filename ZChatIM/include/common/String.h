#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ZChatIM
{
	namespace common
	{
		class String {
		public:
			static std::vector<std::string> Split(const std::string& str, char delimiter);

			static std::vector<std::string> Split(const std::string& str, const std::string& delimiters);

			static std::string Join(const std::vector<std::string>& strings, const std::string& separator);

			static std::string Replace(const std::string& str, const std::string& oldStr, const std::string& newStr);

			static size_t Find(const std::string& str, const std::string& substr);

			static size_t Find(const std::string& str, const std::string& substr, size_t pos);

			static std::string ToLower(const std::string& str);

			static std::string ToUpper(const std::string& str);

			static std::string Trim(const std::string& str);

			static std::string TrimLeft(const std::string& str);

			static std::string TrimRight(const std::string& str);

			static bool IsEmpty(const std::string& str);

			static bool IsBlank(const std::string& str);

			static bool StartsWith(const std::string& str, const std::string& prefix);

			static bool EndsWith(const std::string& str, const std::string& suffix);

			static bool Contains(const std::string& str, const std::string& substr);

			static int32_t ToInt32(const std::string& str);

			static uint32_t ToUInt32(const std::string& str);

			static int64_t ToInt64(const std::string& str);

			static uint64_t ToUInt64(const std::string& str);

			static double ToDouble(const std::string& str);

			static std::string FromInt32(int32_t value);

			static std::string FromUInt32(uint32_t value);

			static std::string FromInt64(int64_t value);

			static std::string FromUInt64(uint64_t value);

			static std::string FromDouble(double value);

			static std::string Utf8ToGbk(const std::string& utf8);

			static std::string GbkToUtf8(const std::string& gbk);

			static bool SecureCompare(const std::string& str1, const std::string& str2);

			static uint64_t GenerateHashCode(const std::string& str);

		private:
			String() = delete;
			~String() = delete;
		};

	} // namespace common
} // namespace ZChatIM
