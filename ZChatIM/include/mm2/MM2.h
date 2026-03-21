#pragma once

#include "../Types.h"
#include "../common/JniSecurityPolicy.h"
#include "storage/MessageBlock.h"
#include "storage/ZdbManager.h"
#include "storage/SqliteMetadataDb.h"
#include "storage/BlockIndex.h"
#include "storage/StorageIntegrityManager.h"
#include "MessageQueryManager.h"
#include <atomic>
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <mutex>
#include <string_view>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // MM2 模块 - 消息加密存储
        // -------------------------------------------------------------
        // 并发：public 实例方法实现应在入口持有 m_stateMutex（递归）；LastError()/SetLastError 内部亦使用该锁。
        // **`MessageQueryManager::List*`** 经 **`MM2` 内部回调**持同一 **`m_stateMutex`**，可与其它 MM2 API 安全交错调用。
        // **`GetStorageIntegrityManager()`** 返回引用不持锁；对其子调用仍须与 MM2 **串行**（见 JniSecurityPolicy）。
        // =============================================================

        class MM2 {
            friend class MessageQueryManager;

        public:
            // =============================================================
            // 构造函数/析构函数
            // =============================================================
            
            MM2();
            ~MM2();
            
            // =============================================================
            // 初始化
            // =============================================================
            
            // 初始化：**`dataDir` / `indexDir` 须非空**字符串。`dataDir` 下放 `.zdb`；`indexDir` 下创建 **`zchatim_metadata.db`**（Windows 上 SQLite 打开路径见 `SqliteMetadataDb::Open` 宽路径→UTF-8；JNI/上层宜传 `filesystem::path` 再转窄串时知悉 ACP 限制）。
            // **`Crypto::Init` 与 `mm2_message_key.bin`** 推迟到首次需加解密的路径（**`EnsureMessageCryptoReadyUnlocked`**），例如 **`StoreMessage` / `StoreMessages` / `RetrieveMessage` / `RetrieveMessages` / `GetSessionMessages`**（**`limit>0`** 时）；**`DeleteMessage` 不经过该路径**（不解密，仅清零 `.zdb` 区间与删索引行）。**`StoreFileChunk` / `GetFileChunk`** 仅需 ZDB+SQLite+SIM 即可完成 **`Initialize`**。
            // **Windows**：密钥文件为 **ZMK1（DPAPI 封装）**，**兼容**旧版 **32 字节明文**（加载后会尽力改写）；**非 Windows**：仍为 **32 字节明文**（见 **`03-Storage.md`**）。
            bool Initialize(const std::string& dataDir, const std::string& indexDir);
            
            // 清理
            void Cleanup();
            
            // =============================================================
            // 消息操作
            // =============================================================
            
            // 存储消息：`sessionId` 须 **`USER_ID_SIZE`（16）** 字节（与 JNI `imSessionId` 约定一致）。
            // 明文经 **AES-256-GCM**（`mm2::Crypto`：**Windows BCrypt** / **Unix OpenSSL 3**）后写入 `.zdb`；`outMessageId` 为 16 字节随机 id。
            // 单条明文上限 **`ZDB_MAX_WRITE_SIZE - NONCE_SIZE - AUTH_TAG_SIZE`**。失败时查 **`LastError()`**。
            bool StoreMessage(const std::vector<uint8_t>& sessionId, const std::vector<uint8_t>& payload, std::vector<uint8_t>& outMessageId);
            
            // 检索消息：`messageId` 须 16 字节；要求 **`im_messages`** 与 **`data_blocks`** 一致。失败时 **`outPayload` 清空**。
            bool RetrieveMessage(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& outPayload);
            
            // 删除消息：须有 **im_messages** 行（避免误删 **StoreFileChunk** 的 **data_blocks**）；**data_blocks** 存在时先 **ZdbManager::DeleteData** 清零密文区再删索引行；并删 **`im_message_reply`** 中 **`message_id`** 指向本条的行。
            bool DeleteMessage(const std::vector<uint8_t>& messageId);

            // =============================================================
            // 消息缓存/已读状态
            // =============================================================
            // 将消息标为已读：SQLite **`im_messages.read_at_ms`** 从 NULL 写入 **`readTimestampMs`**（Unix 毫秒）；已读则 **true**（幂等）。须存在 **`im_messages`** 行。
            bool MarkMessageRead(const std::vector<uint8_t>& messageId, uint64_t readTimestampMs);

            // 会话内未读（**`read_at_ms IS NULL`**）的 **`message_id`**，按插入顺序 **升序**，最多 **`limit`** 条。**`limit==0`** 成功且空。配对第二元：**未读占位为 0**（与 JNI **`GetUnreadSessionMessageIds`** 仅 id 对齐；**不**触达加解密）。
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
            
            // 批量存储消息：顺序调用 **`StoreMessage`**；**`sessionId`** 须 **`USER_ID_SIZE`**。任一条失败返回 **`false`**，并对本批**已成功**写入的条目**内部回滚**（与 **`DeleteMessage`** 同等清理），**`outMessageIds` 清空**（**全有或全无**；**`LastError`** 保留**首条失败原因**）。
            bool StoreMessages(const std::vector<uint8_t>& sessionId, const std::vector<std::vector<uint8_t>>& payloads, std::vector<std::vector<uint8_t>>& outMessageIds);
            
            // 批量检索消息：顺序 **`RetrieveMessage`**；**任一条失败**则返回 **`false`** 并**清空** **`outPayloads`**（全有或全无）。
            bool RetrieveMessages(const std::vector<std::vector<uint8_t>>& messageIds, std::vector<std::vector<uint8_t>>& outPayloads);
            
            // =============================================================
            // 文件操作
            // =============================================================
            
            // 存储文件分片：`data_blocks.data_id`（16 字节）= SHA256(fileId 字节 ‖ chunkIndex 小端 u32) 的前 16 字节；`chunk_idx` 与 chunkIndex 一致。
            // 单次 `data.size()` ≤ `ZDB_MAX_WRITE_SIZE`；覆盖同 (fileId, chunkIndex) 时先擦除旧物理区间再追加。
            // 写入 .zdb → UpsertZdbFile → ComputeSha256 → RecordDataBlockHash（与 `03-Storage.md` 第七节 MM2 行一致）。
            bool StoreFileChunk(const std::string& fileId, uint32_t chunkIndex, const std::vector<uint8_t>& data);
            
            // 获取文件分片：按上式派生 data_id 查 SQLite → ReadData → SHA256 与 VerifyDataBlockHash；校验失败则清空 outData。
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
            bool GetTransferResumeChunkIndex(const std::string& fileId, uint32_t& outChunkIndex);

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
            
            // 获取会话消息：**`limit==0`** 成功且空（不触达密钥就绪）；否则取该会话下**最近** **`limit`** 条（按 **`im_messages.rowid`**），**`outMessages`** 为 **`(message_id, 明文 payload)`**，顺序为**插入顺序正序**（先插入者在前，**非**墙钟时间戳）。任一条 **`RetrieveMessage`** 失败则**清空** **`outMessages`** 并返回 **`false`**。须 **`Initialize`** 且能完成消息密钥就绪。
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

            // 最近一次失败原因（线程安全；与 SetLastError / 各 API 错误路径一致）
            std::string LastError() const;

            // =============================================================
            // 存储完整性校验（SQLite）
            // =============================================================
            // Compute / Record / Verify data_blocks.sha256（契约层接口）
            // 注意：dataId 与 SQLite `data_blocks.data_id` 对齐时，std::string 须承载 **16 字节原始二进制**
            //（`MESSAGE_ID_SIZE`），而非十六进制文本；实现层应委托 `GetStorageIntegrityManager()` 并做编码一致转换。
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

            // 已持有 m_stateMutex（与 ZdbManager::CleanupUnlocked 同理）
            void CleanupUnlocked();

            // 已持有 m_stateMutex；`indexDir/mm2_message_key.bin`（32 字节 AES-256 密钥）
            bool LoadOrCreateMessageStorageKeyUnlocked();

            // 已持有 m_stateMutex；首次 StoreMessage/RetrieveMessage 前调用：Crypto::Init + 加载/生成消息密钥
            bool EnsureMessageCryptoReadyUnlocked();
            // 已持有 m_stateMutex；`dataId16` 须为 MESSAGE_ID_SIZE 字节
            bool PutDataBlockBlobUnlocked(const std::vector<uint8_t>& dataId16, int32_t chunkIdx, const std::vector<uint8_t>& blob);
            bool GetDataBlockBlobUnlocked(const std::vector<uint8_t>& dataId16, int32_t chunkIdx, std::vector<uint8_t>& outBlob);

            // `.zdb` 已成功追加 `[offset,offset+blobLen)` 后，若 SQLite 等后续失败：清零该区间、删除 `data_blocks`（若仍有行）、`UpsertZdbFile` 刷新 used_size（v1 不收缩头内 usedSize，仅覆盖为 0）。
            bool RevertFailedPutDataBlockUnlocked(
                const std::vector<uint8_t>& dataId16,
                int32_t                     chunkIdx,
                const std::string&          zdbFileId,
                uint64_t                    offset,
                size_t                      blobLen);
            
            // =============================================================
            // 成员变量
            // =============================================================
            
            bool m_initialized;                       // 初始化状态
            ZdbManager m_zdbManager;                  // ZDB文件管理器
            SqliteMetadataDb m_metadataDb;            // 元数据索引（zdb_files / data_blocks）
            BlockIndex m_blockIndex;                  // 块索引
            // 状态互斥（递归）：所有 public 实例方法实现 MUST 在入口持锁；MessageQueryManager 仅在此锁保护下调用。
            mutable std::recursive_mutex m_stateMutex;
            
            // 会话消息缓存
            std::map<std::string, std::vector<std::pair<std::vector<uint8_t>, uint64_t>>> m_sessionCache;

            // 存储完整性校验管理器（记录/比对 sha256 到 SQLite）
            StorageIntegrityManager m_storageIntegrityManager;

            // 消息列表与同步查询（与 BlockIndex / 会话缓存一致生命周期）
            MessageQueryManager m_messageQueryManager;

            // Initialize 路径（CleanupAllData 等使用）
            std::string              m_dataDir;
            std::string              m_indexDir;
            std::filesystem::path    m_metadataDbPath;
            mutable std::string      m_lastError;

            // AES-256 消息载荷加密（`mm2/storage/Crypto`）；首次需要时由 EnsureMessageCryptoReadyUnlocked 加载或生成
            std::vector<uint8_t>     m_messageStorageKey;

            // 始终对 m_lastError 加锁写入（recursive_mutex，可与外层 API 已持锁嵌套）。
            void SetLastError(std::string_view message) const;

            // `MessageQueryManager` 回调：内部持锁，与 **`GetSessionMessages`** / **`RetrieveMessage`** 一致。
            std::vector<std::vector<uint8_t>> InternalListMessagesForQueryManager(const std::vector<uint8_t>& sessionId, int count);
            std::vector<std::vector<uint8_t>> InternalListMessagesSinceMessageIdForQueryManager(
                const std::vector<uint8_t>& sessionId,
                const std::vector<uint8_t>& lastMsgId,
                int                         count);
            std::vector<std::vector<uint8_t>> InternalListMessagesSinceTimestampForQueryManager(
                const std::vector<uint8_t>& sessionId,
                uint64_t                    sinceTimestampMs,
                int                         count);

            // 序列号
            static std::atomic<uint64_t> s_sequence;

            // 已持 **`m_stateMutex`**；**`DeleteMessage` / `CleanupSessionMessages`** 共用。
            bool DeleteMessageImplUnlocked(const std::vector<uint8_t>& messageId);
            // 删除某逻辑 **`fileId`** 下 **连续** chunk 索引 **0..N-1**（遇首个缺失则停）；用于 **`CancelFile` / `CompleteFile` 失败回滚**等。
            bool EraseAllChunksForLogicalFileUnlocked(const std::string& logicalFileId);
        };
        
    } // namespace mm2
} // namespace ZChatIM
