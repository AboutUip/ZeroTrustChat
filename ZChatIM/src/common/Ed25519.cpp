#include "common/Ed25519.h"
#include "common/OpenSsl3Required.h"

#include <openssl/evp.h>

namespace ZChatIM::common {

    bool Ed25519VerifyDetached(
        const uint8_t* message,
        size_t         messageLen,
        const uint8_t signature[64],
        const uint8_t publicKey[32])
    {
        if (!message && messageLen > 0) {
            return false;
        }
        EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, publicKey, 32);
        if (!pkey) {
            return false;
        }
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            EVP_PKEY_free(pkey);
            return false;
        }
        const int initOk = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey);
        int       verifyOk = 0;
        if (initOk == 1) {
            verifyOk = EVP_DigestVerify(ctx, signature, 64, message, messageLen);
        }
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return initOk == 1 && verifyOk == 1;
    }

} // namespace ZChatIM::common
