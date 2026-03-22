#pragma once

#include "Types.h"
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace ZChatIM
{
    namespace mm2
    {
        // Public API serialized by recursive_mutex (Create/Open may Close under lock).
        class ZdbFile {
        public:
            ZdbFile();
            ~ZdbFile();

            bool Open(const std::string& filePath);

            void Close();

            bool Create(const std::string& filePath);

            bool IsOpen() const;

            bool WriteData(uint64_t offset, const uint8_t* data, size_t length);

            bool AppendRaw(const uint8_t* data, size_t length, uint64_t& outOffset);

            bool ReadData(uint64_t offset, uint8_t* buffer, size_t length);

            bool OverwriteData(uint64_t offset, size_t length);

            bool AllocateSpace(size_t size, uint64_t& outOffset);

            bool FreeSpace(uint64_t offset, size_t size);

            size_t GetAvailableSpace() const;

            size_t GetUsedSpace() const;

            size_t GetTotalSpace() const;

            std::string GetFilePath() const;

            std::string GetFileId() const;

            bool IsFull() const;

            bool IsCorrupted() const;

            std::string LastError() const { return m_lastError; }

        private:
            bool InitHeader();

            bool ReadHeader();

            bool WriteHeader();

            bool FillRandomData(uint64_t offset, size_t length);

            bool IsValidOffset(uint64_t offset, size_t length) const;

            std::string m_filePath;
            std::fstream m_file;
            ZdbHeader m_header;
            mutable std::recursive_mutex m_mutex;
            std::string m_lastError;

            std::vector<uint64_t> m_freeSlots;
            size_t m_usedSpace;
        };

    } // namespace mm2
} // namespace ZChatIM
