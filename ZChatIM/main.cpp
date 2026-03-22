#include <cstring>
#include <iostream>

#if defined(ZCHATIM_NO_INTREE_TESTS)
namespace {
	int RunMM1ManagerTests()
	{
		std::cerr << "ZChatIM: in-tree tests not compiled (configure with -DZCHATIM_BUILD_TESTS=ON and rebuild).\n";
		return 2;
	}
	int RunIm1kStressTests()
	{
		std::cerr << "ZChatIM: in-tree tests not compiled (configure with -DZCHATIM_BUILD_TESTS=ON and rebuild).\n";
		return 2;
	}
} // namespace
#else
// Tests: common_tools_test, mm1_managers_test (内含 MM2-50 + jni_im_smoke + jni_local_register_rtc), ...
int RunMM1ManagerTests();
int RunIm1kStressTests();
#endif

int main(int argc, char* argv[])
{
	if (argc > 1 && std::strcmp(argv[1], "--test") == 0) {
		const int failed = RunMM1ManagerTests();
		return failed != 0 ? 1 : 0;
	}

	if (argc > 1 && std::strcmp(argv[1], "--test-im-1k") == 0) {
		const int failed = RunIm1kStressTests();
		return failed != 0 ? 1 : 0;
	}

	if (argc > 1 && std::strncmp(argv[1], "--test", 6) == 0) {
		std::cerr << "Unknown option: " << argv[1] << "\n";
		std::cerr << "Use: --test  (full suite: common + MM1/MM2 + minimal MM2 + MM2-50 + JNI IM smoke + JNI local/RTC)\n";
		std::cerr << "     --test-im-1k  (1000 JNI IM stress cases + unified failure report; needs OpenSSL EVP)\n";
		std::cerr << "If you expected a --test* flag, rebuild the ZChatIM target (CMake / VS Build).\n";
		return 2;
	}

	std::cout << "ZChatIM C++ core.\n";
	std::cout << "Technical documentation index: docs/README.md (repository root).\n";
	std::cout << "JNI shared library target: ZChatIMJNI.\n";
	std::cout << "Full in-tree tests: ZChatIM --test (requires tests built in).\n";
	std::cout << "IM stress pack: ZChatIM --test-im-1k\n";
	return 0;
}
