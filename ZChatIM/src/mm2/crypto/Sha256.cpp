// SHA-256 via OpenSSL 3 EVP（与 **ZChatIM** 其余密码学统一 **libcrypto**）。

#include "mm2/crypto/Sha256.h"
#include "common/OpenSsl3Required.h"

#include <openssl/evp.h>

#include <utility>

namespace ZChatIM::crypto {

    namespace {

        using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

        MdCtxPtr MakeMdCtx()
        {
            EVP_MD_CTX* raw = EVP_MD_CTX_new();
            return MdCtxPtr(raw, &EVP_MD_CTX_free);
        }

        bool DigestInitSha256(EVP_MD_CTX* ctx)
        {
            return ctx != nullptr && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
        }

    } // namespace

    struct Sha256Hasher::Impl {
        MdCtxPtr ctx;

        Impl()
            : ctx(MakeMdCtx())
        {
            if (ctx) {
                (void)DigestInitSha256(ctx.get());
            }
        }
    };

    Sha256Hasher::Sha256Hasher()
        : impl_(std::make_unique<Impl>())
    {
    }

    Sha256Hasher::~Sha256Hasher() = default;

    Sha256Hasher::Sha256Hasher(Sha256Hasher&&) noexcept = default;

    Sha256Hasher& Sha256Hasher::operator=(Sha256Hasher&&) noexcept = default;

    void Sha256Hasher::Reset()
    {
        if (!impl_ || !impl_->ctx) {
            return;
        }
        (void)DigestInitSha256(impl_->ctx.get());
    }

    bool Sha256Hasher::Update(const uint8_t* data, size_t length)
    {
        if (length > 0 && data == nullptr) {
            return false;
        }
        if (!impl_ || !impl_->ctx) {
            return false;
        }
        return EVP_DigestUpdate(impl_->ctx.get(), data, length) == 1;
    }

    bool Sha256Hasher::Final(uint8_t outDigest[32])
    {
        if (outDigest == nullptr || !impl_ || !impl_->ctx) {
            return false;
        }
        unsigned int len = 32;
        if (EVP_DigestFinal_ex(impl_->ctx.get(), outDigest, &len) != 1) {
            return false;
        }
        if (len != 32U) {
            return false;
        }
        Reset();
        return true;
    }

    bool Sha256(const uint8_t* data, size_t length, uint8_t outDigest[32])
    {
        if (outDigest == nullptr) {
            return false;
        }
        if (length > 0 && data == nullptr) {
            return false;
        }
        unsigned int len = 32;
        return EVP_Digest(data, length, outDigest, &len, EVP_sha256(), nullptr) == 1 && len == 32U;
    }

} // namespace ZChatIM::crypto
