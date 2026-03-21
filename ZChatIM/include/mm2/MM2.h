#pragma once

#include "../Types.h"
#include "../common/JniSecurityPolicy.h"
#include "storage/Crypto.h"
#include "storage/MessageBlock.h"
#include "storage/ZdbManager.h"
#include "storage/BlockIndex.h"
#include "storage/StorageIntegrityManager.h"
#include "MessageQueryManager.h"
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // MM2 模块 - 消息加密存储
        // -------------------------------------------------------------
        // 并发：public 实例方法实现 SHOULD 在入口持有 m_stateMutex（递归）；
        // GetMessageQueryManager() 返回的查询器仅在此锁保护下使用（见 JniSecurityPolicy）。
        // =============================================================

        class MM2 {
        public:
            // =============================================================
            // 构造函数/析构函数
            // =============================================================
            
            MM2();
            ~MM2();
            
            // =============================================================
            // 初始化
            // =============================================================
            
            // 初始化
            bool Initialize(const std::string& dataDir, const std::string& indexDir);
            
            // 清理
            void Cleanup();
            
            // =============================================================
            // 消息操作
            // =============================================================
            
            // 存储消息
            bool StoreMessage(const std::vector<uint8_t>& sessionId, const std::vector<uint8_t>& payload, std::vector<uint8_t>& outMessageId);
            
            // 检索消息
            bool RetrieveMessage(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& outPayload);
            
            // 删除消息
            // 删除消息（保留回复关系，确保回复链路在消息撤回后仍可显示“原消息已删除”）
            bool DeleteMessage(const std::vector<uint8_t>& messageId);

            // =============================================================
            // 消息缓存/已读状态
            // =============================================================
            // MarkMessageRead: 将消息标记为已读（从未读 LRU 淘汰集合中移除）
            bool MarkMessageRead(const std::vector<uint8_t>& messageId, uint64_t readTimestampMs);

            // 获取会话未读消息（用于实现未读 LRU）
            bool GetUnreadSessionMessages(
                const std::vector<uint8_t>& sessionId,
                size_t limit,
                std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outUnreadMessages);

            // =============================================================
            // 消息回复关系
            // =============================================================
            // StoreMessageReplyRelation: 存储回复链路关联
            bool StoreMessageReplyRelation(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& repliedMsgId,
                const std::vector<uint8_t>& repliedSenderId,
                const std::vector<uint8_t>& repliedContentDigest);

            // GetMessageReplyRelation: 查询回复链路关联（支持 repliedMsgId 被删除后的摘要显示）
            bool GetMessageReplyRelation(
                const std::vector<uint8_t>& messageId,
                std::vector<uint8_t>& outRepliedMsgId,
                std::vector<uint8_t>& outRepliedSenderId,
                std::vector<uint8_t>& outRepliedContentDigest);

            // 编辑消息（仅更新密文与编辑状态；编辑权限校验在上层/MM1 完成）
            // newEditCount: 编辑次数（实现层应保证 <=3 等规则）
            bool EditMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                uint32_t newEditCount);

            // 获取消息编辑状态（editCount 与 lastEditTimeSeconds）
            bool GetMessageEditState(
                const std::vector<uint8_t>& messageId,
                uint32_t& outEditCount,
                uint64_t& outLastEditTimeSeconds);
            
            // 批量存储消息
            bool StoreMessages(const std::vector<uint8_t>& sessionId, const std::vector<std::vector<uint8_t>>& payloads, std::vector<std::vector<uint8_t>>& outMessageIds);
            
            // 批量检索消息
            bool RetrieveMessages(const std::vector<std::vector<uint8_t>>& messageIds, std::vector<std::vector<uint8_t>>& outPayloads);
            
            // =============================================================
            // 文件操作
            // =============================================================
            
            // 存储文件分片
            // 必须：写入 .zdb 后计算 sha256，并调用 RecordDataBlockHash 记录到 SQLite
            bool StoreFileChunk(const std::string& fileId, uint32_t chunkIndex, const std::vector<uint8_t>& data);
            
            // 获取文件分片
            // 必须：从 .zdb 读取后计算 sha256，并调用 VerifyDataBlockHash 与 SQLite 比对（不一致则标记失效；实现层策略）
            bool GetFileChunk(const std::string& fileId, uint32_t chunkIndex, std::vector<uint8_t>& outData);
            
            // 完成文件传输
            bool CompleteFile(const std::string& fileId, const uint8_t* sha256);
            
            // 取消文件传输
            bool CancelFile(const std::string& fileId);

            // =============================================================
            // 文件传输续传断点
            // =============================================================
            // StoreTransferResumeChunkIndex: 记录最后成功接收的 chunkIndex（MM2 内存）
            bool StoreTransferResumeChunkIndex(const std::string& fileId, uint32_t chunkIndex);

            // GetTransferResumeChunkIndex: 获取续传断点 chunkIndex
            bool GetTransferResumeChunkIndex(const std::string& fileId, uint32_t& outChunkIndex) const;

            // CleanupTransferResumeChunkIndex: 清理续传断点
            bool CleanupTransferResumeChunkIndex(const std::string& fileId);

            // =============================================================
            // 好友验证（好友请求记录存储）
            // =============================================================
            // storeFriendRequest(fromUserId,toUserId,timestamp,signature) -> requestId
            bool StoreFriendRequest(
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519,
                std::vector<uint8_t>& outRequestId);

            // respondFriendRequest(requestId,accept,responderId,timestamp,signature) -> result
            bool UpdateFriendRequestStatus(
                const std::vector<uint8_t>& requestId,
                bool accept,
                const std::vector<uint8_t>& responderId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            // deleteFriendRequest(requestId) -> result
            bool DeleteFriendRequest(const std::vector<uint8_t>& requestId);

            // cleanupExpiredFriendRequests(nowMs): 清理过期请求记录
            bool CleanupExpiredFriendRequests(uint64_t nowMs);

            // =============================================================
            // 群组名称元数据（groupName 更新）
            // =============================================================
            bool UpdateGroupName(
                const std::vector<uint8_t>& groupId,
                const std::string& newGroupName,
                uint64_t updateTimeSeconds,
                const std::vector<uint8_t>& updateBy);

            bool GetGroupName(
                const std::vector<uint8_t>& groupId,
                std::string& outGroupName);
            
            // =============================================================
            // 会话管理
            // =============================================================
            
            // 获取会话消息
            bool GetSessionMessages(const std::vector<uint8_t>& sessionId, size_t limit, std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& outMessages);
            
            // 清理会话过期消息
            bool CleanupSessionMessages(const std::vector<uint8_t>& sessionId);
            
            // =============================================================
            // 数据清理
            // =============================================================
            
            // 清理过期数据
            bool CleanupExpiredData();
            
            // 清理所有数据
            bool CleanupAllData();
            
            // 优化存储
            bool OptimizeStorage();
            
            // =============================================================
            // 状态查询
            // =============================================================
            
            // 获取存储状态
            bool GetStorageStatus(size_t& totalSpace, size_t& usedSpace, size_t& availableSpace);
            
            // 获取文件数量
            size_t GetFileCount();
            
            // 获取消息数量
            size_t GetMessageCount();

            // =============================================================
            // 存储完整性校验（SQLite）
            // =============================================================
            // Compute / Record / Verify data_blocks.sha256（契约层接口）
            bool RecordDataBlockHash(
                const std::string& dataId,
                uint32_t chunkIndex,
                const std::string& fileId,
                uint64_t offset,
                uint64_t length,
                const uint8_t sha256[32]);

            bool VerifyDataBlockHash(
                const std::string& dataId,
                uint32_t chunkIndex,
                const uint8_t sha256[32],
                bool& outMatch);

            // 获取完整性校验管理器（用于上层/MM2 实现调度）
            StorageIntegrityManager& GetStorageIntegrityManager();

            // =============================================================
            // 消息查询 / 同步（ListMessages*）
            // -------------------------------------------------------------
            // 与 MM2 同属可信存储边界；JniBridge 对 listMessages* 应路由至此，
            // 不得在桥接层旁路构造独立的 MessageQueryManager（避免与索引/.zdb 状态脱节）。
            // 安全：实现须遵守可见性与授权策略（如按 userId/session 过滤），且不得用查询接口
            // 替代 MM1 侧强制路径（Recall/Reply/Edit 的签名校验仍须走对应 MM1 管理器）。
            // =============================================================
            MessageQueryManager& GetMessageQueryManager();
            
            // =============================================================
            // 静态方法
            // =============================================================
            
            // 获取单例实例
            static MM2& Instance();
            
            // 获取下一个序列号
            static uint64_t GetNextSequence();
            
        private:
            // =============================================================
            // 内部方法
            // =============================================================
            
            // 生成消息ID
            std::vector<uint8_t> GenerateMessageId();
            
            // 生成文件ID
            std::string GenerateFileId();
            
            // 检查消息是否过期
            bool IsMessageExpired(uint64_t timestamp);
            
            // 检查文件是否过期
            bool IsFileExpired(uint64_t timestamp);
            
            // 销毁数据（Level 2）
            bool DestroyData(const std::string& fileId, uint64_t offset, size_t length);
            
            // =============================================================
            // 成员变量
            // =============================================================
            
            bool m_initialized;                       // 初始化状态
            Crypto m_crypto;                          // 加密组件
            ZdbManager m_zdbManager;                  // ZDB文件管理器
            BlockIndex m_blockIndex;                  // 块索引
            // 状态互斥（递归）：所有 public 实例方法实现 MUST 在入口持锁；MessageQueryManager 仅在此锁保护下调用。
            mutable std::recursive_mutex m_stateMutex;
            
            // 会话消息缓存
            std::map<std::string, std::vector<std::pair<std::vector<uint8_t>, uint64_t>>> m_sessionCache;

            // 存储完整性校验管理器（记录/比对 sha256 到 SQLite）
            StorageIntegrityManager m_storageIntegrityManager;

            // 消息列表与同步查询（与 BlockIndex / 会话缓存一致生命周期）
            MessageQueryManager m_messageQueryManager;
            
            // 序列号
            static std::atomic<uint64_t> s_sequence;
        };
        
    } // namespace mm2
} // namespace ZChatIM
