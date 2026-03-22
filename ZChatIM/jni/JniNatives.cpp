// JNI ↔ JniInterface：与 com.yhj.zchat.jni.ZChatIMNative 方法名、签名一致（RegisterNatives）。
#include <jni.h>

#include "jni/JniInterface.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

    std::vector<uint8_t> JBytes(JNIEnv* env, jbyteArray arr)
    {
        if (!arr) {
            return {};
        }
        const jsize n = env->GetArrayLength(arr);
        if (n <= 0) {
            return {};
        }
        std::vector<uint8_t> out(static_cast<size_t>(n));
        env->GetByteArrayRegion(arr, 0, n, reinterpret_cast<jbyte*>(out.data()));
        return out;
    }

    jbyteArray ToJBytesEmptyOk(JNIEnv* env, const std::vector<uint8_t>& v)
    {
        const jsize n = static_cast<jsize>(v.size());
        jbyteArray a    = env->NewByteArray(n);
        if (n > 0) {
            env->SetByteArrayRegion(a, 0, n, reinterpret_cast<const jbyte*>(v.data()));
        }
        return a;
    }

    /** C++ 契约：空 vector = null → JNI null */
    jbyteArray ToJBytesOrNull(JNIEnv* env, const std::vector<uint8_t>& v)
    {
        if (v.empty()) {
            return nullptr;
        }
        return ToJBytesEmptyOk(env, v);
    }

    std::string JString(JNIEnv* env, jstring s)
    {
        if (!s) {
            return {};
        }
        const char* u = env->GetStringUTFChars(s, nullptr);
        std::string out(u ? u : "");
        env->ReleaseStringUTFChars(s, u);
        return out;
    }

    jstring ToJString(JNIEnv* env, const std::string& s)
    {
        return env->NewStringUTF(s.c_str());
    }

    jobject NewStringStringMap(JNIEnv* env, const std::map<std::string, std::string>& m)
    {
        jclass    mapCls  = env->FindClass("java/util/HashMap");
        jmethodID ctor    = env->GetMethodID(mapCls, "<init>", "()V");
        jmethodID put     = env->GetMethodID(mapCls, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
        jobject   mapObj  = env->NewObject(mapCls, ctor);
        for (const auto& pr : m) {
            jstring k = env->NewStringUTF(pr.first.c_str());
            jstring v = env->NewStringUTF(pr.second.c_str());
            env->CallObjectMethod(mapObj, put, k, v);
            env->DeleteLocalRef(k);
            env->DeleteLocalRef(v);
        }
        env->DeleteLocalRef(mapCls);
        return mapObj;
    }

    jclass g_byteArrayClass = nullptr;

    bool EnsureByteArrayClass(JNIEnv* env)
    {
        if (g_byteArrayClass) {
            return true;
        }
        jclass local = env->FindClass("[B");
        if (!local) {
            return false;
        }
        g_byteArrayClass = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        return g_byteArrayClass != nullptr;
    }

    jobjectArray ToJObjectArrayOfByteArray(JNIEnv* env, const std::vector<std::vector<uint8_t>>& rows)
    {
        if (!EnsureByteArrayClass(env)) {
            return nullptr;
        }
        const jsize n = static_cast<jsize>(rows.size());
        jobjectArray out = env->NewObjectArray(n, g_byteArrayClass, nullptr);
        for (jsize i = 0; i < n; ++i) {
            jbyteArray row = ToJBytesEmptyOk(env, rows[static_cast<size_t>(i)]);
            env->SetObjectArrayElement(out, i, row);
            env->DeleteLocalRef(row);
        }
        return out;
    }

    std::vector<std::vector<uint8_t>> MentionIdsFromJObjectArray(JNIEnv* env, jobjectArray arr)
    {
        std::vector<std::vector<uint8_t>> out;
        if (!arr) {
            return out;
        }
        const jsize n = env->GetArrayLength(arr);
        for (jsize i = 0; i < n; ++i) {
            jobject el = env->GetObjectArrayElement(arr, i);
            if (!el) {
                out.push_back({});
                continue;
            }
            auto* ba = static_cast<jbyteArray>(el);
            out.push_back(JBytes(env, ba));
            env->DeleteLocalRef(el);
        }
        return out;
    }

} // namespace

#define ZCHAT_JNI static void JNICALL

extern "C" {

    JNIEXPORT jboolean JNICALL
    n_initialize(JNIEnv* env, jclass, jstring dataDir, jstring indexDir)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::Initialize(JString(env, dataDir), JString(env, indexDir)));
    }

    JNIEXPORT jboolean JNICALL
    n_initializeWithPassphrase(JNIEnv* env, jclass, jstring dataDir, jstring indexDir, jstring passphrase)
    {
        const std::string dd = JString(env, dataDir);
        const std::string id = JString(env, indexDir);
        const char*       p  = nullptr;
        if (passphrase != nullptr) {
            p = env->GetStringUTFChars(passphrase, nullptr);
            if (p == nullptr) {
                return JNI_FALSE;
            }
        }
        const bool ok = ZChatIM::jni::JniInterface::InitializeWithPassphrase(dd, id, p);
        if (p != nullptr) {
            env->ReleaseStringUTFChars(passphrase, p);
        }
        return static_cast<jboolean>(ok);
    }

    ZCHAT_JNI n_cleanup(JNIEnv* env, jclass)
    {
        (void)env;
        ZChatIM::jni::JniInterface::Cleanup();
    }

    JNIEXPORT jbyteArray JNICALL
    n_auth(JNIEnv* env, jclass, jbyteArray userId, jbyteArray token, jbyteArray clientIp)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::Auth(JBytes(env, userId), JBytes(env, token), JBytes(env, clientIp)));
    }

    JNIEXPORT jboolean JNICALL
    n_verifySession(JNIEnv* env, jclass, jbyteArray sessionId)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::VerifySession(JBytes(env, sessionId)));
    }

    JNIEXPORT jboolean JNICALL
    n_destroySession(JNIEnv* env, jclass, jbyteArray caller, jbyteArray target)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::DestroySession(JBytes(env, caller), JBytes(env, target)));
    }

    JNIEXPORT jboolean JNICALL
    n_registerLocalUser(JNIEnv* env, jclass, jbyteArray userId, jbyteArray pwd, jbyteArray recovery)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::RegisterLocalUser(
            JBytes(env, userId),
            JBytes(env, pwd),
            JBytes(env, recovery)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_authWithLocalPassword(JNIEnv* env, jclass, jbyteArray userId, jbyteArray pwd, jbyteArray clientIp)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::AuthWithLocalPassword(
                JBytes(env, userId),
                JBytes(env, pwd),
                JBytes(env, clientIp)));
    }

    JNIEXPORT jboolean JNICALL
    n_hasLocalPassword(JNIEnv* env, jclass, jbyteArray userId)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::HasLocalPassword(JBytes(env, userId)));
    }

    JNIEXPORT jboolean JNICALL
    n_changeLocalPassword(
        JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId, jbyteArray oldPwd, jbyteArray newPwd)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::ChangeLocalPassword(
            JBytes(env, caller),
            JBytes(env, userId),
            JBytes(env, oldPwd),
            JBytes(env, newPwd)));
    }

    JNIEXPORT jboolean JNICALL
    n_resetLocalPasswordWithRecovery(
        JNIEnv* env, jclass, jbyteArray userId, jbyteArray recovery, jbyteArray newPwd, jbyteArray clientIp)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::ResetLocalPasswordWithRecovery(
            JBytes(env, userId),
            JBytes(env, recovery),
            JBytes(env, newPwd),
            JBytes(env, clientIp)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_rtcStartCall(JNIEnv* env, jclass, jbyteArray caller, jbyteArray peer, jint callKind)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::RtcStartCall(
                JBytes(env, caller),
                JBytes(env, peer),
                static_cast<int32_t>(callKind)));
    }

    JNIEXPORT jboolean JNICALL
    n_rtcAcceptCall(JNIEnv* env, jclass, jbyteArray caller, jbyteArray callId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::RtcAcceptCall(JBytes(env, caller), JBytes(env, callId)));
    }

    JNIEXPORT jboolean JNICALL
    n_rtcRejectCall(JNIEnv* env, jclass, jbyteArray caller, jbyteArray callId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::RtcRejectCall(JBytes(env, caller), JBytes(env, callId)));
    }

    JNIEXPORT jboolean JNICALL
    n_rtcEndCall(JNIEnv* env, jclass, jbyteArray caller, jbyteArray callId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::RtcEndCall(JBytes(env, caller), JBytes(env, callId)));
    }

    JNIEXPORT jint JNICALL
    n_rtcGetCallState(JNIEnv* env, jclass, jbyteArray caller, jbyteArray callId)
    {
        (void)env;
        return static_cast<jint>(ZChatIM::jni::JniInterface::RtcGetCallState(
            JBytes(env, caller),
            JBytes(env, callId)));
    }

    JNIEXPORT jint JNICALL
    n_rtcGetCallKind(JNIEnv* env, jclass, jbyteArray caller, jbyteArray callId)
    {
        (void)env;
        return static_cast<jint>(ZChatIM::jni::JniInterface::RtcGetCallKind(
            JBytes(env, caller),
            JBytes(env, callId)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_storeMessage(JNIEnv* env, jclass, jbyteArray caller, jbyteArray imSessionId, jbyteArray payload)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::StoreMessage(
                JBytes(env, caller),
                JBytes(env, imSessionId),
                JBytes(env, payload)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_retrieveMessage(JNIEnv* env, jclass, jbyteArray caller, jbyteArray messageId)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::RetrieveMessage(JBytes(env, caller), JBytes(env, messageId)));
    }

    JNIEXPORT jboolean JNICALL
    n_deleteMessage(JNIEnv* env, jclass, jbyteArray caller, jbyteArray messageId, jbyteArray senderId, jbyteArray sig)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::DeleteMessage(
            JBytes(env, caller),
            JBytes(env, messageId),
            JBytes(env, senderId),
            JBytes(env, sig)));
    }

    JNIEXPORT jboolean JNICALL
    n_recallMessage(JNIEnv* env, jclass, jbyteArray caller, jbyteArray messageId, jbyteArray senderId, jbyteArray sig)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::RecallMessage(
            JBytes(env, caller),
            JBytes(env, messageId),
            JBytes(env, senderId),
            JBytes(env, sig)));
    }

    JNIEXPORT jobjectArray JNICALL
    n_listMessages(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId, jint count)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::ListMessages(JBytes(env, caller), JBytes(env, userId), count));
    }

    JNIEXPORT jobjectArray JNICALL
    n_listMessagesSinceTimestamp(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray userId,
        jlong      sinceMs,
        jint       count)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::ListMessagesSinceTimestamp(
                JBytes(env, caller),
                JBytes(env, userId),
                static_cast<uint64_t>(sinceMs),
                count));
    }

    JNIEXPORT jobjectArray JNICALL
    n_listMessagesSinceMessageId(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray userId,
        jbyteArray lastMsgId,
        jint       count)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::ListMessagesSinceMessageId(
                JBytes(env, caller),
                JBytes(env, userId),
                JBytes(env, lastMsgId),
                count));
    }

    JNIEXPORT jboolean JNICALL
    n_markMessageRead(JNIEnv* env, jclass, jbyteArray caller, jbyteArray messageId, jlong readTs)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::MarkMessageRead(
            JBytes(env, caller),
            JBytes(env, messageId),
            static_cast<uint64_t>(readTs)));
    }

    JNIEXPORT jobjectArray JNICALL
    n_getUnreadSessionMessageIds(JNIEnv* env, jclass, jbyteArray caller, jbyteArray imSessionId, jint limit)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::GetUnreadSessionMessageIds(
                JBytes(env, caller),
                JBytes(env, imSessionId),
                limit));
    }

    JNIEXPORT jboolean JNICALL
    n_storeMessageReplyRelation(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray senderPk,
        jbyteArray messageId,
        jbyteArray repliedMsgId,
        jbyteArray repliedSenderId,
        jbyteArray digest,
        jbyteArray senderId,
        jbyteArray sig)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::StoreMessageReplyRelation(
            JBytes(env, caller),
            JBytes(env, senderPk),
            JBytes(env, messageId),
            JBytes(env, repliedMsgId),
            JBytes(env, repliedSenderId),
            JBytes(env, digest),
            JBytes(env, senderId),
            JBytes(env, sig)));
    }

    JNIEXPORT jobjectArray JNICALL
    n_getMessageReplyRelation(JNIEnv* env, jclass, jbyteArray caller, jbyteArray messageId)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::GetMessageReplyRelation(JBytes(env, caller), JBytes(env, messageId)));
    }

    JNIEXPORT jboolean JNICALL
    n_editMessage(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray messageId,
        jbyteArray newContent,
        jlong      editTsSec,
        jbyteArray signature,
        jbyteArray senderId)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::EditMessage(
            JBytes(env, caller),
            JBytes(env, messageId),
            JBytes(env, newContent),
            static_cast<uint64_t>(editTsSec),
            JBytes(env, signature),
            JBytes(env, senderId)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_getMessageEditState(JNIEnv* env, jclass, jbyteArray caller, jbyteArray messageId)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::GetMessageEditState(JBytes(env, caller), JBytes(env, messageId)));
    }

    JNIEXPORT jboolean JNICALL
    n_storeUserData(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId, jint type, jbyteArray data)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::StoreUserData(
            JBytes(env, caller),
            JBytes(env, userId),
            type,
            JBytes(env, data)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_getUserData(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId, jint type)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::GetUserData(JBytes(env, caller), JBytes(env, userId), type));
    }

    JNIEXPORT jboolean JNICALL
    n_deleteUserData(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId, jint type)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::DeleteUserData(JBytes(env, caller), JBytes(env, userId), type));
    }

    JNIEXPORT jbyteArray JNICALL
    n_sendFriendRequest(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray fromUserId,
        jbyteArray toUserId,
        jlong      ts,
        jbyteArray sig)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::SendFriendRequest(
                JBytes(env, caller),
                JBytes(env, fromUserId),
                JBytes(env, toUserId),
                static_cast<uint64_t>(ts),
                JBytes(env, sig)));
    }

    JNIEXPORT jboolean JNICALL
    n_respondFriendRequest(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray requestId,
        jboolean   accept,
        jbyteArray responderId,
        jlong      ts,
        jbyteArray sig)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::RespondFriendRequest(
            JBytes(env, caller),
            JBytes(env, requestId),
            accept == JNI_TRUE,
            JBytes(env, responderId),
            static_cast<uint64_t>(ts),
            JBytes(env, sig)));
    }

    JNIEXPORT jboolean JNICALL
    n_deleteFriend(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray userId,
        jbyteArray friendId,
        jlong      ts,
        jbyteArray sig)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::DeleteFriend(
            JBytes(env, caller),
            JBytes(env, userId),
            JBytes(env, friendId),
            static_cast<uint64_t>(ts),
            JBytes(env, sig)));
    }

    JNIEXPORT jobjectArray JNICALL
    n_getFriends(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::GetFriends(JBytes(env, caller), JBytes(env, userId)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_createGroup(JNIEnv* env, jclass, jbyteArray caller, jbyteArray creatorId, jstring name)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::CreateGroup(
                JBytes(env, caller),
                JBytes(env, creatorId),
                JString(env, name)));
    }

    JNIEXPORT jboolean JNICALL
    n_inviteMember(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId, jbyteArray userId)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::InviteMember(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, userId)));
    }

    JNIEXPORT jboolean JNICALL
    n_removeMember(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId, jbyteArray userId)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::RemoveMember(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, userId)));
    }

    JNIEXPORT jboolean JNICALL
    n_leaveGroup(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId, jbyteArray userId)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::LeaveGroup(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, userId)));
    }

    JNIEXPORT jobjectArray JNICALL
    n_getGroupMembers(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::GetGroupMembers(JBytes(env, caller), JBytes(env, groupId)));
    }

    JNIEXPORT jboolean JNICALL
    n_updateGroupKey(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::UpdateGroupKey(JBytes(env, caller), JBytes(env, groupId)));
    }

    JNIEXPORT jboolean JNICALL
    n_validateMentionRequest(
        JNIEnv* env,
        jclass,
        jbyteArray   caller,
        jbyteArray   groupId,
        jbyteArray   senderId,
        jint         mentionType,
        jobjectArray mentioned,
        jlong        nowMs,
        jbyteArray   sig)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::ValidateMentionRequest(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, senderId),
            mentionType,
            MentionIdsFromJObjectArray(env, mentioned),
            static_cast<uint64_t>(nowMs),
            JBytes(env, sig)));
    }

    JNIEXPORT jboolean JNICALL
    n_recordMentionAtAllUsage(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray groupId,
        jbyteArray senderId,
        jlong      nowMs)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::RecordMentionAtAllUsage(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, senderId),
            static_cast<uint64_t>(nowMs)));
    }

    JNIEXPORT jboolean JNICALL
    n_muteMember(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray groupId,
        jbyteArray userId,
        jbyteArray mutedBy,
        jlong      startMs,
        jlong      durationSec,
        jbyteArray reason)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::MuteMember(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, userId),
            JBytes(env, mutedBy),
            static_cast<uint64_t>(startMs),
            static_cast<int64_t>(durationSec),
            JBytes(env, reason)));
    }

    JNIEXPORT jboolean JNICALL
    n_isMuted(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId, jbyteArray userId, jlong nowMs)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::IsMuted(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, userId),
            static_cast<uint64_t>(nowMs)));
    }

    JNIEXPORT jboolean JNICALL
    n_unmuteMember(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId, jbyteArray userId, jbyteArray unmutedBy)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::UnmuteMember(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, userId),
            JBytes(env, unmutedBy)));
    }

    JNIEXPORT jboolean JNICALL
    n_updateGroupName(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray groupId,
        jbyteArray updaterId,
        jstring    newName,
        jlong      nowMs)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::UpdateGroupName(
            JBytes(env, caller),
            JBytes(env, groupId),
            JBytes(env, updaterId),
            JString(env, newName),
            static_cast<uint64_t>(nowMs)));
    }

    JNIEXPORT jstring JNICALL
    n_getGroupName(JNIEnv* env, jclass, jbyteArray caller, jbyteArray groupId)
    {
        return ToJString(env, ZChatIM::jni::JniInterface::GetGroupName(JBytes(env, caller), JBytes(env, groupId)));
    }

    JNIEXPORT jboolean JNICALL
    n_storeFileChunk(JNIEnv* env, jclass, jbyteArray caller, jstring fileId, jint chunkIndex, jbyteArray data)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::StoreFileChunk(
            JBytes(env, caller),
            JString(env, fileId),
            chunkIndex,
            JBytes(env, data)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_getFileChunk(JNIEnv* env, jclass, jbyteArray caller, jstring fileId, jint chunkIndex)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::GetFileChunk(
                JBytes(env, caller),
                JString(env, fileId),
                chunkIndex));
    }

    JNIEXPORT jboolean JNICALL
    n_completeFile(JNIEnv* env, jclass, jbyteArray caller, jstring fileId, jbyteArray sha256)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::CompleteFile(
            JBytes(env, caller),
            JString(env, fileId),
            JBytes(env, sha256)));
    }

    JNIEXPORT jboolean JNICALL
    n_cancelFile(JNIEnv* env, jclass, jbyteArray caller, jstring fileId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::CancelFile(JBytes(env, caller), JString(env, fileId)));
    }

    JNIEXPORT jboolean JNICALL
    n_storeTransferResumeChunkIndex(JNIEnv* env, jclass, jbyteArray caller, jstring fileId, jint chunkIndex)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::StoreTransferResumeChunkIndex(
            JBytes(env, caller),
            JString(env, fileId),
            static_cast<uint32_t>(chunkIndex)));
    }

    JNIEXPORT jint JNICALL
    n_getTransferResumeChunkIndex(JNIEnv* env, jclass, jbyteArray caller, jstring fileId)
    {
        (void)env;
        const uint32_t v =
            ZChatIM::jni::JniInterface::GetTransferResumeChunkIndex(JBytes(env, caller), JString(env, fileId));
        return static_cast<jint>(v);
    }

    JNIEXPORT jboolean JNICALL
    n_cleanupTransferResumeChunkIndex(JNIEnv* env, jclass, jbyteArray caller, jstring fileId)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::CleanupTransferResumeChunkIndex(
            JBytes(env, caller),
            JString(env, fileId)));
    }

    JNIEXPORT jobjectArray JNICALL
    n_getSessionMessages(JNIEnv* env, jclass, jbyteArray caller, jbyteArray imSessionId, jint limit)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::GetSessionMessages(
                JBytes(env, caller),
                JBytes(env, imSessionId),
                limit));
    }

    JNIEXPORT jboolean JNICALL
    n_getSessionStatus(JNIEnv* env, jclass, jbyteArray caller, jbyteArray imSessionId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::GetSessionStatus(JBytes(env, caller), JBytes(env, imSessionId)));
    }

    ZCHAT_JNI n_touchSession(JNIEnv* env, jclass, jbyteArray caller, jbyteArray imSessionId, jlong nowMs)
    {
        (void)env;
        ZChatIM::jni::JniInterface::TouchSession(
            JBytes(env, caller),
            JBytes(env, imSessionId),
            static_cast<uint64_t>(nowMs));
    }

    ZCHAT_JNI n_cleanupExpiredSessions(JNIEnv* env, jclass, jbyteArray caller, jlong nowMs)
    {
        (void)env;
        ZChatIM::jni::JniInterface::CleanupExpiredSessions(
            JBytes(env, caller),
            static_cast<uint64_t>(nowMs));
    }

    JNIEXPORT jbyteArray JNICALL
    n_registerDeviceSession(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray userId,
        jbyteArray deviceId,
        jbyteArray sessionId,
        jlong      loginMs,
        jlong      lastMs)
    {
        std::vector<uint8_t> kicked;
        if (!ZChatIM::jni::JniInterface::RegisterDeviceSession(
                JBytes(env, caller),
                JBytes(env, userId),
                JBytes(env, deviceId),
                JBytes(env, sessionId),
                static_cast<uint64_t>(loginMs),
                static_cast<uint64_t>(lastMs),
                kicked)) {
            return nullptr;
        }
        // 成功：无踢出 → 长度 0 的 byte[]；有踢出 → 16B（与 C++ 空 vector「无踢」区分于失败 null）
        return ToJBytesEmptyOk(env, kicked);
    }

    JNIEXPORT jboolean JNICALL
    n_updateLastActive(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray userId,
        jbyteArray sessionId,
        jlong      nowMs)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::UpdateLastActive(
            JBytes(env, caller),
            JBytes(env, userId),
            JBytes(env, sessionId),
            static_cast<uint64_t>(nowMs)));
    }

    JNIEXPORT jobjectArray JNICALL
    n_getDeviceSessions(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId)
    {
        return ToJObjectArrayOfByteArray(
            env,
            ZChatIM::jni::JniInterface::GetDeviceSessions(JBytes(env, caller), JBytes(env, userId)));
    }

    ZCHAT_JNI n_cleanupExpiredDeviceSessions(JNIEnv* env, jclass, jbyteArray caller, jlong nowMs)
    {
        (void)env;
        ZChatIM::jni::JniInterface::CleanupExpiredDeviceSessions(
            JBytes(env, caller),
            static_cast<uint64_t>(nowMs));
    }

    JNIEXPORT jboolean JNICALL
    n_getUserStatus(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::GetUserStatus(JBytes(env, caller), JBytes(env, userId)));
    }

    JNIEXPORT jboolean JNICALL
    n_cleanupSessionMessages(JNIEnv* env, jclass, jbyteArray caller, jbyteArray imSessionId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::CleanupSessionMessages(JBytes(env, caller), JBytes(env, imSessionId)));
    }

    JNIEXPORT jboolean JNICALL
    n_cleanupExpiredData(JNIEnv* env, jclass, jbyteArray caller)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::CleanupExpiredData(JBytes(env, caller)));
    }

    JNIEXPORT jboolean JNICALL
    n_optimizeStorage(JNIEnv* env, jclass, jbyteArray caller)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::OptimizeStorage(JBytes(env, caller)));
    }

    JNIEXPORT jobject JNICALL
    n_getStorageStatus(JNIEnv* env, jclass, jbyteArray caller)
    {
        return NewStringStringMap(
            env,
            ZChatIM::jni::JniInterface::GetStorageStatus(JBytes(env, caller)));
    }

    JNIEXPORT jlong JNICALL
    n_getMessageCount(JNIEnv* env, jclass, jbyteArray caller)
    {
        (void)env;
        return static_cast<jlong>(ZChatIM::jni::JniInterface::GetMessageCount(JBytes(env, caller)));
    }

    JNIEXPORT jlong JNICALL
    n_getFileCount(JNIEnv* env, jclass, jbyteArray caller)
    {
        (void)env;
        return static_cast<jlong>(ZChatIM::jni::JniInterface::GetFileCount(JBytes(env, caller)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_generateMasterKey(JNIEnv* env, jclass, jbyteArray caller)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::GenerateMasterKey(JBytes(env, caller)));
    }

    JNIEXPORT jbyteArray JNICALL
    n_refreshSessionKey(JNIEnv* env, jclass, jbyteArray caller)
    {
        return ToJBytesOrNull(
            env,
            ZChatIM::jni::JniInterface::RefreshSessionKey(JBytes(env, caller)));
    }

    ZCHAT_JNI n_emergencyWipe(JNIEnv* env, jclass, jbyteArray caller)
    {
        (void)env;
        ZChatIM::jni::JniInterface::EmergencyWipe(JBytes(env, caller));
    }

    JNIEXPORT jobject JNICALL
    n_getStatus(JNIEnv* env, jclass, jbyteArray caller)
    {
        return NewStringStringMap(env, ZChatIM::jni::JniInterface::GetStatus(JBytes(env, caller)));
    }

    JNIEXPORT jboolean JNICALL
    n_rotateKeys(JNIEnv* env, jclass, jbyteArray caller)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::RotateKeys(JBytes(env, caller)));
    }

    ZCHAT_JNI n_configurePinnedPublicKeyHashes(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray current,
        jbyteArray standby)
    {
        (void)env;
        ZChatIM::jni::JniInterface::ConfigurePinnedPublicKeyHashes(
            JBytes(env, caller),
            JBytes(env, current),
            JBytes(env, standby));
    }

    JNIEXPORT jboolean JNICALL
    n_verifyPinnedServerCertificate(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray clientId,
        jbyteArray presented)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::VerifyPinnedServerCertificate(
            JBytes(env, caller),
            JBytes(env, clientId),
            JBytes(env, presented)));
    }

    JNIEXPORT jboolean JNICALL
    n_isClientBanned(JNIEnv* env, jclass, jbyteArray caller, jbyteArray clientId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::IsClientBanned(JBytes(env, caller), JBytes(env, clientId)));
    }

    ZCHAT_JNI n_recordFailure(JNIEnv* env, jclass, jbyteArray caller, jbyteArray clientId)
    {
        (void)env;
        ZChatIM::jni::JniInterface::RecordFailure(JBytes(env, caller), JBytes(env, clientId));
    }

    ZCHAT_JNI n_clearBan(JNIEnv* env, jclass, jbyteArray caller, jbyteArray clientId)
    {
        (void)env;
        ZChatIM::jni::JniInterface::ClearBan(JBytes(env, caller), JBytes(env, clientId));
    }

    JNIEXPORT jboolean JNICALL
    n_deleteAccount(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray userId,
        jbyteArray reauth,
        jbyteArray second)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::DeleteAccount(
            JBytes(env, caller),
            JBytes(env, userId),
            JBytes(env, reauth),
            JBytes(env, second)));
    }

    JNIEXPORT jboolean JNICALL
    n_isAccountDeleted(JNIEnv* env, jclass, jbyteArray caller, jbyteArray userId)
    {
        (void)env;
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::IsAccountDeleted(JBytes(env, caller), JBytes(env, userId)));
    }

    JNIEXPORT jboolean JNICALL
    n_updateFriendNote(
        JNIEnv* env,
        jclass,
        jbyteArray caller,
        jbyteArray userId,
        jbyteArray friendId,
        jbyteArray note,
        jlong      ts,
        jbyteArray sig)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::UpdateFriendNote(
            JBytes(env, caller),
            JBytes(env, userId),
            JBytes(env, friendId),
            JBytes(env, note),
            static_cast<uint64_t>(ts),
            JBytes(env, sig)));
    }

    JNIEXPORT jboolean JNICALL
    n_validateJniCallWeak(JNIEnv* env, jclass)
    {
        (void)env;
        return static_cast<jboolean>(ZChatIM::jni::JniInterface::ValidateJniCall());
    }

    JNIEXPORT jboolean JNICALL
    n_validateJniCallStrong(JNIEnv* env, jclass, jclass paramClass)
    {
        return static_cast<jboolean>(
            ZChatIM::jni::JniInterface::ValidateJniCall(env, paramClass));
    }

    static const JNINativeMethod kNativeMethods[] = {
        {"initialize", "(Ljava/lang/String;Ljava/lang/String;)Z", reinterpret_cast<void*>(n_initialize)},
        {"initializeWithPassphrase",
         "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
         reinterpret_cast<void*>(n_initializeWithPassphrase)},
        {"cleanup", "()V", reinterpret_cast<void*>(n_cleanup)},
        {"auth", "([B[B[B)[B", reinterpret_cast<void*>(n_auth)},
        {"verifySession", "([B)Z", reinterpret_cast<void*>(n_verifySession)},
        {"destroySession", "([B[B)Z", reinterpret_cast<void*>(n_destroySession)},
        {"registerLocalUser", "([B[B[B)Z", reinterpret_cast<void*>(n_registerLocalUser)},
        {"authWithLocalPassword", "([B[B[B)[B", reinterpret_cast<void*>(n_authWithLocalPassword)},
        {"hasLocalPassword", "([B)Z", reinterpret_cast<void*>(n_hasLocalPassword)},
        {"changeLocalPassword", "([B[B[B[B)Z", reinterpret_cast<void*>(n_changeLocalPassword)},
        {"resetLocalPasswordWithRecovery", "([B[B[B[B)Z", reinterpret_cast<void*>(n_resetLocalPasswordWithRecovery)},
        {"rtcStartCall", "([B[BI)[B", reinterpret_cast<void*>(n_rtcStartCall)},
        {"rtcAcceptCall", "([B[B)Z", reinterpret_cast<void*>(n_rtcAcceptCall)},
        {"rtcRejectCall", "([B[B)Z", reinterpret_cast<void*>(n_rtcRejectCall)},
        {"rtcEndCall", "([B[B)Z", reinterpret_cast<void*>(n_rtcEndCall)},
        {"rtcGetCallState", "([B[B)I", reinterpret_cast<void*>(n_rtcGetCallState)},
        {"rtcGetCallKind", "([B[B)I", reinterpret_cast<void*>(n_rtcGetCallKind)},
        {"storeMessage", "([B[B[B)[B", reinterpret_cast<void*>(n_storeMessage)},
        {"retrieveMessage", "([B[B)[B", reinterpret_cast<void*>(n_retrieveMessage)},
        {"deleteMessage", "([B[B[B[B)Z", reinterpret_cast<void*>(n_deleteMessage)},
        {"recallMessage", "([B[B[B[B)Z", reinterpret_cast<void*>(n_recallMessage)},
        {"listMessages", "([B[BI)[[B", reinterpret_cast<void*>(n_listMessages)},
        {"listMessagesSinceTimestamp", "([B[BJI)[[B", reinterpret_cast<void*>(n_listMessagesSinceTimestamp)},
        {"listMessagesSinceMessageId", "([B[B[BI)[[B", reinterpret_cast<void*>(n_listMessagesSinceMessageId)},
        {"markMessageRead", "([B[BJ)Z", reinterpret_cast<void*>(n_markMessageRead)},
        {"getUnreadSessionMessageIds", "([B[BI)[[B", reinterpret_cast<void*>(n_getUnreadSessionMessageIds)},
        {"storeMessageReplyRelation", "([B[B[B[B[B[B[B[B)Z", reinterpret_cast<void*>(n_storeMessageReplyRelation)},
        {"getMessageReplyRelation", "([B[B)[[B", reinterpret_cast<void*>(n_getMessageReplyRelation)},
        {"editMessage", "([B[B[BJ[B[B)Z", reinterpret_cast<void*>(n_editMessage)},
        {"getMessageEditState", "([B[B)[B", reinterpret_cast<void*>(n_getMessageEditState)},
        {"storeUserData", "([B[BI[B)Z", reinterpret_cast<void*>(n_storeUserData)},
        {"getUserData", "([B[BI)[B", reinterpret_cast<void*>(n_getUserData)},
        {"deleteUserData", "([B[BI)Z", reinterpret_cast<void*>(n_deleteUserData)},
        {"sendFriendRequest", "([B[B[BJ[B)[B", reinterpret_cast<void*>(n_sendFriendRequest)},
        {"respondFriendRequest", "([B[BZ[BJ[B)Z", reinterpret_cast<void*>(n_respondFriendRequest)},
        {"deleteFriend", "([B[B[BJ[B)Z", reinterpret_cast<void*>(n_deleteFriend)},
        {"getFriends", "([B[B)[[B", reinterpret_cast<void*>(n_getFriends)},
        {"createGroup", "([B[BLjava/lang/String;)[B", reinterpret_cast<void*>(n_createGroup)},
        {"inviteMember", "([B[B[B)Z", reinterpret_cast<void*>(n_inviteMember)},
        {"removeMember", "([B[B[B)Z", reinterpret_cast<void*>(n_removeMember)},
        {"leaveGroup", "([B[B[B)Z", reinterpret_cast<void*>(n_leaveGroup)},
        {"getGroupMembers", "([B[B)[[B", reinterpret_cast<void*>(n_getGroupMembers)},
        {"updateGroupKey", "([B[B)Z", reinterpret_cast<void*>(n_updateGroupKey)},
        {"validateMentionRequest", "([B[B[BI[[BJ[B)Z", reinterpret_cast<void*>(n_validateMentionRequest)},
        {"recordMentionAtAllUsage", "([B[B[BJ)Z", reinterpret_cast<void*>(n_recordMentionAtAllUsage)},
        {"muteMember", "([B[B[B[BJJ[B)Z", reinterpret_cast<void*>(n_muteMember)},
        {"isMuted", "([B[B[BJ)Z", reinterpret_cast<void*>(n_isMuted)},
        {"unmuteMember", "([B[B[B[B)Z", reinterpret_cast<void*>(n_unmuteMember)},
        {"updateGroupName", "([B[B[BLjava/lang/String;J)Z", reinterpret_cast<void*>(n_updateGroupName)},
        {"getGroupName", "([B[B)Ljava/lang/String;", reinterpret_cast<void*>(n_getGroupName)},
        {"storeFileChunk", "([BLjava/lang/String;I[B)Z", reinterpret_cast<void*>(n_storeFileChunk)},
        {"getFileChunk", "([BLjava/lang/String;I)[B", reinterpret_cast<void*>(n_getFileChunk)},
        {"completeFile", "([BLjava/lang/String;[B)Z", reinterpret_cast<void*>(n_completeFile)},
        {"cancelFile", "([BLjava/lang/String;)Z", reinterpret_cast<void*>(n_cancelFile)},
        {"storeTransferResumeChunkIndex", "([BLjava/lang/String;I)Z", reinterpret_cast<void*>(n_storeTransferResumeChunkIndex)},
        {"getTransferResumeChunkIndex", "([BLjava/lang/String;)I", reinterpret_cast<void*>(n_getTransferResumeChunkIndex)},
        {"cleanupTransferResumeChunkIndex", "([BLjava/lang/String;)Z", reinterpret_cast<void*>(n_cleanupTransferResumeChunkIndex)},
        {"getSessionMessages", "([B[BI)[[B", reinterpret_cast<void*>(n_getSessionMessages)},
        {"getSessionStatus", "([B[B)Z", reinterpret_cast<void*>(n_getSessionStatus)},
        {"touchSession", "([B[BJ)V", reinterpret_cast<void*>(n_touchSession)},
        {"cleanupExpiredSessions", "([BJ)V", reinterpret_cast<void*>(n_cleanupExpiredSessions)},
        {"registerDeviceSession", "([B[B[B[BJJ)[B", reinterpret_cast<void*>(n_registerDeviceSession)},
        {"updateLastActive", "([B[B[BJ)Z", reinterpret_cast<void*>(n_updateLastActive)},
        {"getDeviceSessions", "([B[B)[[B", reinterpret_cast<void*>(n_getDeviceSessions)},
        {"cleanupExpiredDeviceSessions", "([BJ)V", reinterpret_cast<void*>(n_cleanupExpiredDeviceSessions)},
        {"getUserStatus", "([B[B)Z", reinterpret_cast<void*>(n_getUserStatus)},
        {"cleanupSessionMessages", "([B[B)Z", reinterpret_cast<void*>(n_cleanupSessionMessages)},
        {"cleanupExpiredData", "([B)Z", reinterpret_cast<void*>(n_cleanupExpiredData)},
        {"optimizeStorage", "([B)Z", reinterpret_cast<void*>(n_optimizeStorage)},
        {"getStorageStatus", "([B)Ljava/util/Map;", reinterpret_cast<void*>(n_getStorageStatus)},
        {"getMessageCount", "([B)J", reinterpret_cast<void*>(n_getMessageCount)},
        {"getFileCount", "([B)J", reinterpret_cast<void*>(n_getFileCount)},
        {"generateMasterKey", "([B)[B", reinterpret_cast<void*>(n_generateMasterKey)},
        {"refreshSessionKey", "([B)[B", reinterpret_cast<void*>(n_refreshSessionKey)},
        {"emergencyWipe", "([B)V", reinterpret_cast<void*>(n_emergencyWipe)},
        {"getStatus", "([B)Ljava/util/Map;", reinterpret_cast<void*>(n_getStatus)},
        {"rotateKeys", "([B)Z", reinterpret_cast<void*>(n_rotateKeys)},
        {"configurePinnedPublicKeyHashes", "([B[B[B)V", reinterpret_cast<void*>(n_configurePinnedPublicKeyHashes)},
        {"verifyPinnedServerCertificate", "([B[B[B)Z", reinterpret_cast<void*>(n_verifyPinnedServerCertificate)},
        {"isClientBanned", "([B[B)Z", reinterpret_cast<void*>(n_isClientBanned)},
        {"recordFailure", "([B[B)V", reinterpret_cast<void*>(n_recordFailure)},
        {"clearBan", "([B[B)V", reinterpret_cast<void*>(n_clearBan)},
        {"deleteAccount", "([B[B[B[B)Z", reinterpret_cast<void*>(n_deleteAccount)},
        {"isAccountDeleted", "([B[B)Z", reinterpret_cast<void*>(n_isAccountDeleted)},
        {"updateFriendNote", "([B[B[B[BJ[B)Z", reinterpret_cast<void*>(n_updateFriendNote)},
        {"validateJniCall", "()Z", reinterpret_cast<void*>(n_validateJniCallWeak)},
        {"validateJniCall", "(Ljava/lang/Class;)Z", reinterpret_cast<void*>(n_validateJniCallStrong)},
    };

    JNIEXPORT jint JNICALL zchatim_RegisterNatives(JNIEnv* env)
    {
        jclass cls = env->FindClass("com/yhj/zchat/jni/ZChatIMNative");
        if (!cls) {
            return JNI_ERR;
        }
        const jint r = env->RegisterNatives(
            cls,
            kNativeMethods,
            static_cast<jint>(sizeof(kNativeMethods) / sizeof(kNativeMethods[0])));
        env->DeleteLocalRef(cls);
        return r == JNI_OK ? JNI_OK : JNI_ERR;
    }

} // extern "C"

#undef ZCHAT_JNI
