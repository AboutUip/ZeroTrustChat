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
        // IM: RAM only (AES-GCM). Files/group keys: .zdb + data_blocks. Public methods hold m_stateMutex; List* same lock;
        // GetStorageIntegrityManager() not locked — serialize (JniSecurityPolicy).
        class MM2 {
            friend class MessageQueryManager;

        public:
            MM2();
            ~MM2();

            // dataDir/.zdb, indexDir/metadata.db + mm2_message_key.bin. SQLCipher default: Crypto+key in Initialize.
            // ZMK1/2/3/ZMKP: see 03-Storage.md, MM2.cpp. nullptr passphrase == two-arg Initialize.
            bool Initialize(const std::string& dataDir, const std::string& indexDir);
            bool Initialize(const std::string& dataDir, const std::string& indexDir, const char* messageKeyPassphraseUtf8);

            void Cleanup();

            bool IsInitialized() const;

            std::string GetDataDirUtf8() const;
            std::string GetIndexDirUtf8() const;

            // mm1_user_kv（SQLCipher）；Initialize 之后；data <= 16MiB。type 含 LPH1/LRC1/AVT1 等，无白名单。
            bool StoreMm1UserDataBlob(const std::vector<uint8_t>& userId, int32_t type, const std::vector<uint8_t>& data);
            bool GetMm1UserDataBlob(const std::vector<uint8_t>& userId, int32_t type, std::vector<uint8_t>& outData);
            bool DeleteMm1UserDataBlob(const std::vector<uint8_t>& userId, int32_t type);

            // sessionId/senderUserId 16B; AES-GCM to ImRam only; max payload ZDB_MAX_WRITE_SIZE - nonce - tag.
            bool StoreMessage(
                const std::vector<uint8_t>& sessionId,
                const std::vector<uint8_t>& senderUserId,
                const std::vector<uint8_t>& payload,
                std::vector<uint8_t>&       outMessageId);
            
            bool RetrieveMessage(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& outPayload);

            // ImRam only; clears reply map; does not touch file data_blocks.
            bool DeleteMessage(const std::vector<uint8_t>& messageId);

            bool MarkMessageRead(const std::vector<uint8_t>& messageId, uint64_t readTimestampMs);

            // Unread ids ascending; pair.second placeholder 0 for JNI.
            bool GetUnreadSessionMessages(
                const std::vector<uint8_t>& sessionId,
                size_t limit,
                std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outUnreadMessages);

            // ImRam reply map; same session; repliedSenderId matches RAM sender; group: SQL member check.
            bool StoreMessageReplyRelation(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& repliedMsgId,
                const std::vector<uint8_t>& repliedSenderId,
                const std::vector<uint8_t>& repliedContentDigest);

            bool GetMessageReplyRelation(
                const std::vector<uint8_t>& messageId,
                std::vector<uint8_t>& outRepliedMsgId,
                std::vector<uint8_t>& outRepliedSenderId,
                std::vector<uint8_t>& outRepliedContentDigest);

            // Plaintext-in name legacy; GCM to row->blob. MM1: policy/signature.
            bool EditMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                uint32_t newEditCount);

            bool GetMessageEditState(
                const std::vector<uint8_t>& messageId,
                uint32_t& outEditCount,
                uint64_t& outLastEditTimeSeconds);
            
            // All-or-nothing batch StoreMessage; LastError = first failure.
            bool StoreMessages(
                const std::vector<uint8_t>&              sessionId,
                const std::vector<uint8_t>&              senderUserId,
                const std::vector<std::vector<uint8_t>>& payloads,
                std::vector<std::vector<uint8_t>>&       outMessageIds);

            bool GetMessageSenderUserId(const std::vector<uint8_t>& messageId, std::vector<uint8_t>& outSenderUserId);

            bool RetrieveMessages(const std::vector<std::vector<uint8_t>>& messageIds, std::vector<std::vector<uint8_t>>& outPayloads);

            // data_id = first16(SHA256(fileId||chunk LE32)); SIM hash chain (03-Storage 第七节).
            bool StoreFileChunk(const std::string& fileId, uint32_t chunkIndex, const std::vector<uint8_t>& data);

            bool GetFileChunk(const std::string& fileId, uint32_t chunkIndex, std::vector<uint8_t>& outData);

            bool CompleteFile(const std::string& fileId, const uint8_t* sha256);

            bool CancelFile(const std::string& fileId);

            // In-memory resume index only.
            bool StoreTransferResumeChunkIndex(const std::string& fileId, uint32_t chunkIndex);

            bool GetTransferResumeChunkIndex(const std::string& fileId, uint32_t& outChunkIndex);

            bool CleanupTransferResumeChunkIndex(const std::string& fileId);

            bool StoreFriendRequest(
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519,
                std::vector<uint8_t>& outRequestId);

            bool FindPendingOutgoingFriendRequestId(
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                std::vector<uint8_t>&       outRequestId);

            bool UpdateFriendRequestStatus(
                const std::vector<uint8_t>& requestId,
                bool accept,
                const std::vector<uint8_t>& responderId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            bool DeleteFriendRequest(const std::vector<uint8_t>& requestId);

            bool CleanupExpiredFriendRequests(uint64_t nowMs);

            bool GetFriendRequestRowForMm1(
                const std::vector<uint8_t>& requestId,
                std::vector<uint8_t>&       outFromUser,
                std::vector<uint8_t>&       outToUser,
                int32_t&                    outStatus);
            bool ListPendingFriendRequestsForMm1(
                const std::vector<uint8_t>&        userId,
                std::vector<std::vector<uint8_t>>& outRows);
            bool ListAcceptedFriendUserIdsForMm1(
                const std::vector<uint8_t>& userId,
                std::vector<std::vector<uint8_t>>& outFriends);
            bool DeleteAcceptedFriendshipBetweenForMm1(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId);

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

            // ZGK1 envelope on data_blocks chunk0; layout 03-Storage 2.4.
            bool UpsertGroupKeyEnvelopeForMm1(const std::vector<uint8_t>& groupId, uint64_t epochSeconds);
            // Not for JNI; test/MM1-only.
            bool TryGetGroupKeyEnvelopeForMm1(const std::vector<uint8_t>& groupId, std::vector<uint8_t>& outEnvelope);

            // Tests only; fake accepted edge.
            bool SeedAcceptedFriendshipForSelfTest(
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t                    nowSeconds);

            bool UpdateGroupName(
                const std::vector<uint8_t>& groupId,
                const std::string& newGroupName,
                uint64_t updateTimeSeconds,
                const std::vector<uint8_t>& updateBy);

            bool GetGroupName(
                const std::vector<uint8_t>& groupId,
                std::string& outGroupName);

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

            // Device/status/@ALL window tables; need Initialize.
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

            bool Mm1UpsertUserStatus(const std::vector<uint8_t>& userId, bool online, uint64_t updatedMs);
            bool Mm1GetUserStatus(const std::vector<uint8_t>& userId, bool& outOnline, bool& outFound);
            bool Mm1ClearAllUserStatus();

            bool Mm1MentionAtAllLoadTimes(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                std::vector<uint64_t>& outTimesMs);
            bool Mm1MentionAtAllStoreTimes(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint64_t>& timesMs);
            bool Mm1ClearAllMentionAtAllWindows();
            
            // limit 0 => empty ok; else recent N plaintext pairs; any decrypt fail => false, clear out.
            bool GetSessionMessages(const std::vector<uint8_t>& sessionId, size_t limit, std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& outMessages);

            bool CleanupSessionMessages(const std::vector<uint8_t>& sessionId);

            bool CleanupExpiredData();

            bool CleanupAllData();

            bool OptimizeStorage();

            bool GetStorageStatus(size_t& totalSpace, size_t& usedSpace, size_t& availableSpace);

            size_t GetFileCount();

            size_t GetMessageCount();

            std::string LastError() const;

            // dataId: 16 raw bytes in string (MESSAGE_ID_SIZE).
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

            StorageIntegrityManager& GetStorageIntegrityManager();

            MessageQueryManager& GetMessageQueryManager();

            static MM2& Instance();

            static uint64_t GetNextSequence();

        private:
            std::vector<uint8_t> GenerateMessageId();

            std::string GenerateFileId();

            bool IsMessageExpired(uint64_t timestamp);

            bool IsFileExpired(uint64_t timestamp);

            bool DestroyData(const std::string& fileId, uint64_t offset, size_t length);

            void CleanupUnlocked();

            bool LoadOrCreateMessageStorageKeyUnlocked(const char* optionalMessageKeyPassphraseUtf8);
            bool InitializeImplUnlocked(const std::string& dataDir, const std::string& indexDir, const char* optionalMessageKeyPassphraseUtf8);

            bool EnsureMessageCryptoReadyUnlocked();
            bool PutDataBlockBlobUnlocked(const std::vector<uint8_t>& dataId16, int32_t chunkIdx, const std::vector<uint8_t>& blob);
            bool GetDataBlockBlobUnlocked(const std::vector<uint8_t>& dataId16, int32_t chunkIdx, std::vector<uint8_t>& outBlob);
            bool WriteGroupKeyEnvelopeUnlocked(const std::vector<uint8_t>& groupId, uint64_t epochSeconds);

            bool RevertFailedPutDataBlockUnlocked(
                const std::vector<uint8_t>& dataId16,
                int32_t                     chunkIdx,
                const std::string&          zdbFileId,
                uint64_t                    offset,
                size_t                      blobLen);
            
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

            bool m_initialized;
            ZdbManager m_zdbManager;
            SqliteMetadataDb m_metadataDb;
            mutable std::recursive_mutex m_stateMutex;

            std::map<std::string, std::vector<std::pair<std::vector<uint8_t>, uint64_t>>> m_sessionCache;

            std::map<std::vector<uint8_t>, std::vector<ImRamMessageRow>, BytesVecLess> m_imRamBySession{};
            std::map<std::vector<uint8_t>, std::vector<uint8_t>, BytesVecLess>           m_imRamMsgToSession{};
            std::map<std::vector<uint8_t>, ImRamReplyRow, BytesVecLess>                 m_imRamReplies{};

            StorageIntegrityManager m_storageIntegrityManager;

            MessageQueryManager m_messageQueryManager;

            std::string              m_dataDir;
            std::string              m_indexDir;
            std::filesystem::path    m_metadataDbPath;
            mutable std::string      m_lastError;

            std::vector<uint8_t>     m_messageStorageKey;

            void SetLastError(std::string_view message) const;

            std::vector<std::vector<uint8_t>> InternalListMessagesForQueryManager(const std::vector<uint8_t>& sessionId, int count);
            std::vector<std::vector<uint8_t>> InternalListMessagesSinceMessageIdForQueryManager(
                const std::vector<uint8_t>& sessionId,
                const std::vector<uint8_t>& lastMsgId,
                int                         count);
            std::vector<std::vector<uint8_t>> InternalListMessagesSinceTimestampForQueryManager(
                const std::vector<uint8_t>& sessionId,
                uint64_t                    sinceTimestampMs,
                int                         count);

            static std::atomic<uint64_t> s_sequence;

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
            bool EraseAllChunksForLogicalFileUnlocked(const std::string& logicalFileId);
        };
        
    } // namespace mm2
} // namespace ZChatIM
