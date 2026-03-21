// Implemented in tests/mm1_managers_test.cpp (forward decl avoids include-path issues)
int RunMM1ManagerTests();

#include <cstring>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--test") == 0) {
        const int failed = RunMM1ManagerTests();
        return failed != 0 ? 1 : 0;
    }

    std::cout << "ZChatIM C++ core stub.\n";
    std::cout << "Run self-tests: ZChatIM --test\n";
    std::cout << "JNI shared library target: ZChatIMJNI.\n";
    return 0;
}
