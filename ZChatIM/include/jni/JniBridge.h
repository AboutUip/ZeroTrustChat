#pragma once

#include "../Types.h"
#include "../common/JniSecurityPolicy.h"
#include "../mm1/MM1.h"
#include "../mm2/MM2.h"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ZChatIM
{
	namespace jni
	{
		// JNI singleton; API order matches JniInterface. Contract: docs/06-Appendix/01-JNI.md.
		class JniBridge {
		public:
			static JniBridge& Instance();

			bool Initialize(const std::string& dataDir, const std::string& indexDir);
			// Non-empty C string -> MM2 3-arg (ZMKP; SQLCipher). nullptr/empty == 2-arg.
			bool Initialize(
				const std::string& dataDir,
				const std::string& indexDir,
				const char* messageKeyPassphraseUtf8);
			/** 最近一次 Initialize / InitializeWithPassphrase 失败时的说明（MM1/MM2）；成功或未调用时为空。 */
			std::string LastInitializeError() const;
			void Cleanup();

			// Idempotent; m_initialized=false release only; no m_apiRecursiveMutex here (EmergencyWipe).
			void NotifyExternalTrustedZoneWipeHandled();

			std::vector<uint8_t> Auth(
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& token,
				const std::vector<uint8_t>& clientIp = {});
			bool VerifySession(const std::vector<uint8_t>& sessionId);
			bool DestroySession(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& sessionIdToDestroy);

			bool RegisterLocalUser(
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& passwordUtf8,
				const std::vector<uint8_t>& recoverySecretUtf8);

			std::vector<uint8_t> AuthWithLocalPassword(
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& passwordUtf8,
				const std::vector<uint8_t>& clientIp = {});

			bool HasLocalPassword(const std::vector<uint8_t>& userId);

			bool ChangeLocalPassword(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& oldPasswordUtf8,
				const std::vector<uint8_t>& newPasswordUtf8);

			bool ResetLocalPasswordWithRecovery(
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& recoverySecretUtf8,
				const std::vector<uint8_t>& newPasswordUtf8,
				const std::vector<uint8_t>& clientIp = {});

			std::vector<uint8_t> RtcStartCall(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& peerUserId,
				int32_t callKind);

			bool RtcAcceptCall(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& callId);

			bool RtcRejectCall(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& callId);

			bool RtcEndCall(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& callId);

			int32_t RtcGetCallState(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& callId);

			int32_t RtcGetCallKind(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& callId);

			std::vector<uint8_t> StoreMessage(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& imSessionId,
				const std::vector<uint8_t>& payload);

			std::vector<uint8_t> RetrieveMessage(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& messageId);

			bool DeleteMessage(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& messageId,
				const std::vector<uint8_t>& senderId,
				const std::vector<uint8_t>& signatureEd25519);

			bool RecallMessage(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& messageId,
				const std::vector<uint8_t>& senderId,
				const std::vector<uint8_t>& signatureEd25519);

			std::vector<std::vector<uint8_t>> ListMessages(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				int count);

			std::vector<std::vector<uint8_t>> ListMessagesSinceTimestamp(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				uint64_t sinceTimestampMs,
				int count);

			std::vector<std::vector<uint8_t>> ListMessagesSinceMessageId(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& lastMsgId,
				int count);

			bool MarkMessageRead(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& messageId,
				uint64_t readTimestampMs);

			std::vector<std::vector<uint8_t>> GetUnreadSessionMessageIds(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& imSessionId,
				int limit);

			bool StoreMessageReplyRelation(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& senderEd25519PublicKey,
				const std::vector<uint8_t>& messageId,
				const std::vector<uint8_t>& repliedMsgId,
				const std::vector<uint8_t>& repliedSenderId,
				const std::vector<uint8_t>& repliedContentDigest,
				const std::vector<uint8_t>& senderId,
				const std::vector<uint8_t>& signatureEd25519);

			std::vector<std::vector<uint8_t>> GetMessageReplyRelation(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& messageId);

			bool EditMessage(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& messageId,
				const std::vector<uint8_t>& newEncryptedContent,
				uint64_t editTimestampSeconds,
				const std::vector<uint8_t>& signature,
				const std::vector<uint8_t>& senderId);

			std::vector<uint8_t> GetMessageEditState(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& messageId);

			bool StoreUserData(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				int32_t type,
				const std::vector<uint8_t>& data);

			std::vector<uint8_t> GetUserData(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				int32_t type);

			bool DeleteUserData(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				int32_t type);

			std::vector<uint8_t> SendFriendRequest(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& fromUserId,
				const std::vector<uint8_t>& toUserId,
				uint64_t timestampSeconds,
				const std::vector<uint8_t>& signatureEd25519);

			bool RespondFriendRequest(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& requestId,
				bool accept,
				const std::vector<uint8_t>& responderId,
				uint64_t timestampSeconds,
				const std::vector<uint8_t>& signatureEd25519);

			bool DeleteFriend(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& friendId,
				uint64_t timestampSeconds,
				const std::vector<uint8_t>& signatureEd25519);

			std::vector<std::vector<uint8_t>> GetFriends(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId);

			std::vector<uint8_t> CreateGroup(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& creatorId,
				const std::string& name);

			bool InviteMember(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& userId);

			bool RemoveMember(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& userId);

			bool LeaveGroup(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& userId);

			std::vector<std::vector<uint8_t>> GetGroupMembers(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId);

			bool UpdateGroupKey(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId);

			bool ValidateMentionRequest(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& senderId,
				int32_t mentionType,
				const std::vector<std::vector<uint8_t>>& mentionedUserIds,
				uint64_t nowMs,
				const std::vector<uint8_t>& signatureEd25519);

			bool RecordMentionAtAllUsage(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& senderId,
				uint64_t nowMs);

			bool MuteMember(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& mutedBy,
				uint64_t startTimeMs,
				int64_t durationSeconds,
				const std::vector<uint8_t>& reason);

			bool IsMuted(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& userId,
				uint64_t nowMs);

			bool UnmuteMember(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& unmutedBy);

			bool UpdateGroupName(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId,
				const std::vector<uint8_t>& updaterId,
				const std::string& newGroupName,
				uint64_t nowMs);

			std::string GetGroupName(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& groupId);

			bool StoreFileChunk(
				const std::vector<uint8_t>& callerSessionId,
				const std::string& fileId,
				int chunkIndex,
				const std::vector<uint8_t>& data);

			std::vector<uint8_t> GetFileChunk(
				const std::vector<uint8_t>& callerSessionId,
				const std::string& fileId,
				int chunkIndex);

			bool CompleteFile(
				const std::vector<uint8_t>& callerSessionId,
				const std::string& fileId,
				const std::vector<uint8_t>& sha256);

			bool CancelFile(
				const std::vector<uint8_t>& callerSessionId,
				const std::string& fileId);

			bool StoreTransferResumeChunkIndex(
				const std::vector<uint8_t>& callerSessionId,
				const std::string& fileId,
				uint32_t chunkIndex);

			uint32_t GetTransferResumeChunkIndex(
				const std::vector<uint8_t>& callerSessionId,
				const std::string& fileId);

			bool CleanupTransferResumeChunkIndex(
				const std::vector<uint8_t>& callerSessionId,
				const std::string& fileId);

			std::vector<std::vector<uint8_t>> GetSessionMessages(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& imSessionId,
				int limit);

			bool GetSessionStatus(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& imSessionId);

			void TouchSession(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& imSessionId,
				uint64_t nowMs);

			void CleanupExpiredSessions(
				const std::vector<uint8_t>& callerSessionId,
				uint64_t nowMs);

			// true + empty out = ok; true + 16B = kicked session; false = error (JNI null).
			bool RegisterDeviceSession(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& deviceId,
				const std::vector<uint8_t>& sessionId,
				uint64_t                    loginTimeMs,
				uint64_t                    lastActiveMs,
				std::vector<uint8_t>& outKickedSessionId);

			bool UpdateLastActive(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& sessionId,
				uint64_t nowMs);

			std::vector<std::vector<uint8_t>> GetDeviceSessions(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId);

			void CleanupExpiredDeviceSessions(
				const std::vector<uint8_t>& callerSessionId,
				uint64_t nowMs);

			bool GetUserStatus(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId);

			bool CleanupSessionMessages(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& imSessionId);

			bool CleanupExpiredData(const std::vector<uint8_t>& callerSessionId);
			bool OptimizeStorage(const std::vector<uint8_t>& callerSessionId);

			std::map<std::string, std::string> GetStorageStatus(const std::vector<uint8_t>& callerSessionId);
			int64_t GetMessageCount(const std::vector<uint8_t>& callerSessionId);
			int64_t GetFileCount(const std::vector<uint8_t>& callerSessionId);

			std::vector<uint8_t> GenerateMasterKey(const std::vector<uint8_t>& callerSessionId);
			std::vector<uint8_t> RefreshSessionKey(const std::vector<uint8_t>& callerSessionId);
			void EmergencyWipe(const std::vector<uint8_t>& callerSessionId);
			std::map<std::string, std::string> GetStatus(const std::vector<uint8_t>& callerSessionId);
			bool RotateKeys(const std::vector<uint8_t>& callerSessionId);

			// TLS pin; see JniSecurityPolicy.
			void ConfigurePinnedPublicKeyHashes(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& currentSpkiSha256,
				const std::vector<uint8_t>& standbySpkiSha256);

			bool VerifyPinnedServerCertificate(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& clientId,
				const std::vector<uint8_t>& presentedSpkiSha256);

			bool IsClientBanned(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& clientId);

			void RecordFailure(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& clientId);

			void ClearBan(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& clientId);

			bool DeleteAccount(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& reauthToken,
				const std::vector<uint8_t>& secondConfirmToken);

			bool IsAccountDeleted(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId);

			bool UpdateFriendNote(
				const std::vector<uint8_t>& callerSessionId,
				const std::vector<uint8_t>& userId,
				const std::vector<uint8_t>& friendId,
				const std::vector<uint8_t>& newEncryptedNote,
				uint64_t updateTimestampSeconds,
				const std::vector<uint8_t>& signatureEd25519);

			bool ValidateJniCall();
			bool ValidateJniCall(const void* jniEnv, const void* jcls);

		private:
			JniBridge();
			~JniBridge();

			JniBridge(const JniBridge&) = delete;
			JniBridge& operator=(const JniBridge&) = delete;

			bool CheckInitialized();
			void LogOperation(const std::string& operation, bool success);

			// CheckInitialized without bridge lock; use release/acquire with Init/Cleanup/Wipe.
			std::atomic<bool> m_initialized{ false };
			mutable std::recursive_mutex m_apiRecursiveMutex;
			std::string m_lastInitializeError;

			mm1::MM1& m_mm1;
			mm2::MM2& m_mm2;
		};

	} // namespace jni
} // namespace ZChatIM
