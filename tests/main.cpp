#include "TestHarness.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    using namespace atlas::test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;
    for (const auto& t : registry())
    {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = failureCount();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        ++ran;
        if (failureCount() == before) std::printf("[  OK  ] %s\n", t.name);
        else { std::printf("[ FAIL ] %s\n", t.name); ++failedTests; }
    }
    std::printf("\n==== %d tests, %d checks, %d failed tests (%d failed checks) ====\n",
                ran, checkCount(), failedTests, failureCount());
    return failureCount() == 0 ? 0 : 1;
}
