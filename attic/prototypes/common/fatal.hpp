// Fatal checks, on in every build (data/cheap-invariants). Tests install a
// handler that throws, so "this assert fires" is testable in-process.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace proto {

using FatalHandler = void (*)(const char* what);

inline FatalHandler& fatalHandler() {
    static FatalHandler h = nullptr;
    return h;
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

}  // namespace proto

#define PROTO_ASSERT(cond, msg)                  \
    do {                                         \
        if (!(cond)) ::proto::fatal(msg);        \
    } while (0)
