// macOS / iOS：**mm2_message_key.bin** 的 **ZMK3** 封装密钥存 **Keychain**（与 **ZMK2** 机器派生区分）。
#include "mm2/storage/Crypto.h"
#include "Types.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ZChatIM::mm2::detail {

    namespace {

        bool MakeAccountHexFromIndexDir(const std::string& indexDirUtf8, char outHex33[33], std::string& err)
        {
            std::vector<uint8_t> h = Crypto::HashSha256(
                reinterpret_cast<const uint8_t*>(indexDirUtf8.data()), indexDirUtf8.size());
            if (h.size() != ZChatIM::SHA256_SIZE) {
                err = "HashSha256 failed";
                return false;
            }
            static const char* xd = "0123456789abcdef";
            for (size_t i = 0; i < 16U; ++i) {
                outHex33[2U * i]     = xd[h[i] >> 4U];
                outHex33[2U * i + 1U] = xd[h[i] & 0xFU];
            }
            outHex33[32] = '\0';
            return true;
        }

    } // namespace

    bool Mm2DarwinGetOrCreateMessageWrapKey(const std::string& indexDirUtf8, std::vector<uint8_t>& out32, std::string& err)
    {
        out32.clear();
        err.clear();
        char acct[33]{};
        if (!MakeAccountHexFromIndexDir(indexDirUtf8, acct, err)) {
            return false;
        }

        CFStringRef service = CFSTR("ZChatIM.MM2.mm2_message_key.wrap.v1");
        CFStringRef account =
            CFStringCreateWithCString(kCFAllocatorDefault, acct, kCFStringEncodingUTF8);
        if (account == nullptr) {
            err = "CFStringCreateWithCString(account) failed";
            return false;
        }

        const void* qkeys[] = {
            kSecClass,
            kSecAttrService,
            kSecAttrAccount,
            kSecReturnData,
            kSecMatchLimit,
        };
        const void* qvals[] = {
            kSecClassGenericPassword,
            service,
            account,
            kCFBooleanTrue,
            kSecMatchLimitOne,
        };
        CFDictionaryRef query = CFDictionaryCreate(
            kCFAllocatorDefault,
            qkeys,
            qvals,
            sizeof(qkeys) / sizeof(qkeys[0]),
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        if (query == nullptr) {
            CFRelease(account);
            err = "CFDictionaryCreate(query) failed";
            return false;
        }

        CFTypeRef result = nullptr;
        OSStatus  st     = SecItemCopyMatching(query, &result);
        CFRelease(query);

        if (st == errSecSuccess) {
            if (result == nullptr || CFGetTypeID(result) != CFDataGetTypeID()) {
                if (result != nullptr) {
                    CFRelease(result);
                }
                CFRelease(account);
                err = "Keychain item is not CFData";
                return false;
            }
            CFDataRef data = reinterpret_cast<CFDataRef>(result);
            if (CFDataGetLength(data) != static_cast<CFIndex>(ZChatIM::CRYPTO_KEY_SIZE)) {
                CFRelease(result);
                CFRelease(account);
                err = "Keychain wrap key wrong length";
                return false;
            }
            const uint8_t* p = CFDataGetBytePtr(data);
            out32.assign(p, p + ZChatIM::CRYPTO_KEY_SIZE);
            CFRelease(result);
            CFRelease(account);
            return true;
        }

        if (st != errSecItemNotFound) {
            CFRelease(account);
            err = "SecItemCopyMatching failed OSStatus=" + std::to_string(static_cast<int>(st));
            return false;
        }

        std::vector<uint8_t> nk = Crypto::GenerateSecureRandom(ZChatIM::CRYPTO_KEY_SIZE);
        if (nk.size() != ZChatIM::CRYPTO_KEY_SIZE) {
            CFRelease(account);
            err = "GenerateSecureRandom failed";
            return false;
        }

        CFDataRef vdata = CFDataCreate(
            kCFAllocatorDefault, nk.data(), static_cast<CFIndex>(nk.size()));
        if (vdata == nullptr) {
            CFRelease(account);
            err = "CFDataCreate failed";
            return false;
        }

        const void* akeys[] = {
            kSecClass,
            kSecAttrService,
            kSecAttrAccount,
            kSecValueData,
            kSecAttrAccessible,
        };
        const void* avals[] = {
            kSecClassGenericPassword,
            service,
            account,
            vdata,
            kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
        };
        CFDictionaryRef addDict = CFDictionaryCreate(
            kCFAllocatorDefault,
            akeys,
            avals,
            sizeof(akeys) / sizeof(akeys[0]),
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        if (addDict == nullptr) {
            CFRelease(vdata);
            CFRelease(account);
            err = "CFDictionaryCreate(add) failed";
            return false;
        }

        st = SecItemAdd(addDict, nullptr);
        CFRelease(addDict);
        CFRelease(vdata);
        CFRelease(account);

        if (st != errSecSuccess) {
            err = "SecItemAdd failed OSStatus=" + std::to_string(static_cast<int>(st));
            return false;
        }

        out32 = std::move(nk);
        return true;
    }

    bool Mm2DarwinDeleteMessageWrapKey(const std::string& indexDirUtf8, std::string& err)
    {
        err.clear();
        char acct[33]{};
        if (!MakeAccountHexFromIndexDir(indexDirUtf8, acct, err)) {
            return false;
        }

        CFStringRef service = CFSTR("ZChatIM.MM2.mm2_message_key.wrap.v1");
        CFStringRef account =
            CFStringCreateWithCString(kCFAllocatorDefault, acct, kCFStringEncodingUTF8);
        if (account == nullptr) {
            err = "CFStringCreateWithCString(account) failed";
            return false;
        }

        const void* dkeys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* dvals[] = {kSecClassGenericPassword, service, account};
        CFDictionaryRef delQuery = CFDictionaryCreate(
            kCFAllocatorDefault,
            dkeys,
            dvals,
            sizeof(dkeys) / sizeof(dkeys[0]),
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        if (delQuery == nullptr) {
            CFRelease(account);
            err = "CFDictionaryCreate(delete) failed";
            return false;
        }

        OSStatus st = SecItemDelete(delQuery);
        CFRelease(delQuery);
        CFRelease(account);

        if (st != errSecSuccess && st != errSecItemNotFound) {
            err = "SecItemDelete failed OSStatus=" + std::to_string(static_cast<int>(st));
            return false;
        }
        return true;
    }

} // namespace ZChatIM::mm2::detail
