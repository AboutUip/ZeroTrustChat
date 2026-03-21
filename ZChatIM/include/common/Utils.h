#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ZChatIM
{
    namespace common
    {
        // =============================================================
        // 通用工具类
        // =============================================================
        
        class Utils {
        public:
            // =============================================================
            // 字节转换
            // =============================================================
            
            // 字节数组转十六进制字符串
            static std::string BytesToHex(const uint8_t* data, size_t length);
            
            // 十六进制字符串转字节数组
            static bool HexToBytes(const std::string& hex, uint8_t* output, size_t outputLen);
            
            // 字符串转字节数组
            static std::vector<uint8_t> StringToBytes(const std::string& str);
            
            // 字节数组转字符串
            static std::string BytesToString(const uint8_t* data, size_t length);
            
            // =============================================================
            // 数值转换
            // =============================================================
            
            // 大端转小端（32位）
            static uint32_t BigEndianToLittleEndian(uint32_t value);
            
            // 小端转大端（32位）
            static uint32_t LittleEndianToBigEndian(uint32_t value);
            
            // 大端转小端（64位）
            static uint64_t BigEndianToLittleEndian64(uint64_t value);
            
            // 小端转大端（64位）
            static uint64_t LittleEndianToBigEndian64(uint64_t value);
            
            // =============================================================
            // 校验和
            // =============================================================
            
            // 计算 CRC32
            static uint32_t CalculateCRC32(const uint8_t* data, size_t length);
            
            // 计算 Adler32
            static uint32_t CalculateAdler32(const uint8_t* data, size_t length);
            
        private:
            // 禁止实例化
            Utils() = delete;
            ~Utils() = delete;
        };
        
    } // namespace common
} // namespace ZChatIM
