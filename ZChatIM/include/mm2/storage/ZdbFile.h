#pragma once

#include "Types.h"
#include <string>
#include <cstdint>
#include <fstream>
#include <mutex>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // ZDB文件
        // =============================================================
        
        class ZdbFile {
        public:
            // =============================================================
            // 构造函数/析构函数
            // =============================================================
            
            ZdbFile();
            ~ZdbFile();
            
            // =============================================================
            // 文件操作
            // =============================================================
            
            // 打开文件
            bool Open(const std::string& filePath);
            
            // 关闭文件
            void Close();
            
            // 创建文件
            bool Create(const std::string& filePath);
            
            // 检查文件是否打开
            bool IsOpen() const;
            
            // =============================================================
            // 数据操作
            // =============================================================
            
            // 写入数据（payload 区：offset >= sizeof(ZdbHeader)；成功后可扩展 usedSize）
            bool WriteData(uint64_t offset, const uint8_t* data, size_t length);

            // 在文件尾追加 payload（v1 推荐路径；outOffset 为写入起点，含头后第一字节起算）
            bool AppendRaw(const uint8_t* data, size_t length, uint64_t& outOffset);
            
            // 读取数据
            bool ReadData(uint64_t offset, uint8_t* buffer, size_t length);
            
            // 覆写数据（用于销毁）
            bool OverwriteData(uint64_t offset, size_t length);
            
            // =============================================================
            // 空间管理
            // =============================================================
            
            // 分配空间
            bool AllocateSpace(size_t size, uint64_t& outOffset);
            
            // 释放空间
            bool FreeSpace(uint64_t offset, size_t size);
            
            // 获取可用空间
            size_t GetAvailableSpace() const;
            
            // 获取已用空间
            size_t GetUsedSpace() const;
            
            // 获取总空间
            size_t GetTotalSpace() const;
            
            // =============================================================
            // 文件信息
            // =============================================================
            
            // 获取文件路径
            std::string GetFilePath() const;
            
            // 获取文件ID
            std::string GetFileId() const;
            
            // 检查文件是否已满
            bool IsFull() const;
            
            // 检查文件是否损坏
            bool IsCorrupted() const;

            std::string LastError() const { return m_lastError; }

        private:
            // =============================================================
            // 内部方法
            // =============================================================
            
            // 初始化文件头
            bool InitHeader();
            
            // 读取文件头
            bool ReadHeader();
            
            // 写入文件头
            bool WriteHeader();
            
            // 填充随机数据
            bool FillRandomData(uint64_t offset, size_t length);
            
            // 检查偏移量是否有效
            bool IsValidOffset(uint64_t offset, size_t length) const;
            
            // =============================================================
            // 成员变量
            // =============================================================
            
            std::string m_filePath;     // 文件路径
            std::fstream m_file;        // 文件流
            ZdbHeader m_header;         // 文件头
            mutable std::mutex m_mutex; // 互斥锁
            std::string m_lastError;

            // 空间管理（v1：仅追加，空闲表未使用）
            std::vector<uint64_t> m_freeSlots; // 空闲槽位
            size_t m_usedSpace;                // 与 m_header.usedSize 同步（遗留字段）
        };
        
    } // namespace mm2
} // namespace ZChatIM
