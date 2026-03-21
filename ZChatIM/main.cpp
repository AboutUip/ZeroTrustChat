#include <cstring>
#include <iostream>

#if defined(ZCHATIM_NO_INTREE_TESTS)
namespace {
int RunMM1ManagerTests()
{
    std::cerr << "ZChatIM: in-tree tests not compiled (configure with -DZCHATIM_BUILD_TESTS=ON and rebuild).\n";
    return 2;
}
int RunMinimalScenarioTests()
{
    std::cerr << "ZChatIM: in-tree tests not compiled (configure with -DZCHATIM_BUILD_TESTS=ON and rebuild).\n";
    return 2;
}
int RunMM2FiftyScenarioTests()
{
    std::cerr << "ZChatIM: in-tree tests not compiled (configure with -DZCHATIM_BUILD_TESTS=ON and rebuild).\n";
    return 2;
}
} // namespace
#else
// Implemented in tests/mm1_managers_test.cpp / tests/mm2_fifty_scenarios_test.cpp
int RunMM1ManagerTests();
int RunMinimalScenarioTests();
int RunMM2FiftyScenarioTests();
#endif

int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--test-minimal") == 0) {
        const int failed = RunMinimalScenarioTests();
        return failed != 0 ? 1 : 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--test-mm250") == 0) {
        const int failed = RunMM2FiftyScenarioTests();
        return failed != 0 ? 1 : 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--test") == 0) {
        const int failed = RunMM1ManagerTests();
        return failed != 0 ? 1 : 0;
    }

    // Typos / stale exe: older builds did not recognize --test-mm250 and fell through here silently.
    if (argc > 1 && std::strncmp(argv[1], "--test", 6) == 0) {
        std::cerr << "Unknown option: " << argv[1] << "\n";
        std::cerr << "Use: --test | --test-minimal | --test-mm250\n";
        std::cerr << "If you expected --test-mm250, rebuild the ZChatIM target (CMake / VS Build).\n";
        return 2;
    }

    std::cout << "ZChatIM C++ core.\n";
    std::cout << "Technical documentation index: docs/README.md (repository root).\n";
    std::cout << "JNI shared library target: ZChatIMJNI.\n";
    return 0;
}
