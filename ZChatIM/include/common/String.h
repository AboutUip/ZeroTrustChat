#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ZChatIM
{
    namespace common
    {
        // =============================================================
        // 字符串工具类
        // =============================================================
        
        class String {
        public:
            // =============================================================
            // 字符串操作
            // =============================================================
            
            // 字符串分割
            static std::vector<std::string> Split(const std::string& str, char delimiter);
            
            // 字符串分割（多个分隔符）
            static std::vector<std::string> Split(const std::string& str, const std::string& delimiters);
            
            // 字符串连接
            static std::string Join(const std::vector<std::string>& strings, const std::string& separator);
            
            // 字符串替换
            static std::string Replace(const std::string& str, const std::string& oldStr, const std::string& newStr);
            
            // 字符串查找
            static size_t Find(const std::string& str, const std::string& substr);
            
            // 字符串查找（从指定位置）
            static size_t Find(const std::string& str, const std::string& substr, size_t pos);
            
            // =============================================================
            // 字符串转换
            // =============================================================
            
            // 转换为小写
            static std::string ToLower(const std::string& str);
            
            // 转换为大写
            static std::string ToUpper(const std::string& str);
            
            // 去除首尾空白
            static std::string Trim(const std::string& str);
            
            // 去除左侧空白
            static std::string TrimLeft(const std::string& str);
            
            // 去除右侧空白
            static std::string TrimRight(const std::string& str);
            
            // =============================================================
            // 字符串检查
            // =============================================================
            
            // 检查是否为空
            static bool IsEmpty(const std::string& str);
            
            // 检查是否为空白
            static bool IsBlank(const std::string& str);
            
            // 检查是否以指定前缀开头
            static bool StartsWith(const std::string& str, const std::string& prefix);
            
            // 检查是否以指定后缀结尾
            static bool EndsWith(const std::string& str, const std::string& suffix);
            
            // 检查是否包含指定子串
            static bool Contains(const std::string& str, const std::string& substr);
            
            // =============================================================
            // 数值转换
            // =============================================================
            
            // 字符串转整数
            static int32_t ToInt32(const std::string& str);
            
            // 字符串转无符号整数
            static uint32_t ToUInt32(const std::string& str);
            
            // 字符串转64位整数
            static int64_t ToInt64(const std::string& str);
            
            // 字符串转无符号64位整数
            static uint64_t ToUInt64(const std::string& str);
            
            // 字符串转浮点数
            static double ToDouble(const std::string& str);
            
            // 整数转字符串
            static std::string FromInt32(int32_t value);
            
            // 无符号整数转字符串
            static std::string FromUInt32(uint32_t value);
            
            // 64位整数转字符串
            static std::string FromInt64(int64_t value);
            
            // 无符号64位整数转字符串
            static std::string FromUInt64(uint64_t value);
            
            // 浮点数转字符串
            static std::string FromDouble(double value);
            
            // =============================================================
            // 编码转换
            // =============================================================
            
            // UTF-8 转 GBK
            static std::string Utf8ToGbk(const std::string& utf8);
            
            // GBK 转 UTF-8
            static std::string GbkToUtf8(const std::string& gbk);
            
            // =============================================================
            // 安全操作
            // =============================================================
            
            // 安全的字符串比较
            static bool SecureCompare(const std::string& str1, const std::string& str2);
            
            // 生成安全的字符串哈希
            static uint64_t GenerateHashCode(const std::string& str);
            
        private:
            // 禁止实例化
            String() = delete;
            ~String() = delete;
        };
        
    } // namespace common
} // namespace ZChatIM
