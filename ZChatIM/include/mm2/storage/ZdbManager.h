#pragma once

#include "Types.h"
#include "ZdbFile.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>

namespace ZChatIM
{
    namespace mm2
    {
        // m_mutex serializes API; fileId basename only (see IsSafeZdbFileId in .cpp).
        class ZdbManager {
        public:
            ZdbManager();
            ~ZdbManager();

            bool Initialize(const std::string& dataDir);

            void Cleanup();

            bool CreateFile(std::string& outFileId);

            bool OpenFile(const std::string& fileId);

            bool CloseFile(const std::string& fileId);

            bool DeleteFile(const std::string& fileId);

            bool WriteData(const std::string& dataId, const uint8_t* data, size_t length, std::string& outFileId, uint64_t& outOffset);

            bool ReadData(const std::string& fileId, uint64_t offset, uint8_t* buffer, size_t length);

            bool DeleteData(const std::string& fileId, uint64_t offset, size_t length);

            bool AllocateSpace(size_t size, std::string& outFileId, uint64_t& outOffset);

            bool FreeSpace(const std::string& fileId, uint64_t offset, size_t size);

            size_t GetTotalAvailableSpace() const;

            size_t GetTotalUsedSpace() const;

            size_t GetTotalSpace() const;

            std::vector<std::string> GetFileList() const;

            bool GetFileStatus(const std::string& fileId, size_t& usedSpace, size_t& availableSpace) const;

            bool NeedCreateNewFile() const;

            std::string LastError() const { return m_lastError; }

        private:
            bool SelectFile(size_t size, std::string& outFileId);

            bool ScanExistingFiles();

            std::string GenerateFileName();

            std::string BuildFilePath(const std::string& fileId);

            bool CheckDirectory();

            bool CleanupExpiredFiles();

            bool CreateNewFileUnlocked(std::string& outFileId);

            void CleanupUnlocked();

            std::string m_dataDir;
            std::string m_lastError;
            mutable std::mutex m_mutex;
            std::map<std::string, std::shared_ptr<ZdbFile>> m_files;
            std::vector<std::string> m_fileList;
        };

    } // namespace mm2
} // namespace ZChatIM
