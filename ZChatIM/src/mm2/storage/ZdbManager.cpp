#include "mm2/storage/ZdbManager.h"
#include "Types.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <sstream>

namespace ZChatIM::mm2 {

    namespace {

        bool EndsWithZdb(std::string_view name)
        {
            return name.size() >= 4 && name.compare(name.size() - 4, 4, ".zdb") == 0;
        }

    } // namespace

    ZdbManager::ZdbManager()
        : m_totalUsedSpace(0)
        , m_totalAvailableSpace(0)
    {
    }

    ZdbManager::~ZdbManager()
    {
        Cleanup();
    }

    void ZdbManager::CleanupUnlocked()
    {
        for (auto& kv : m_files) {
            if (kv.second) {
                kv.second->Close();
            }
        }
        m_files.clear();
        m_fileList.clear();
        m_totalUsedSpace      = 0;
        m_totalAvailableSpace = 0;
        m_dataDir.clear();
    }

    void ZdbManager::RefreshTotalsLocked()
    {
        m_totalUsedSpace      = 0;
        m_totalAvailableSpace = 0;
        for (const auto& id : m_fileList) {
            auto it = m_files.find(id);
            if (it == m_files.end() || !it->second) {
                continue;
            }
            m_totalUsedSpace += it->second->GetUsedSpace();
            m_totalAvailableSpace += it->second->GetAvailableSpace();
        }
    }

    bool ZdbManager::CheckDirectory()
    {
        std::error_code ec;
        std::filesystem::create_directories(m_dataDir, ec);
        if (ec) {
            m_lastError = "create_directories failed: " + ec.message();
            return false;
        }
        return true;
    }

    std::string ZdbManager::BuildFilePath(const std::string& fileId)
    {
        return (std::filesystem::path(m_dataDir) / fileId).string();
    }

    std::string ZdbManager::GenerateFileName()
    {
        std::random_device rd;
        for (int attempt = 0; attempt < 64; ++attempt) {
            const auto         seed = static_cast<uint64_t>(rd())
                ^ (static_cast<uint64_t>(
                       std::chrono::steady_clock::now().time_since_epoch().count())
                   << 1);
            std::ostringstream oss;
            oss << "z_" << std::hex << seed << ".zdb";
            const std::string  name = oss.str();
            const std::string  path = BuildFilePath(name);
            if (!std::filesystem::exists(path)) {
                return name;
            }
        }
        m_lastError = "GenerateFileName: could not allocate unique name";
        return {};
    }

    bool ZdbManager::ScanExistingFiles()
    {
        m_files.clear();
        m_fileList.clear();
        m_totalUsedSpace      = 0;
        m_totalAvailableSpace = 0;

        std::error_code ec;
        if (!std::filesystem::exists(m_dataDir, ec)) {
            return true;
        }

        for (const auto& entry : std::filesystem::directory_iterator(m_dataDir, ec)) {
            if (ec) {
                m_lastError = "directory_iterator: " + ec.message();
                return false;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string fname = entry.path().filename().string();
            if (!EndsWithZdb(fname)) {
                continue;
            }
            auto zf = std::make_shared<ZdbFile>();
            if (!zf->Open(entry.path().string())) {
                m_lastError = "ScanExistingFiles open failed (" + fname + "): " + zf->LastError();
                return false;
            }
            m_files[fname] = zf;
            m_fileList.push_back(fname);
        }

        std::sort(m_fileList.begin(), m_fileList.end());
        RefreshTotalsLocked();
        return true;
    }

    bool ZdbManager::CreateNewFileUnlocked(std::string& outFileId)
    {
        const std::string name = GenerateFileName();
        if (name.empty()) {
            return false;
        }
        const std::string path = BuildFilePath(name);
        auto              zf   = std::make_shared<ZdbFile>();
        if (!zf->Create(path)) {
            m_lastError = "CreateNewFileUnlocked: " + zf->LastError();
            return false;
        }
        m_files[name] = zf;
        m_fileList.push_back(name);
        std::sort(m_fileList.begin(), m_fileList.end());
        outFileId = name;
        RefreshTotalsLocked();
        return true;
    }

    bool ZdbManager::Initialize(const std::string& dataDir)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        CleanupUnlocked();

        m_dataDir = dataDir;
        if (!CheckDirectory()) {
            return false;
        }
        if (!ScanExistingFiles()) {
            return false;
        }
        return true;
    }

    void ZdbManager::Cleanup()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupUnlocked();
    }

    bool ZdbManager::CreateFile(std::string& outFileId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        if (m_dataDir.empty()) {
            m_lastError = "ZdbManager not initialized";
            return false;
        }
        return CreateNewFileUnlocked(outFileId);
    }

    bool ZdbManager::OpenFile(const std::string& fileId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        if (m_dataDir.empty()) {
            m_lastError = "ZdbManager not initialized";
            return false;
        }
        if (m_files.count(fileId) != 0) {
            return true;
        }
        const std::string path = BuildFilePath(fileId);
        if (!std::filesystem::exists(path)) {
            m_lastError = "OpenFile: path not found";
            return false;
        }
        auto zf = std::make_shared<ZdbFile>();
        if (!zf->Open(path)) {
            m_lastError = zf->LastError();
            return false;
        }
        m_files[fileId] = zf;
        if (std::find(m_fileList.begin(), m_fileList.end(), fileId) == m_fileList.end()) {
            m_fileList.push_back(fileId);
            std::sort(m_fileList.begin(), m_fileList.end());
        }
        RefreshTotalsLocked();
        return true;
    }

    bool ZdbManager::CloseFile(const std::string& fileId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        auto it = m_files.find(fileId);
        if (it == m_files.end()) {
            m_lastError = "CloseFile: not open";
            return false;
        }
        if (it->second) {
            it->second->Close();
        }
        m_files.erase(it);
        m_fileList.erase(std::remove(m_fileList.begin(), m_fileList.end(), fileId), m_fileList.end());
        RefreshTotalsLocked();
        return true;
    }

    bool ZdbManager::DeleteFile(const std::string& fileId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        const std::string path = BuildFilePath(fileId);
        auto              it   = m_files.find(fileId);
        if (it != m_files.end()) {
            if (it->second) {
                it->second->Close();
            }
            m_files.erase(it);
        }
        m_fileList.erase(std::remove(m_fileList.begin(), m_fileList.end(), fileId), m_fileList.end());

        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec && std::filesystem::exists(path)) {
            m_lastError = "DeleteFile: remove failed: " + ec.message();
            return false;
        }
        RefreshTotalsLocked();
        return true;
    }

    bool ZdbManager::SelectFile(size_t size, std::string& outFileId)
    {
        for (const auto& id : m_fileList) {
            auto it = m_files.find(id);
            if (it == m_files.end() || !it->second) {
                continue;
            }
            if (it->second->GetAvailableSpace() >= size) {
                outFileId = id;
                return true;
            }
        }
        return false;
    }

    bool ZdbManager::WriteData(
        const std::string& dataId,
        const uint8_t*     data,
        size_t             length,
        std::string&       outFileId,
        uint64_t&          outOffset)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        outOffset = 0;
        outFileId.clear();

        if (m_dataDir.empty()) {
            m_lastError = "ZdbManager not initialized";
            return false;
        }
        if (dataId.size() != MESSAGE_ID_SIZE) {
            m_lastError = "WriteData: dataId must be MESSAGE_ID_SIZE (16) bytes";
            return false;
        }
        if (length > ZDB_MAX_WRITE_SIZE) {
            m_lastError = "WriteData: length exceeds ZDB_MAX_WRITE_SIZE";
            return false;
        }
        if (length > 0 && data == nullptr) {
            m_lastError = "WriteData: null data";
            return false;
        }

        std::string hostId;
        if (!SelectFile(length, hostId)) {
            if (!CreateNewFileUnlocked(hostId)) {
                return false;
            }
            if (!SelectFile(length, hostId)) {
                m_lastError = "WriteData: no space after CreateFile";
                return false;
            }
        }

        auto it = m_files.find(hostId);
        if (it == m_files.end() || !it->second) {
            m_lastError = "WriteData: internal file map inconsistent";
            return false;
        }

        uint64_t off = 0;
        if (!it->second->AppendRaw(data, length, off)) {
            m_lastError = it->second->LastError();
            return false;
        }
        outFileId = hostId;
        outOffset = off;
        RefreshTotalsLocked();
        (void)dataId; // v1: logical id reserved for metadata layer
        return true;
    }

    bool ZdbManager::ReadData(const std::string& fileId, uint64_t offset, uint8_t* buffer, size_t length)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        if (m_dataDir.empty()) {
            m_lastError = "ZdbManager not initialized";
            return false;
        }
        auto it = m_files.find(fileId);
        if (it == m_files.end() || !it->second) {
            m_lastError = "ReadData: file not open";
            return false;
        }
        if (!it->second->ReadData(offset, buffer, length)) {
            m_lastError = it->second->LastError();
            return false;
        }
        return true;
    }

    bool ZdbManager::DeleteData(const std::string& fileId, uint64_t offset, size_t length)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        auto it = m_files.find(fileId);
        if (it == m_files.end() || !it->second) {
            m_lastError = "DeleteData: file not open";
            return false;
        }
        if (!it->second->OverwriteData(offset, length)) {
            m_lastError = it->second->LastError();
            return false;
        }
        RefreshTotalsLocked();
        return true;
    }

    bool ZdbManager::AllocateSpace(size_t size, std::string& outFileId, uint64_t& outOffset)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        outOffset = 0;
        outFileId.clear();
        if (m_dataDir.empty()) {
            m_lastError = "ZdbManager not initialized";
            return false;
        }

        std::string hostId;
        if (!SelectFile(size, hostId)) {
            if (!CreateNewFileUnlocked(hostId)) {
                return false;
            }
            if (!SelectFile(size, hostId)) {
                m_lastError = "AllocateSpace: no space after CreateFile";
                return false;
            }
        }
        auto it = m_files.find(hostId);
        if (it == m_files.end() || !it->second) {
            m_lastError = "AllocateSpace: internal file map inconsistent";
            return false;
        }
        uint64_t off = 0;
        if (!it->second->AllocateSpace(size, off)) {
            m_lastError = it->second->LastError();
            return false;
        }
        outFileId = hostId;
        outOffset = off;
        return true;
    }

    bool ZdbManager::FreeSpace(const std::string& /*fileId*/, uint64_t /*offset*/, size_t /*size*/)
    {
        // v1: no free list
        return true;
    }

    size_t ZdbManager::GetTotalAvailableSpace() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t sum = 0;
        for (const auto& id : m_fileList) {
            auto it = m_files.find(id);
            if (it != m_files.end() && it->second) {
                sum += it->second->GetAvailableSpace();
            }
        }
        return sum;
    }

    size_t ZdbManager::GetTotalUsedSpace() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t sum = 0;
        for (const auto& id : m_fileList) {
            auto it = m_files.find(id);
            if (it != m_files.end() && it->second) {
                sum += it->second->GetUsedSpace();
            }
        }
        return sum;
    }

    size_t ZdbManager::GetTotalSpace() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t sum = 0;
        for (const auto& id : m_fileList) {
            auto it = m_files.find(id);
            if (it != m_files.end() && it->second) {
                sum += it->second->GetTotalSpace();
            }
        }
        return sum;
    }

    std::vector<std::string> ZdbManager::GetFileList() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_fileList;
    }

    bool ZdbManager::GetFileStatus(const std::string& fileId, size_t& usedSpace, size_t& availableSpace) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_files.find(fileId);
        if (it == m_files.end() || !it->second) {
            return false;
        }
        usedSpace      = it->second->GetUsedSpace();
        availableSpace = it->second->GetAvailableSpace();
        return true;
    }

    bool ZdbManager::NeedCreateNewFile() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_fileList.empty()) {
            return true;
        }
        for (const auto& id : m_fileList) {
            auto it = m_files.find(id);
            if (it != m_files.end() && it->second && it->second->GetAvailableSpace() > 0) {
                return false;
            }
        }
        return true;
    }

    bool ZdbManager::CleanupExpiredFiles()
    {
        // v1: policy not wired
        return true;
    }

} // namespace ZChatIM::mm2
