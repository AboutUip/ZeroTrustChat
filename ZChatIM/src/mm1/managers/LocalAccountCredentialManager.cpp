#include "mm1/managers/LocalAccountCredentialManager.h"
#include "mm1/managers/AuthSessionManager.h"
#include "mm2/MM2.h"
#include "mm2/storage/Crypto.h"
#include "Types.h"
#include "common/Memory.h"

#include <array>
#include <cstring>

namespace ZChatIM::mm1 {
    namespace {

        constexpr uint32_t kBlobVersion = 1;
        constexpr size_t   kSaltBytes = 16;
        constexpr size_t   kDkBytes   = 32;
        constexpr size_t   kBlobV1Len = 4 + kSaltBytes + 4 + kDkBytes;

        void AppendU32Be(std::vector<uint8_t>& v, uint32_t x)
        {
            v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>(x & 0xFF));
        }

        bool ReadU32Be(const uint8_t* p, size_t n, size_t& off, uint32_t& out)
        {
            if (off + 4 > n) {
                return false;
            }
            out = (static_cast<uint32_t>(p[off]) << 24) | (static_cast<uint32_t>(p[off + 1]) << 16)
                | (static_cast<uint32_t>(p[off + 2]) << 8) | static_cast<uint32_t>(p[off + 3]);
            off += 4;
            return true;
        }

        bool PasswordShapeOk(const std::vector<uint8_t>& p)
        {
            return p.size() >= LOCAL_ACCOUNT_PASSWORD_MIN_UTF8_BYTES
                && p.size() <= LOCAL_ACCOUNT_PASSWORD_MAX_UTF8_BYTES;
        }

        bool RecoveryShapeOk(const std::vector<uint8_t>& r)
        {
            return r.size() >= AUTH_OPAQUE_CREDENTIAL_MIN_BYTES && r.size() <= 8192;
        }

        bool BuildCredentialBlobV1(std::vector<uint8_t>& outBlob)
        {
            outBlob.clear();
            outBlob.reserve(kBlobV1Len);
            AppendU32Be(outBlob, kBlobVersion);
            std::vector<uint8_t> salt = mm2::Crypto::GenerateSecureRandom(kSaltBytes);
            if (salt.size() != kSaltBytes) {
                outBlob.clear();
                return false;
            }
            outBlob.insert(outBlob.end(), salt.begin(), salt.end());
            AppendU32Be(outBlob, static_cast<uint32_t>(LOCAL_ACCOUNT_PBKDF2_ITERATIONS));
            outBlob.resize(4 + kSaltBytes + 4 + kDkBytes); // placeholder for dk
            return true;
        }

        bool FillPasswordDerivedKey(
            const std::vector<uint8_t>& passwordUtf8,
            const uint8_t*            salt,
            uint32_t                    iterations,
            uint8_t*                    dkOut)
        {
            return mm2::Crypto::DeriveKeyPbkdf2HmacSha256(
                passwordUtf8.data(),
                passwordUtf8.size(),
                salt,
                kSaltBytes,
                static_cast<int>(iterations),
                dkOut,
                kDkBytes);
        }

        bool ParseBlobV1(const std::vector<uint8_t>& blob, uint32_t& iterOut, const uint8_t** saltOut, const uint8_t** dkOut)
        {
            if (blob.size() != kBlobV1Len) {
                return false;
            }
            size_t off = 0;
            uint32_t ver = 0;
            if (!ReadU32Be(blob.data(), blob.size(), off, ver) || ver != kBlobVersion) {
                return false;
            }
            *saltOut = blob.data() + off;
            off += kSaltBytes;
            if (!ReadU32Be(blob.data(), blob.size(), off, iterOut)) {
                return false;
            }
            *dkOut = blob.data() + off;
            return off + kDkBytes == blob.size();
        }

        bool VerifyPasswordAgainstBlob(const std::vector<uint8_t>& passwordUtf8, const std::vector<uint8_t>& blob)
        {
            uint32_t                   iter = 0;
            const uint8_t*             salt = nullptr;
            const uint8_t*             dk   = nullptr;
            if (!ParseBlobV1(blob, iter, &salt, &dk)) {
                return false;
            }
            std::array<uint8_t, kDkBytes> trial{};
            if (!FillPasswordDerivedKey(passwordUtf8, salt, iter, trial.data())) {
                return false;
            }
            return common::Memory::ConstantTimeCompare(trial.data(), dk, kDkBytes);
        }

        bool WriteCredentialBlobForPassword(const std::vector<uint8_t>& passwordUtf8, std::vector<uint8_t>& outBlob)
        {
            if (!BuildCredentialBlobV1(outBlob)) {
                return false;
            }
            const uint8_t* salt = outBlob.data() + 4;
            uint32_t       iter = LOCAL_ACCOUNT_PBKDF2_ITERATIONS;
            uint8_t*       dk  = outBlob.data() + 4 + kSaltBytes + 4;
            if (!FillPasswordDerivedKey(passwordUtf8, salt, iter, dk)) {
                outBlob.clear();
                return false;
            }
            return true;
        }

    } // namespace

    LocalAccountCredentialManager::LocalAccountCredentialManager()  = default;
    LocalAccountCredentialManager::~LocalAccountCredentialManager() = default;

    bool LocalAccountCredentialManager::HasLocalPassword(const std::vector<uint8_t>& userId)
    {
        if (userId.size() != USER_ID_SIZE) {
            return false;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (!m2.IsInitialized()) {
            return false;
        }
        std::vector<uint8_t> blob;
        return m2.GetMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1, blob) && blob.size() == kBlobV1Len;
    }

    bool LocalAccountCredentialManager::RegisterLocalUser(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& passwordUtf8,
        const std::vector<uint8_t>& recoverySecretUtf8)
    {
        if (userId.size() != USER_ID_SIZE || !PasswordShapeOk(passwordUtf8) || !RecoveryShapeOk(recoverySecretUtf8)) {
            return false;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (!m2.IsInitialized()) {
            return false;
        }
        std::vector<uint8_t> existing;
        if (m2.GetMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1, existing) && !existing.empty()) {
            return false;
        }
        std::vector<uint8_t> passBlob;
        std::vector<uint8_t> recvBlob;
        if (!WriteCredentialBlobForPassword(passwordUtf8, passBlob)) {
            return false;
        }
        if (!WriteCredentialBlobForPassword(recoverySecretUtf8, recvBlob)) {
            return false;
        }
        if (!m2.StoreMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1, passBlob)) {
            return false;
        }
        if (!m2.StoreMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_RECOVERY_V1, recvBlob)) {
            (void)m2.DeleteMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1);
            return false;
        }
        return true;
    }

    std::vector<uint8_t> LocalAccountCredentialManager::AuthWithLocalPassword(
        AuthSessionManager&         auth,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& passwordUtf8,
        const std::vector<uint8_t>& clientIp)
    {
        if (userId.size() != USER_ID_SIZE || !PasswordShapeOk(passwordUtf8)) {
            return {};
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (!m2.IsInitialized()) {
            return {};
        }
        if (!auth.ConsumeAuthAttemptSlot(userId, clientIp)) {
            return {};
        }
        std::vector<uint8_t> blob;
        if (!m2.GetMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1, blob) || blob.size() != kBlobV1Len) {
            auth.OnAuthIdentityCheckFailed(userId, clientIp);
            return {};
        }
        if (!VerifyPasswordAgainstBlob(passwordUtf8, blob)) {
            auth.OnAuthIdentityCheckFailed(userId, clientIp);
            return {};
        }
        return auth.FinalizeAuthSuccess(userId, clientIp);
    }

    bool LocalAccountCredentialManager::ChangeLocalPassword(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& oldPasswordUtf8,
        const std::vector<uint8_t>& newPasswordUtf8)
    {
        if (userId.size() != USER_ID_SIZE || !PasswordShapeOk(oldPasswordUtf8) || !PasswordShapeOk(newPasswordUtf8)) {
            return false;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (!m2.IsInitialized()) {
            return false;
        }
        std::vector<uint8_t> blob;
        if (!m2.GetMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1, blob) || blob.size() != kBlobV1Len) {
            return false;
        }
        if (!VerifyPasswordAgainstBlob(oldPasswordUtf8, blob)) {
            return false;
        }
        std::vector<uint8_t> newBlob;
        if (!WriteCredentialBlobForPassword(newPasswordUtf8, newBlob)) {
            return false;
        }
        return m2.StoreMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1, newBlob);
    }

    bool LocalAccountCredentialManager::ResetLocalPasswordWithRecovery(
        AuthSessionManager&         auth,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& recoverySecretUtf8,
        const std::vector<uint8_t>& newPasswordUtf8,
        const std::vector<uint8_t>& clientIp)
    {
        if (userId.size() != USER_ID_SIZE || !RecoveryShapeOk(recoverySecretUtf8) || !PasswordShapeOk(newPasswordUtf8)) {
            return false;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (!m2.IsInitialized()) {
            return false;
        }
        if (!auth.ConsumeAuthAttemptSlot(userId, clientIp)) {
            return false;
        }
        std::vector<uint8_t> recvBlob;
        if (!m2.GetMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_RECOVERY_V1, recvBlob) || recvBlob.size() != kBlobV1Len) {
            auth.OnAuthIdentityCheckFailed(userId, clientIp);
            return false;
        }
        if (!VerifyPasswordAgainstBlob(recoverySecretUtf8, recvBlob)) {
            auth.OnAuthIdentityCheckFailed(userId, clientIp);
            return false;
        }
        std::vector<uint8_t> newPassBlob;
        if (!WriteCredentialBlobForPassword(newPasswordUtf8, newPassBlob)) {
            return false;
        }
        if (!m2.StoreMm1UserDataBlob(userId, MM1_USER_KV_TYPE_LOCAL_PASSWORD_V1, newPassBlob)) {
            return false;
        }
        auth.ClearAuthThrottleSuccess(userId, clientIp);
        return true;
    }

} // namespace ZChatIM::mm1
