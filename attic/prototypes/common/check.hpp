// Minimal in-house test macros: CHECK records a failure and carries on,
// REQUIRE stops the test binary at once. main() returns checkResult().
#pragma once

#include <cstdio>
#include <cstdlib>

namespace check {
inline int& failures() {
    static int f = 0;
    return f;
}
inline int result(const char* name) {
    if (failures() != 0) {
        std::fprintf(stderr, "%s: %d check(s) FAILED\n", name, failures());
        return 1;
    }
    std::printf("%s: all checks passed\n", name);
    return 0;
}
}  // namespace check

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++::check::failures();                                                  \
        }                                                                           \
    } while (0)

#define REQUIRE(cond)                                                                 \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "%s:%d: REQUIRE failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                             \
        }                                                                             \
    } while (0)
