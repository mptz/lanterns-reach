#pragma once
// Our own fixed 64-bit mix, never std::hash (deps/toolchain: no std::hash value
// leaves the process, and digests and seeded choices are compared across
// processes). SplitMix64's finaliser (Steele, Lea, Flood 2014), with its
// increment.

#include <cstdint>

constexpr std::uint64_t mix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
