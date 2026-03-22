#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <system_error>

namespace ZChatIM
{
	namespace common
	{
		class File {
		public:
			static bool Exists(const std::string& filePath);

			static bool IsFile(const std::string& path);

			static bool IsDirectory(const std::string& path);

			static uint64_t GetSize(const std::string& filePath);

			static uint64_t GetLastModified(const std::string& filePath);

			static bool Delete(const std::string& filePath);

			static bool Rename(const std::string& oldPath, const std::string& newPath);

			static bool Copy(const std::string& srcPath, const std::string& destPath);

			static bool CreateDirectory(const std::string& path);

			static bool CreateDirectoryRecursive(const std::string& path);

			static bool DeleteDirectory(const std::string& path);

			static bool DeleteDirectoryRecursive(const std::string& path);

			static bool ListDirectory(const std::string& path, std::vector<std::string>& out, std::error_code& ec);

			static std::string GetFileName(const std::string& path);

			static std::string GetFileNameWithoutExtension(const std::string& path);

			static std::string GetFileExtension(const std::string& path);

			static std::string GetDirectoryPath(const std::string& path);

			static std::string JoinPath(const std::string& path1, const std::string& path2);

			static std::string GetAbsolutePath(const std::string& path);

			static bool ReadFile(const std::string& filePath, std::vector<uint8_t>& buffer);

			static bool WriteFile(const std::string& filePath, const uint8_t* data, size_t length);

			static bool AppendFile(const std::string& filePath, const uint8_t* data, size_t length);

		private:
			File() = delete;
			~File() = delete;
		};

	} // namespace common
} // namespace ZChatIM
