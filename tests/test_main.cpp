#include "test.h"

#include <cstring>

bool g_updateGolden = false;

int main(int argc, char** argv)
{
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--update-golden") == 0) g_updateGolden = true;
        else filter = argv[i];
    }

    int run = 0;
    for (const test::Case& c : test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = test::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        std::printf("[ %s ] %s\n", test::failures() == before ? " OK " : "FAIL", c.name);
        ++run;
    }
    std::printf("\n%d test(s), %d failure(s)\n", run, test::failures());
    return test::failures() == 0 ? 0 : 1;
}
