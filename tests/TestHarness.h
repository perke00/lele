#pragma once
// Minimal dependency-free test harness (no network => no Catch2/GoogleTest).

#include <cmath>
#include <cstdio>
#include <vector>

namespace atlas::test {

struct TestCase { const char* name; void (*fn)(); };

inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline int& failureCount() { static int f = 0; return f; }
inline int& checkCount()   { static int c = 0; return c; }

struct Registrar { Registrar(const char* n, void (*f)()) { registry().push_back({ n, f }); } };

} // namespace atlas::test

#define ATLAS_TEST(name)                                                         \
    static void name();                                                          \
    static ::atlas::test::Registrar atlasReg_##name(#name, name);                \
    static void name()

#define CHECK(cond)                                                              \
    do { ++::atlas::test::checkCount();                                          \
         if (!(cond)) { ++::atlas::test::failureCount();                         \
             std::printf("    FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); } \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
    do { ++::atlas::test::checkCount();                                          \
         const double va_ = (a), vb_ = (b), vt_ = (tol);                         \
         if (!(std::fabs(va_ - vb_) <= vt_)) { ++::atlas::test::failureCount();  \
             std::printf("    FAIL %s:%d  CHECK_NEAR(%s, %s, %s)  got %.6f vs %.6f (diff %.6f)\n", \
                         __FILE__, __LINE__, #a, #b, #tol, va_, vb_, std::fabs(va_ - vb_)); } \
    } while (0)

#define INFO(...) do { std::printf("    . "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
