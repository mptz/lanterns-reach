#pragma once
// Minimal in-house test macro (deps/testing): no framework. A failed CHECK
// prints its location and expression and marks the run failed; the test
// executable's exit status is the number of failures (capped).

#include <cstdio>

namespace check {
inline int& failures() {
    static int n = 0;
    return n;
}
}  // namespace check

// Variadic so brace-initializers with commas (CellPos{1, 2, 3}) need no
// extra parentheses.
#define CHECK(...)                                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
            ++check::failures();                                                          \
        }                                                                                 \
    } while (0)
