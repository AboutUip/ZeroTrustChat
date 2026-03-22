/**
 * Legacy TweetNaCl **`randombytes`**（BCrypt）。**ZChatIMCore 不编译** TweetNaCl；生产随机数统一 **OpenSSL `RAND_bytes`**。
 */
#if defined(_WIN32)

#    include <windows.h>
#    include <bcrypt.h>
#    include <stdint.h>
#    include <string.h>

#    ifndef NT_SUCCESS
#        define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#    endif

typedef unsigned char      u8;
typedef unsigned long long u64;

void randombytes(u8* x, u64 xlen)
{
    if (x == 0 || xlen == 0) {
        return;
    }
    while (xlen > 0) {
        ULONG chunk = (xlen > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (ULONG)xlen;
        NTSTATUS st = BCryptGenRandom(NULL, x, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!NT_SUCCESS(st)) {
            (void)memset(x, 0, (size_t)chunk);
            return;
        }
        x += chunk;
        xlen -= chunk;
    }
}

#endif /* _WIN32 */
