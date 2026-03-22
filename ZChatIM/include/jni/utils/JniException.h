#pragma once

#include "../Types.h"
#include <string>
#include <stdexcept>

namespace ZChatIM
{
	namespace jni
	{
		class JniException : public std::runtime_error {
		public:
			enum class ErrorCode {
				SUCCESS = 0,
				JNI_ENV_ERROR = 1,
				JNI_CLASS_ERROR = 2,
				JNI_METHOD_ERROR = 3,
				JNI_MEMORY_ERROR = 4,
				JNI_TYPE_ERROR = 5,
				SECURITY_ERROR = 6,
				STORAGE_ERROR = 7,
				NETWORK_ERROR = 8,
				INVALID_PARAMETER = 9,
				UNKNOWN_ERROR = 10
			};

			JniException(ErrorCode code, const std::string& message);

			ErrorCode GetErrorCode() const;

			std::string GetErrorMessage() const;

			static std::string ErrorCodeToString(ErrorCode code);

		private:
			ErrorCode m_errorCode;
			std::string m_errorMessage;
		};

		class ExceptionUtils {
		public:
			static void ThrowException(JniException::ErrorCode code, const std::string& message);

			static std::string CatchException();

			static void CheckParameter(bool condition, const std::string& message);

			static void CheckMemory(void* ptr, const std::string& message);

			static void CheckResult(bool result, JniException::ErrorCode code, const std::string& message);

		private:
			ExceptionUtils() = delete;
			~ExceptionUtils() = delete;
		};

	} // namespace jni
} // namespace ZChatIM
