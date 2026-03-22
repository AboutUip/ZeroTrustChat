#pragma once

#include <string>
#include <cstdint>

namespace ZChatIM
{
	namespace common
	{
		class Time {
		public:
			static uint64_t GetCurrentTimestamp();

			static uint64_t GetCurrentTimestampSeconds();

			static std::string TimestampToString(uint64_t timestamp);

			static uint64_t StringToTimestamp(const std::string& timeStr);

			static int64_t CalculateDuration(uint64_t start, uint64_t end);

			static int64_t CalculateDurationSeconds(uint64_t start, uint64_t end);

			static bool IsExpired(uint64_t timestamp, uint64_t durationMs);

			static bool IsExpiredSeconds(uint64_t timestamp, uint64_t durationSeconds);

			static bool IsExpiredDays(uint64_t timestamp, uint64_t days);

			static std::string GetCurrentTimeString();

			static std::string GetCurrentDateString();

			static std::string GetCurrentDateTimeString();

		private:
			Time() = delete;
			~Time() = delete;
		};

	} // namespace common
} // namespace ZChatIM
