#include "common/String.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <sstream>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace ZChatIM::common {

    std::vector<std::string> String::Split(const std::string& str, char delimiter)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char c : str) {
            if (c == delimiter) {
                out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        out.push_back(cur);
        return out;
    }

    std::vector<std::string> String::Split(const std::string& str, const std::string& delimiters)
    {
        std::vector<std::string> out;
        size_t start = 0;
        for (;;) {
            const size_t end = str.find_first_of(delimiters, start);
            if (end == std::string::npos) {
                out.push_back(str.substr(start));
                break;
            }
            out.push_back(str.substr(start, end - start));
            start = end + 1;
        }
        return out;
    }

    std::string String::Join(const std::vector<std::string>& strings, const std::string& separator)
    {
        std::string out;
        for (size_t i = 0; i < strings.size(); ++i) {
            if (i > 0)
                out += separator;
            out += strings[i];
        }
        return out;
    }

    std::string String::Replace(const std::string& str, const std::string& oldStr, const std::string& newStr)
    {
        if (oldStr.empty())
            return str;
        std::string out;
        size_t pos = 0;
        while (pos < str.size()) {
            const size_t found = str.find(oldStr, pos);
            if (found == std::string::npos) {
                out.append(str, pos, std::string::npos);
                break;
            }
            out.append(str, pos, found - pos);
            out += newStr;
            pos = found + oldStr.size();
        }
        return out;
    }

    size_t String::Find(const std::string& str, const std::string& substr)
    {
        return str.find(substr);
    }

    size_t String::Find(const std::string& str, const std::string& substr, size_t pos)
    {
        return str.find(substr, pos);
    }

    std::string String::ToLower(const std::string& str)
    {
        std::string out = str;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::string String::ToUpper(const std::string& str)
    {
        std::string out = str;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return out;
    }

    std::string String::Trim(const std::string& str)
    {
        return TrimRight(TrimLeft(str));
    }

    std::string String::TrimLeft(const std::string& str)
    {
        size_t i = 0;
        while (i < str.size() && std::isspace(static_cast<unsigned char>(str[i])))
            ++i;
        return str.substr(i);
    }

    std::string String::TrimRight(const std::string& str)
    {
        if (str.empty())
            return str;
        size_t i = str.size();
        while (i > 0 && std::isspace(static_cast<unsigned char>(str[i - 1])))
            --i;
        return str.substr(0, i);
    }

    bool String::IsEmpty(const std::string& str)
    {
        return str.empty();
    }

    bool String::IsBlank(const std::string& str)
    {
        return Trim(str).empty();
    }

    bool String::StartsWith(const std::string& str, const std::string& prefix)
    {
        return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
    }

    bool String::EndsWith(const std::string& str, const std::string& suffix)
    {
        return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool String::Contains(const std::string& str, const std::string& substr)
    {
        return str.find(substr) != std::string::npos;
    }

    int32_t String::ToInt32(const std::string& str)
    {
        try {
            const long v = std::stol(str);
            if (v > static_cast<long>(INT32_MAX) || v < static_cast<long>(INT32_MIN))
                return 0;
            return static_cast<int32_t>(v);
        } catch (...) {
            return 0;
        }
    }

    uint32_t String::ToUInt32(const std::string& str)
    {
        try {
            const unsigned long v = std::stoul(str);
            if (v > static_cast<unsigned long>(UINT32_MAX))
                return 0;
            return static_cast<uint32_t>(v);
        } catch (...) {
            return 0;
        }
    }

    int64_t String::ToInt64(const std::string& str)
    {
        try {
            return std::stoll(str);
        } catch (...) {
            return 0;
        }
    }

    uint64_t String::ToUInt64(const std::string& str)
    {
        try {
            return std::stoull(str);
        } catch (...) {
            return 0;
        }
    }

    double String::ToDouble(const std::string& str)
    {
        try {
            return std::stod(str);
        } catch (...) {
            return 0.0;
        }
    }

    std::string String::FromInt32(int32_t value)
    {
        return std::to_string(value);
    }

    std::string String::FromUInt32(uint32_t value)
    {
        return std::to_string(value);
    }

    std::string String::FromInt64(int64_t value)
    {
        return std::to_string(value);
    }

    std::string String::FromUInt64(uint64_t value)
    {
        return std::to_string(value);
    }

    std::string String::FromDouble(double value)
    {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    std::string String::Utf8ToGbk(const std::string& utf8)
    {
#if defined(_WIN32)
        if (utf8.empty())
            return utf8;
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        if (wlen <= 0)
            return utf8;
        std::wstring wbuf(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wbuf.data(), wlen);
        const int glen = WideCharToMultiByte(936, 0, wbuf.c_str(), wlen, nullptr, 0, nullptr, nullptr);
        if (glen <= 0)
            return utf8;
        std::string out(static_cast<size_t>(glen), '\0');
        WideCharToMultiByte(936, 0, wbuf.c_str(), wlen, out.data(), glen, nullptr, nullptr);
        return out;
#else
        (void)utf8;
        return utf8;
#endif
    }

    std::string String::GbkToUtf8(const std::string& gbk)
    {
#if defined(_WIN32)
        if (gbk.empty())
            return gbk;
        const int wlen = MultiByteToWideChar(936, 0, gbk.c_str(), static_cast<int>(gbk.size()), nullptr, 0);
        if (wlen <= 0)
            return gbk;
        std::wstring wbuf(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(936, 0, gbk.c_str(), static_cast<int>(gbk.size()), wbuf.data(), wlen);
        const int u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), wlen, nullptr, 0, nullptr, nullptr);
        if (u8len <= 0)
            return gbk;
        std::string out(static_cast<size_t>(u8len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), wlen, out.data(), u8len, nullptr, nullptr);
        return out;
#else
        (void)gbk;
        return gbk;
#endif
    }

    bool String::SecureCompare(const std::string& str1, const std::string& str2)
    {
        if (str1.size() != str2.size())
            return false;
        unsigned char d = 0;
        for (size_t i = 0; i < str1.size(); ++i)
            d |= static_cast<unsigned char>(static_cast<unsigned char>(str1[i]) ^ static_cast<unsigned char>(str2[i]));
        return d == 0;
    }

    uint64_t String::GenerateHashCode(const std::string& str)
    {
        uint64_t h = 14695981039346656037ull;
        for (unsigned char c : str) {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

} // namespace ZChatIM::common
