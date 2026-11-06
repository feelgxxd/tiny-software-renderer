#pragma once
// Minimal test harness (no external dependencies).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& msg)
{
    std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str());
    ++failures();
}

} // namespace test

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST_CASE(name)                                                                      \
    static void TEST_CONCAT(test_fn_, __LINE__)();                                           \
    static test::Registrar TEST_CONCAT(test_reg_, __LINE__)(name, TEST_CONCAT(test_fn_, __LINE__)); \
    static void TEST_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) test::fail(__FILE__, __LINE__, #cond);         \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                          \
    do {                                                                                               \
        double va_ = (a), vb_ = (b);                                                                   \
        if (!(std::fabs(va_ - vb_) <= (eps)))                                                          \
            test::fail(__FILE__, __LINE__,                                                             \
                       std::string(#a " ~= " #b " (") + std::to_string(va_) + " vs " + std::to_string(vb_) + ")"); \
    } while (0)
