#pragma once
// Fatal checks that stay on in every build (data/cheap-invariants): a broken
// invariant aborts rather than corrupting the world. Tests install a handler
// that throws, so "this check fires" is testable in-process.

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using FatalHandler = void (*)(const char* what);

inline FatalHandler& fatalHandler() {
    static FatalHandler handler = nullptr;
    return handler;
}

[[noreturn]] inline void fatal(const char* what) {
    if (FatalHandler h = fatalHandler()) h(what);  // may throw (tests only)
    std::fprintf(stderr, "FATAL: %s\n", what);
    std::abort();
}

struct FatalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
inline void throwingFatalHandler(const char* what) { throw FatalError(what); }

#define FATAL_UNLESS(cond, what)      \
    do {                              \
        if (!(cond)) fatal(what);     \
    } while (0)
