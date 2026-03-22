// .zdb container v1: 64-byte ZdbHeader + raw payload (see docs/02-Core/04-ZdbBinaryLayout.md).

#include "mm2/storage/ZdbFile.h"
#include "mm2/storage/Crypto.h"
#include "Types.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace ZChatIM::mm2 {

    namespace {

        constexpr uint64_t kHeaderBytes = sizeof(ZdbHeader);

        static_assert(kHeaderBytes == 64U, "ZdbHeader must be 64 bytes for v1 layout");
        static_assert(ZChatIM::ZDB_FILE_SIZE >= kHeaderBytes, "ZDB_FILE_SIZE must cover header");

    } // namespace

    ZdbFile::ZdbFile()
        : m_usedSpace(0)
    {
        std::memset(&m_header, 0, sizeof(m_header));
    }

    ZdbFile::~ZdbFile()
    {
        Close();
    }

    bool ZdbFile::InitHeader()
    {
        std::memset(&m_header, 0, sizeof(m_header));
        std::memcpy(m_header.magic, ZChatIM::MAGIC_ZDB, sizeof(m_header.magic));
        m_header.version   = 1;
        m_header.totalSize = static_cast<std::uint64_t>(ZChatIM::ZDB_FILE_SIZE);
        m_header.usedSize  = kHeaderBytes;
        std::memset(m_header.checksum, 0, sizeof(m_header.checksum));
        m_usedSpace = static_cast<size_t>(m_header.usedSize);
        return true;
    }

    bool ZdbFile::ReadHeader()
    {
        m_file.seekg(0, std::ios::beg);
        m_file.read(reinterpret_cast<char*>(&m_header), static_cast<std::streamsize>(sizeof(m_header)));
        if (!m_file || static_cast<size_t>(m_file.gcount()) != sizeof(m_header)) {
            m_lastError = "read ZdbHeader failed";
            return false;
        }
        if (std::memcmp(m_header.magic, ZChatIM::MAGIC_ZDB, sizeof(m_header.magic)) != 0) {
            m_lastError = "invalid ZDB magic";
            return false;
        }
        if (m_header.version != 1) {
            m_lastError = "unsupported ZDB version (v1 expected)";
            return false;
        }
        if (m_header.totalSize != static_cast<std::uint64_t>(ZChatIM::ZDB_FILE_SIZE)) {
            m_lastError = "ZDB totalSize mismatch (expected ZDB_FILE_SIZE)";
            return false;
        }
        if (m_header.usedSize < kHeaderBytes || m_header.usedSize > m_header.totalSize) {
            m_lastError = "invalid ZDB usedSize";
            return false;
        }
        m_usedSpace = static_cast<size_t>(m_header.usedSize);
        return true;
    }

    bool ZdbFile::WriteHeader()
    {
        m_file.seekp(0, std::ios::beg);
        m_file.write(reinterpret_cast<const char*>(&m_header), static_cast<std::streamsize>(sizeof(m_header)));
        m_file.flush();
        if (!m_file) {
            m_lastError = "write ZdbHeader failed";
            return false;
        }
        m_usedSpace = static_cast<size_t>(m_header.usedSize);
        return true;
    }

    bool ZdbFile::FillRandomData(uint64_t /*offset*/, size_t /*length*/)
    {
        // v1: unused on hot path; new file body fill is done in Create() via Crypto::GenerateSecureRandom.
        return true;
    }

    bool ZdbFile::IsValidOffset(uint64_t offset, size_t length) const
    {
        if (length == 0) {
            return true;
        }
        const uint64_t total = m_header.totalSize;
        if (offset > total || length > total || offset + length > total) {
            return false;
        }
        return true;
    }

    bool ZdbFile::Create(const std::string& filePath)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastError.clear();
        Close();

        m_filePath = filePath;
        m_file.open(filePath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!m_file.is_open()) {
            m_lastError = "ZdbFile::Create open failed";
            return false;
        }

        if (!InitHeader()) {
            m_file.close();
            return false;
        }

        m_file.write(reinterpret_cast<const char*>(&m_header), static_cast<std::streamsize>(sizeof(m_header)));
        if (!m_file) {
            m_lastError = "ZdbFile::Create write header failed";
            m_file.close();
            return false;
        }

        // 头之后至文件尾：用 CSPRNG 填满（与产品文档「随机噪声」一致；不依赖 Crypto::Init；OpenSSL RAND_bytes，Unix 可再读 /dev/urandom）
        const size_t kChunk = ZChatIM::FILE_CHUNK_SIZE;
        for (uint64_t pos = kHeaderBytes; pos < m_header.totalSize;) {
            const uint64_t remain = m_header.totalSize - pos;
            const size_t   n      = static_cast<size_t>(std::min<uint64_t>(remain, kChunk));
            std::vector<uint8_t> noise = Crypto::GenerateSecureRandom(n);
            if (noise.size() != n) {
                m_lastError = "ZdbFile::Create secure random fill failed (RNG)";
                m_file.close();
                return false;
            }
            m_file.write(reinterpret_cast<const char*>(noise.data()), static_cast<std::streamsize>(n));
            if (!m_file) {
                m_lastError = "ZdbFile::Create fill body failed";
                m_file.close();
                return false;
            }
            pos += n;
        }
        m_file.flush();
        m_file.close();

        m_file.open(filePath, std::ios::binary | std::ios::in | std::ios::out);
        if (!m_file.is_open()) {
            m_lastError = "ZdbFile::Create reopen failed";
            return false;
        }
        if (!ReadHeader()) {
            m_file.close();
            return false;
        }
        {
            std::error_code ec;
            const auto      bytesOnDisk = std::filesystem::file_size(filePath, ec);
            if (ec || static_cast<std::uint64_t>(bytesOnDisk) != m_header.totalSize) {
                m_lastError = "ZdbFile::Create on-disk size mismatch header.totalSize";
                m_file.close();
                return false;
            }
        }
        m_freeSlots.clear();
        return true;
    }

    bool ZdbFile::Open(const std::string& filePath)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastError.clear();
        Close();

        m_filePath = filePath;
        m_file.open(filePath, std::ios::binary | std::ios::in | std::ios::out);
        if (!m_file.is_open()) {
            m_lastError = "ZdbFile::Open failed";
            return false;
        }
        if (!ReadHeader()) {
            m_file.close();
            return false;
        }
        std::error_code ec;
        const auto      bytesOnDisk = std::filesystem::file_size(filePath, ec);
        if (ec || static_cast<std::uint64_t>(bytesOnDisk) != m_header.totalSize) {
            m_lastError = "ZDB on-disk size mismatch header.totalSize";
            m_file.close();
            return false;
        }
        return true;
    }

    void ZdbFile::Close()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_file.is_open()) {
            m_file.flush();
            m_file.close();
        }
        m_filePath.clear();
    }

    bool ZdbFile::IsOpen() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_file.is_open();
    }

    bool ZdbFile::WriteData(uint64_t offset, const uint8_t* data, size_t length)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastError.clear();
        if (!m_file.is_open()) {
            m_lastError = "ZdbFile not open";
            return false;
        }
        if (offset < kHeaderBytes) {
            m_lastError = "WriteData offset must be >= sizeof(ZdbHeader)";
            return false;
        }
        if (!IsValidOffset(offset, length)) {
            m_lastError = "WriteData out of range";
            return false;
        }
        m_file.seekp(static_cast<std::streamoff>(offset));
        if (length > 0) {
            if (data == nullptr) {
                m_lastError = "WriteData null data";
                return false;
            }
            m_file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
        }
        if (!m_file) {
            m_lastError = "WriteData stream failed";
            return false;
        }
        const uint64_t end = offset + static_cast<uint64_t>(length);
        if (end > m_header.usedSize) {
            m_header.usedSize = end;
        }
        return WriteHeader();
    }

    bool ZdbFile::AppendRaw(const uint8_t* data, size_t length, uint64_t& outOffset)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastError.clear();
        outOffset = 0;
        if (!m_file.is_open()) {
            m_lastError = "ZdbFile not open";
            return false;
        }
        const uint64_t start = m_header.usedSize;
        if (start + static_cast<uint64_t>(length) > m_header.totalSize) {
            m_lastError = "ZdbFile full";
            return false;
        }
        if (length > 0 && data == nullptr) {
            m_lastError = "AppendRaw null data";
            return false;
        }
        if (length > static_cast<size_t>((std::numeric_limits<std::streamsize>::max)())) {
            m_lastError = "AppendRaw length too large for stream";
            return false;
        }
        m_file.seekp(static_cast<std::streamoff>(start));
        if (length > 0) {
            m_file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
        }
        if (!m_file) {
            m_lastError = "AppendRaw write failed";
            return false;
        }
        m_header.usedSize = start + static_cast<uint64_t>(length);
        if (!WriteHeader()) {
            return false;
        }
        outOffset = start;
        return true;
    }

    bool ZdbFile::ReadData(uint64_t offset, uint8_t* buffer, size_t length)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastError.clear();
        if (!m_file.is_open()) {
            m_lastError = "ZdbFile not open";
            return false;
        }
        if (buffer == nullptr && length > 0) {
            m_lastError = "ReadData null buffer";
            return false;
        }
        if (!IsValidOffset(offset, length)) {
            m_lastError = "ReadData out of range";
            return false;
        }
        m_file.seekg(static_cast<std::streamoff>(offset));
        if (length > 0) {
            m_file.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(length));
        }
        if (!m_file || static_cast<size_t>(m_file.gcount()) != length) {
            m_lastError = "ReadData short read";
            return false;
        }
        return true;
    }

    bool ZdbFile::OverwriteData(uint64_t offset, size_t length)
    {
        if (length == 0) {
            return true;
        }
        // 分块清零，避免单次 `vector(length)` 在极大 length 下 OOM 或异常分配。
        constexpr size_t     kChunk = ZChatIM::FILE_CHUNK_SIZE;
        std::vector<uint8_t> zeros(std::min(kChunk, length), 0);
        size_t               remain = length;
        uint64_t             pos    = offset;
        while (remain > 0) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(remain, zeros.size()));
            if (!WriteData(pos, zeros.data(), n)) {
                return false;
            }
            pos += n;
            remain -= n;
        }
        return true;
    }

    bool ZdbFile::AllocateSpace(size_t size, uint64_t& outOffset)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastError.clear();
        outOffset = m_header.usedSize;
        if (static_cast<uint64_t>(size) > m_header.totalSize
            || outOffset + static_cast<uint64_t>(size) > m_header.totalSize) {
            m_lastError = "AllocateSpace no room";
            return false;
        }
        return true;
    }

    bool ZdbFile::FreeSpace(uint64_t /*offset*/, size_t /*size*/)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        // v1: no free list
        return true;
    }

    size_t ZdbFile::GetAvailableSpace() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_header.usedSize >= m_header.totalSize) {
            return 0;
        }
        return static_cast<size_t>(m_header.totalSize - m_header.usedSize);
    }

    size_t ZdbFile::GetUsedSpace() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return static_cast<size_t>(m_header.usedSize);
    }

    size_t ZdbFile::GetTotalSpace() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return static_cast<size_t>(m_header.totalSize);
    }

    std::string ZdbFile::GetFilePath() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_filePath;
    }

    std::string ZdbFile::GetFileId() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_filePath.empty()) {
            return {};
        }
        return std::filesystem::path(m_filePath).filename().string();
    }

    bool ZdbFile::IsFull() const
    {
        return GetAvailableSpace() == 0;
    }

    bool ZdbFile::IsCorrupted() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (std::memcmp(m_header.magic, ZChatIM::MAGIC_ZDB, sizeof(m_header.magic)) != 0) {
            return true;
        }
        if (m_header.version != 1) {
            return true;
        }
        if (m_header.totalSize != static_cast<std::uint64_t>(ZChatIM::ZDB_FILE_SIZE)) {
            return true;
        }
        if (m_header.usedSize < kHeaderBytes || m_header.usedSize > m_header.totalSize) {
            return true;
        }
        return false;
    }

} // namespace ZChatIM::mm2
