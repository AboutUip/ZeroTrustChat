#pragma once

#include "../Types.h"
#include "../common/JniSecurityPolicy.h"
#include "storage/MessageBlock.h"
#include "storage/ZdbManager.h"
#include "storage/SqliteMetadataDb.h"
#include "storage/StorageIntegrityManager.h"
#include "storage/MessageQueryManager.h"
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
        // MM2 模块 - 消息加密与元数据
        // -------------------------------------------------------------
        // **IM 消息**：**仅进程内内存**（AES-GCM 密文 **不落** SQLite / **`.zdb`**）。**文件分片 / 群密钥信封等**仍按 **`data_blocks` + `.zdb`** 持久化。
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
            
            // 初始化：**`dataDir` / `indexDir` 须非空**字符串。`dataDir` 下放 `.zdb`；`indexDir` 下创建 **`zchatim_metadata.db`**（Windows 上路径见 **`SqliteMetadataDb::Open`**：**宽路径→UTF-8**；**`ZCHATIM_USE_SQLCIPHER`** 时为 **`Open(path, key32)`**。JNI/上层宜传 `filesystem::path` 再转窄串时知悉 ACP 限制）。
            // **`ZCHATIM_USE_SQLCIPHER`**（默认 ON，见 CMake）：**`Initialize`** 内会 **`Crypto::Init`** + 加载/创建 **`mm2_message_key.bin`**，再派生 SQLCipher 密钥并打开元数据库（旧版明文库可自动迁移）。**否则**：**`Crypto::Init` 与 `mm2_message_key.bin`** 仍推迟到 **`EnsureMessageCryptoReadyUnlocked`**（首次需消息加解密时），例如 **`StoreMessage` / …**；**`DeleteMessage` 不经过该路径**（不解密，仅清零 `.zdb` 区间与删索引行）。**`StoreFileChunk` / `GetFileChunk`** 在无 SQLCipher 时仅需 ZDB+明文 SQLite+SIM 即可完成 **`Initialize`**。
            // **`indexDir/mm2_message_key.bin`**（**32B 消息主密钥**的落盘形态）：**Windows** **ZMK1**（**DPAPI** **`CryptProtectData`**），**兼容**旧 **32B 明文**并**尽力**改写；**非 Apple 的 Unix** **ZMK2**（机器 **/ 用户 / indexDir** 绑定派生 **+ AES-256-GCM**），**兼容**旧 **32B 明文**并**尽力**改写；**Apple** **ZMK3**（**Keychain** 存封装密钥 **+ GCM**，**`MM2_message_key_darwin.cpp`**），**兼容**旧 **32B 明文**并**尽力**改写；可选 **`ZMKP`**（**用户口令 + PBKDF2 + AES-GCM** 包裹主密钥，见 **`MM2_message_key_passphrase.cpp`**、**`docs/07-Engineering/04-M2-Key-Policy-And-Extensions.md`**）。详见 **`03-Storage.md` 第三节** 与 **`MM2.cpp`**。
            bool Initialize(const std::string& dataDir, const std::string& indexDir);
            // **`messageKeyPassphraseUtf8`** 非空：**新库**写入 **ZMKP**；**已有 ZMKP** 须用相同口令解锁。**已有 ZMK1/2/3** 时**不得**传口令（否则失败）。**须 `ZCHATIM_USE_SQLCIPHER=ON`**。**`nullptr`** 与两参数 **`Initialize`** 等价。
            bool Initialize(const std::string& dataDir, const std::string& indexDir, const char* messageKeyPassphraseUtf8);
            
            // 清理
            void Cleanup();

            // `Initialize` 成功且未 `Cleanup` 时为 true（供 MM1 判断可否走元数据库持久化）。
            bool IsInitialized() const;

            // MM1/JNI 用户级小型 BLOB（如 Ed25519 公钥类型 0x45444A31）：写入 mm1_user_kv（见 03-Storage.md）。
            // 须在 Initialize 之后调用；data 单条上限 16 MiB（与 SqliteMetadataDb 一致）。
            bool StoreMm1UserDataBlob(const std::vector<uint8_t>& userId, int32_t type, const std::vector<uint8_t>& data);
            // 成功：无行时 outData 为空；失败返回 false。
            bool GetMm1UserDataBlob(const std::vector<uint8_t>& userId, int32_t type, std::vector<uint8_t>& outData);
            // 删到行 true；无行 false。
            bool DeleteMm1UserDataBlob(const std::vector<uint8_t>& userId, int32_t type);
            
            // =============================================================
            // 消息操作
            // =============================================================
            
            // 存储消息：`sessionId` 须 **`USER_ID_SIZE`（16）** 字节（与 JNI `imSessionId` 约定一致）。
            // **`senderUserId`**：本条消息**发送者**（16B），保存在 **进程内 IM 行**（与历史 SQLite **`im_messages.sender_user_id`** 语义一致），供 MM1 **编辑/撤回**与 `senderId` 做 **`ConstantTimeCompare`**。
            // 明文经 **AES-256-GCM**（`mm2::Crypto`：**OpenSSL 3**）后密文 **仅驻留 RAM**（nonce‖ciphertext‖tag）；**不落** `im_messages` / `.zdb`。**`Initialize`** 仅 **`ImRamClearUnlocked()`** 清空**本进程** IM RAM（**不**扫元库/`.zdb` 删历史 IM）。`outMessageId` 为 16 字节随机 id。
            // 单条明文上限 **`ZDB_MAX_WRITE_SIZE - NONCE_SIZE - AUTH_TAG_SIZE`**。失败时查 **`LastError()`**。
            bool StoreMessage(
                const std::vector<uint8_t>& sessionId,
                const std::vector<uint8_t>& senderUserId,
                const std::vector<uint8_t>& payload,
                std::vector<uint8_t>&       outMessageId);
            
            // 检索消息：`messageId` 须 16 字节；须存在于 **进程内 IM 存储**。失败时 **`outPayload` 清空**。
            bool RetrieveMessage(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& outPayload);
            
            // 删除消息：须存在于 **进程内 IM 存储**（**不**触碰 **StoreFileChunk** 所用 **`data_blocks`**）；并清除内存中的 **回复关系**。
            bool DeleteMessage(const std::vector<uint8_t>& messageId);

            // =============================================================
            // 消息缓存/已读状态
            // =============================================================
            // 将消息标为已读：**进程内 RAM** 首次写入 **`readTimestampMs`**（Unix 毫秒）；已读则 **true**（幂等）。
            bool MarkMessageRead(const std::vector<uint8_t>& messageId, uint64_t readTimestampMs);

            // 会话内未读（**`read_at_ms IS NULL`**）的 **`message_id`**，按插入顺序 **升序**，最多 **`limit`** 条。**`limit==0`** 成功且空。配对第二元：**未读占位为 0**（与 JNI **`GetUnreadSessionMessageIds`** 仅 id 对齐；**不**触达加解密）。
            bool GetUnreadSessionMessages(
                const std::vector<uint8_t>& sessionId,
                size_t limit,
                std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outUnreadMessages);

            // =============================================================
            // 消息回复关系
            // =============================================================
            // StoreMessageReplyRelation：存 **ImRam** 回复映射。须 **`messageId` / `repliedMsgId` 同属一会话**；
            // **`repliedSenderId`** 须与 **`repliedMsgId`** 在 RAM 中的 **`senderUserId`** 一致（**`ConstantTimeCompare`**）。
            // 若 **`imSessionId`** 在 **`group_members`** 中有行（视为群会话），则 **SQL** 校验 **回复作者** 与 **`repliedSenderId`** 均为该 **`group_id`** 的成员。
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
            
            // 批量存储消息：顺序调用 **`StoreMessage`**；**`sessionId` / `senderUserId`** 须各 **`USER_ID_SIZE`**。任一条失败返回 **`false`**，并对本批**已成功**写入的条目**内部回滚**（与 **`DeleteMessage`** 同等清理），**`outMessageIds` 清空**（**全有或全无**；**`LastError`** 保留**首条失败原因**）。
            bool StoreMessages(
                const std::vector<uint8_t>&              sessionId,
                const std::vector<uint8_t>&              senderUserId,
                const std::vector<std::vector<uint8_t>>& payloads,
                std::vector<std::vector<uint8_t>>&       outMessageIds);

            // 读取 **进程内**保存的 **`senderUserId`**（须 16B）；用于 MM1 安全校验。
            bool GetMessageSenderUserId(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& outSenderUserId);
            
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

            // MM1 好友列表 / 删好友：基于 friend_requests status=1 的边（无单独 friends 表）。
            bool GetFriendRequestRowForMm1(
                const std::vector<uint8_t>& requestId,
                std::vector<uint8_t>&       outFromUser,
                std::vector<uint8_t>&       outToUser,
                int32_t&                    outStatus);
            bool ListAcceptedFriendUserIdsForMm1(
                const std::vector<uint8_t>& userId,
                std::vector<std::vector<uint8_t>>& outFriends);
            bool DeleteAcceptedFriendshipBetweenForMm1(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId);

            // =============================================================
            // 群组：`group_members` + `mm2_group_display`（**`GroupManager`** 建群/成员；**`GroupNameManager`** 改名）
            // =============================================================
            bool CreateGroupSeedForMm1(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& creatorId,
                const std::string&          nameUtf8,
                uint64_t                    nowSeconds);
            bool UpsertGroupMemberForMm1(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                int32_t                     role,
                int64_t                     joinedAtSeconds);
            bool DeleteGroupMemberForMm1(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);
            bool ListGroupMemberUserIdsForMm1(
                const std::vector<uint8_t>& groupId,
                std::vector<std::vector<uint8_t>>& outUserIds);
            bool GetGroupMemberRoleForMm1(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                int32_t&                    outRole,
                int64_t&                    outJoinedAt);
            bool GetGroupMemberExistsForMm1(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                bool&                       outExists);

            // 群密钥信封 **`ZGK1`**：`group_id` **复用为** **`data_blocks`** 的 **`data_id`（16B）chunk 0**（与随机 **`message_id`** collision 概率可忽略；语义上专指群密钥块）。
            // 磁盘布局：**`ZGK1`(4) ‖ `epoch_s` uint64 BE(8) ‖ `CRYPTO_KEY_SIZE` 随机(32)**，共 **44** 字节；**`group_data`** 指向该块（**`03-Storage.md`**）。
            bool UpsertGroupKeyEnvelopeForMm1(const std::vector<uint8_t>& groupId, uint64_t epochSeconds);
            // **禁止经 JNI 裸暴露**：读出 **含 32B 密钥** 的 **`ZGK1`**；仅 **native 自测** 或未来 **MM1 鉴权后的受控路径**。
            bool TryGetGroupKeyEnvelopeForMm1(const std::vector<uint8_t>& groupId, std::vector<uint8_t>& outEnvelope);

            // **禁止 JNI / 产品调用**：仅供 **`ZChatIM --test`** 注入 **`friend_requests` accepted** 单边（**无**真实验签语义）。
            bool SeedAcceptedFriendshipForSelfTest(
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t                    nowSeconds);

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
            // 群禁言（**`mm2_group_mute`**；**MM1 `GroupMuteManager`**）
            // =============================================================
            bool UpsertGroupMuteForMm1(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                int64_t                     startMs,
                int64_t                     durationSeconds,
                const std::vector<uint8_t>& mutedBy,
                const std::vector<uint8_t>& reason);
            bool DeleteGroupMuteForMm1(const std::vector<uint8_t>& groupId, const std::vector<uint8_t>& userId);
            bool GetGroupMuteRowForMm1(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                bool&                       outExists,
                int64_t&                    outStartMs,
                int64_t&                    outDurationS,
                std::vector<uint8_t>&       outMutedBy,
                std::vector<uint8_t>&       outReason);
            bool DeleteExpiredGroupMutesForMm1(int64_t nowMs);

            // =============================================================
            // MM1 进程态持久化（**`SqliteMetadataDb` `user_version=11`**：`mm1_device_sessions` / **`mm1_user_status`** / **`mm1_mention_atall_window`** 等）
            // 仅 **`Initialize` 成功**后有效；否则 **`Mm1RegisterDeviceSession` 等**返回 **false** / 空。
            // **`CleanupAllData`** 删元库文件后自然清空；**`Mm1ClearAll*`** 供紧急擦除前仍可打开库时兜底。
            // =============================================================
            bool Mm1RegisterDeviceSession(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& deviceId,
                const std::vector<uint8_t>& sessionId,
                uint64_t                    loginTimeMs,
                uint64_t                    lastActiveMs,
                std::vector<uint8_t>&       outKickedSessionId);
            bool Mm1UpdateDeviceSessionLastActive(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& sessionId,
                uint64_t                    lastActiveMs);
            bool Mm1ListDeviceSessions(
                const std::vector<uint8_t>& userId,
                std::vector<std::vector<uint8_t>>& outSessionIds,
                std::vector<std::vector<uint8_t>>& outDeviceIds,
                std::vector<uint64_t>&             outLoginTimeMs,
                std::vector<uint64_t>&             outLastActiveMs);
            bool Mm1CleanupExpiredDeviceSessions(uint64_t nowMs, uint64_t idleTimeoutMs);
            bool Mm1ClearAllDeviceSessions();

            bool Mm1TouchImSessionActivity(const std::vector<uint8_t>& imSessionId, uint64_t lastActiveMs);
            bool Mm1SelectImSessionLastActive(const std::vector<uint8_t>& imSessionId, uint64_t& outLastActiveMs, bool& outFound);
            bool Mm1CleanupExpiredImSessionActivity(uint64_t nowMs, uint64_t idleTimeoutMs);
            bool Mm1ClearAllImSessionActivity();

            bool Mm1CertPinningConfigure(const std::vector<uint8_t>& currentSpkiSha256, const std::vector<uint8_t>& standbySpkiSha256);
            bool Mm1CertPinningVerify(const std::vector<uint8_t>& presentedSpkiSha256);
            bool Mm1CertPinningIsBanned(const std::vector<uint8_t>& clientId);
            bool Mm1CertPinningRecordFailure(const std::vector<uint8_t>& clientId);
            bool Mm1CertPinningClearBan(const std::vector<uint8_t>& clientId);
            bool Mm1CertPinningResetAll();

            // --- MM1 `UserStatusManager`（**mm1_user_status**；**最后已知**在线，**服务端**权威）---
            bool Mm1UpsertUserStatus(const std::vector<uint8_t>& userId, bool online, uint64_t updatedMs);
            bool Mm1GetUserStatus(const std::vector<uint8_t>& userId, bool& outOnline, bool& outFound);
            bool Mm1ClearAllUserStatus();

            // --- MM1 `MentionPermissionManager` @ALL 限速（**mm1_mention_atall_window**）---
            bool Mm1MentionAtAllLoadTimes(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                std::vector<uint64_t>& outTimesMs);
            bool Mm1MentionAtAllStoreTimes(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint64_t>& timesMs);
            bool Mm1ClearAllMentionAtAllWindows();
            
            // =============================================================
            // 会话管理
            // =============================================================
            
            // 获取会话消息：**`limit==0`** 成功且空（不触达密钥就绪）；否则取该会话下**最近** **`limit`** 条（**内存插入序**），**`outMessages`** 为 **`(message_id, 明文 payload)`**，顺序为**插入顺序正序**。任一条解密失败则**清空**并 **`false`**。须 **`Initialize`** 且能完成消息密钥就绪。
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

            // 已持有 m_stateMutex；`indexDir/mm2_message_key.bin`（32 字节 AES-256 密钥）。**`optionalMessageKeyPassphraseUtf8`**：见 **`Initialize(..., passphrase)`** 注释。
            bool LoadOrCreateMessageStorageKeyUnlocked(const char* optionalMessageKeyPassphraseUtf8);
            bool InitializeImplUnlocked(const std::string& dataDir, const std::string& indexDir, const char* optionalMessageKeyPassphraseUtf8);

            // 已持有 m_stateMutex；首次 StoreMessage/RetrieveMessage 前调用：Crypto::Init + 加载/生成消息密钥
            bool EnsureMessageCryptoReadyUnlocked();
            // 已持有 m_stateMutex；`dataId16` 须为 MESSAGE_ID_SIZE 字节
            bool PutDataBlockBlobUnlocked(const std::vector<uint8_t>& dataId16, int32_t chunkIdx, const std::vector<uint8_t>& blob);
            bool GetDataBlockBlobUnlocked(const std::vector<uint8_t>& dataId16, int32_t chunkIdx, std::vector<uint8_t>& outBlob);
            // 已持有 m_stateMutex；写 **`ZGK1`** 信封并 **`UpsertGroupData`**；失败时尽力回滚 **`data_blocks` 写入**。
            bool WriteGroupKeyEnvelopeUnlocked(const std::vector<uint8_t>& groupId, uint64_t epochSeconds);

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

            struct BytesVecLess {
                bool operator()(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) const
                {
                    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
                }
            };
            struct ImRamMessageRow {
                std::vector<uint8_t> messageId;
                std::vector<uint8_t> senderUserId;
                std::vector<uint8_t> blob;
                int64_t              stored_at_ms{};
                bool                 has_read{};
                int64_t              read_at_ms{};
                uint32_t             edit_count{};
                uint64_t             last_edit_time_s{};
            };
            struct ImRamReplyRow {
                std::vector<uint8_t> repliedMsgId;
                std::vector<uint8_t> repliedSenderId;
                std::vector<uint8_t> repliedDigest;
            };

            bool m_initialized;                       // 初始化状态
            ZdbManager m_zdbManager;                  // ZDB文件管理器
            SqliteMetadataDb m_metadataDb;            // 元数据索引（zdb_files / data_blocks；文件块索引亦在此，无独立 BlockIndex）
            // 状态互斥（递归）：所有 public 实例方法实现 MUST 在入口持锁；MessageQueryManager 仅在此锁保护下调用。
            mutable std::recursive_mutex m_stateMutex;
            
            // 会话消息缓存
            std::map<std::string, std::vector<std::pair<std::vector<uint8_t>, uint64_t>>> m_sessionCache;

            // IM 消息：**仅内存**（密文 blob = nonce‖ciphertext‖tag，与历史 `.zdb` chunk0 格式一致）
            std::map<std::vector<uint8_t>, std::vector<ImRamMessageRow>, BytesVecLess> m_imRamBySession{};
            std::map<std::vector<uint8_t>, std::vector<uint8_t>, BytesVecLess>           m_imRamMsgToSession{};
            std::map<std::vector<uint8_t>, ImRamReplyRow, BytesVecLess>                 m_imRamReplies{};

            // 存储完整性校验管理器（记录/比对 sha256 到 SQLite）
            StorageIntegrityManager m_storageIntegrityManager;

            // 消息列表与同步查询（与 IM RAM / 元库生命周期一致）
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

            // 已持 **`m_stateMutex`**：**`StoreMessages` 回滚**等仅删 **RAM IM**（**不**动磁盘 **`data_blocks`**）。
            bool DeleteMessageImplUnlocked(const std::vector<uint8_t>& messageId);
            void ImRamClearUnlocked();
            void ImRamClearSessionUnlocked(const std::vector<uint8_t>& sessionId);
            bool ImRamInsertRowUnlocked(const std::vector<uint8_t>& sessionId, ImRamMessageRow row);
            bool ImRamEraseUnlocked(const std::vector<uint8_t>& messageId);
            bool ImRamExistsUnlocked(const std::vector<uint8_t>& messageId);
            bool ImRamLocateUnlocked(const std::vector<uint8_t>& messageId, ImRamMessageRow** outRow);
            bool ImRamListIdsLastNUnlocked(
                const std::vector<uint8_t>& sessionId,
                size_t                      limit,
                std::vector<std::vector<uint8_t>>& outIds);
            bool ImRamListIdsChronologicalFirstNUnlocked(
                const std::vector<uint8_t>& sessionId,
                size_t                      limit,
                std::vector<std::vector<uint8_t>>& outIds);
            bool ImRamListIdsSinceStoredAtUnlocked(
                const std::vector<uint8_t>& sessionId,
                int64_t                     sinceStoredAtMsInclusive,
                size_t                      limit,
                std::vector<std::vector<uint8_t>>& outIds);
            bool ImRamListIdsAfterMessageIdUnlocked(
                const std::vector<uint8_t>& sessionId,
                const std::vector<uint8_t>& afterMessageId,
                size_t                      limit,
                std::vector<std::vector<uint8_t>>& outIds);
            // 删除某逻辑 **`fileId`** 下 **连续** chunk 索引 **0..N-1**（遇首个缺失则停）；用于 **`CancelFile` / `CompleteFile` 失败回滚**等。
            bool EraseAllChunksForLogicalFileUnlocked(const std::string& logicalFileId);
        };
        
    } // namespace mm2
} // namespace ZChatIM
