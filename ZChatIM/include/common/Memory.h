#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace ZChatIM
{
	namespace common
	{
		// Thread-safe stats; caller serializes Free/Reallocate per block.
		class Memory {
		public:
			static void* Allocate(size_t size);

			static void Free(void* ptr);

			static void* Reallocate(void* ptr, size_t newSize);

			static void SecureZero(void* data, size_t length);

			static bool ConstantTimeCompare(const void* a, const void* b, size_t length);

			static void SecureCopy(void* dest, const void* src, size_t length);

			static void SecureFill(void* data, uint8_t value, size_t length);

			static bool LockMemory(void* ptr, size_t length);

			static bool UnlockMemory(void* ptr, size_t length);

			// Windows: 0=RO, 1=NOACCESS, else RW; non-Win: false.
			static bool ProtectMemory(void* ptr, size_t length, int protection);

			static bool IsMemoryAccessible(const void* ptr, size_t length);

			static bool IsMemoryInitialized(const void* ptr, size_t length);

			static bool IsMemoryZero(const void* ptr, size_t length);

			static size_t GetAllocatedSize();

			static size_t GetPeakMemoryUsage();

			static void ResetMemoryStats();

			static void EncryptMemory(void* data, size_t length, const uint8_t* key, size_t keyLength);

			static void DecryptMemory(void* data, size_t length, const uint8_t* key, size_t keyLength);

			static void* AlignMemory(void* ptr, size_t alignment);

			static void* AllocateAligned(size_t size, size_t alignment);

			static void FreeAligned(void* ptr);

		private:
			Memory() = delete;
			~Memory() = delete;
		};

		class MemoryGuard {
		public:
			MemoryGuard(void* ptr, size_t length);

			~MemoryGuard();

			MemoryGuard(const MemoryGuard&) = delete;
			MemoryGuard& operator=(const MemoryGuard&) = delete;

		private:
			void* m_ptr;
			size_t m_length;
		};

	} // namespace common
} // namespace ZChatIM
