#include "common/File.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

namespace ZChatIM::common {

    bool File::Exists(const std::string& filePath)
    {
        std::error_code ec;
        return fs::exists(filePath, ec) && !ec;
    }

    bool File::IsFile(const std::string& path)
    {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    bool File::IsDirectory(const std::string& path)
    {
        std::error_code ec;
        return fs::is_directory(path, ec);
    }

    uint64_t File::GetSize(const std::string& filePath)
    {
        std::error_code ec;
        if (auto sz = fs::file_size(filePath, ec); !ec)
            return static_cast<uint64_t>(sz);
        return 0;
    }

    uint64_t File::GetLastModified(const std::string& filePath)
    {
        std::error_code ec;
        const auto t = fs::last_write_time(filePath, ec);
        if (ec)
            return 0;
        const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count());
    }

    bool File::Delete(const std::string& filePath)
    {
        std::error_code ec;
        return fs::remove(filePath, ec);
    }

    bool File::Rename(const std::string& oldPath, const std::string& newPath)
    {
        std::error_code ec;
        fs::rename(oldPath, newPath, ec);
        return !ec;
    }

    bool File::Copy(const std::string& srcPath, const std::string& destPath)
    {
        std::error_code ec;
        fs::copy_file(srcPath, destPath, fs::copy_options::overwrite_existing, ec);
        return !ec;
    }

    bool File::CreateDirectory(const std::string& path)
    {
        std::error_code ec;
        return fs::create_directory(path, ec);
    }

    bool File::CreateDirectoryRecursive(const std::string& path)
    {
        std::error_code ec;
        return fs::create_directories(path, ec);
    }

    bool File::DeleteDirectory(const std::string& path)
    {
        std::error_code ec;
        return fs::remove(path, ec);
    }

    bool File::DeleteDirectoryRecursive(const std::string& path)
    {
        std::error_code ec;
        const auto n = fs::remove_all(path, ec);
        return !ec && n != static_cast<std::uintmax_t>(-1);
    }

    bool File::ListDirectory(const std::string& path, std::vector<std::string>& out, std::error_code& ec)
    {
        out.clear();
        ec.clear();
        fs::directory_iterator it(path, ec);
        if (ec)
            return false;
        const fs::directory_iterator end{};
        while (it != end) {
            out.push_back(it->path().filename().string());
            it.increment(ec);
            if (ec)
                return false;
        }
        return true;
    }

    std::string File::GetFileName(const std::string& path)
    {
        return fs::path(path).filename().string();
    }

    std::string File::GetFileNameWithoutExtension(const std::string& path)
    {
        return fs::path(path).stem().string();
    }

    std::string File::GetFileExtension(const std::string& path)
    {
        return fs::path(path).extension().string();
    }

    std::string File::GetDirectoryPath(const std::string& path)
    {
        return fs::path(path).parent_path().string();
    }

    std::string File::JoinPath(const std::string& path1, const std::string& path2)
    {
        return (fs::path(path1) / path2).string();
    }

    std::string File::GetAbsolutePath(const std::string& path)
    {
        std::error_code ec;
        return fs::weakly_canonical(fs::absolute(path, ec), ec).string();
    }

    bool File::ReadFile(const std::string& filePath, std::vector<uint8_t>& buffer)
    {
        std::ifstream in(filePath, std::ios::binary | std::ios::ate);
        if (!in)
            return false;
        const std::streamoff len = in.tellg();
        if (len < 0)
            return false;
        if (!in.seekg(0, std::ios::beg))
            return false;
        if (len == 0) {
            buffer.clear();
            return true;
        }
        if (static_cast<std::uintmax_t>(len) > static_cast<std::uintmax_t>(std::numeric_limits<size_t>::max()))
            return false;
        buffer.resize(static_cast<size_t>(len));
        const auto toRead = static_cast<std::streamsize>(len);
        if (!in.read(reinterpret_cast<char*>(buffer.data()), toRead))
            return false;
        return in.gcount() == toRead;
    }

    bool File::WriteFile(const std::string& filePath, const uint8_t* data, size_t length)
    {
        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        if (length == 0)
            return true;
        if (!data)
            return false;
        if (length > static_cast<size_t>((std::numeric_limits<std::streamsize>::max)()))
            return false;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
        return static_cast<bool>(out);
    }

    bool File::AppendFile(const std::string& filePath, const uint8_t* data, size_t length)
    {
        std::ofstream out(filePath, std::ios::binary | std::ios::app);
        if (!out)
            return false;
        if (length == 0)
            return true;
        if (!data)
            return false;
        if (length > static_cast<size_t>((std::numeric_limits<std::streamsize>::max)()))
            return false;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
        return static_cast<bool>(out);
    }

} // namespace ZChatIM::common
