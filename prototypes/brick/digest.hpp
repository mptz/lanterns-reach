// data/content-digest: digest = sum_c W(c) * H(s_c) mod p, p = 2^61 - 1.
// W and H are our own fixed functions (SplitMix64's finaliser, reduced mod
// p; W on coordinates, H over a domain-separated encoding), never std::hash.
#pragma once

#include <cstdint>

namespace proto {

inline constexpr std::uint64_t kP = (std::uint64_t{1} << 61) - 1;

// Exact x mod p for any 64-bit x: x = hi*2^61 + lo == hi + lo (mod p).
constexpr std::uint64_t reduceMod(std::uint64_t x) {
    std::uint64_t r = (x & kP) + (x >> 61);  // <= p + 7
    return r >= kP ? r - kP : r;
}
constexpr std::uint64_t addMod(std::uint64_t a, std::uint64_t b) {  // a, b < p
    std::uint64_t s = a + b;                                         // < 2^62, no overflow
    return s >= kP ? s - kP : s;
}
constexpr std::uint64_t subMod(std::uint64_t a, std::uint64_t b) {  // a, b < p
    return a >= b ? a - b : a + kP - b;
}
constexpr std::uint64_t mulMod(std::uint64_t a, std::uint64_t b) {  // a, b < p
    unsigned __int128 z = static_cast<unsigned __int128>(a) * b;     // < 2^122
    std::uint64_t lo = static_cast<std::uint64_t>(z) & kP;
    std::uint64_t hi = static_cast<std::uint64_t>(z >> 61);          // < 2^61
    std::uint64_t r = lo + hi;                                       // < 2^62
    r = (r & kP) + (r >> 61);
    return r >= kP ? r - kP : r;
}

// SplitMix64's finaliser (Steele, Lea, Flood 2014), with its increment.
constexpr std::uint64_t mix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Domain separation so H is not a plain mix of its encoding.
inline constexpr std::uint64_t kHDomain = 0x48415348535F5348ull;  // "HASHS_SH"

// W, as data/content-digest defines it, on the cell's local coordinates
// (z up), not on the Brick's storage layout:
//   W(x, y, z) = 1 + (mix64(x | y << 6 | z << 12) mod (p - 1)),
// so every weight is nonzero by construction (a zero weight would make a
// cell invisible to the digest).
constexpr std::uint64_t digestWAt(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return 1 + mix64(std::uint64_t{x} | std::uint64_t{y} << 6 | std::uint64_t{z} << 12) % (kP - 1);
}
// W at a layout index. The Brick's layout (x + 64y + 4096z) packs the
// coordinates as W does today, so this converts back explicitly: a layout
// change edits only this function, never the digest's definition.
constexpr std::uint64_t digestW(std::uint32_t cell) {
    return digestWAt(cell & 63u, (cell >> 6) & 63u, (cell >> 12) & 63u);
}
// H(s): the state's canonical 64-bit encoding mixed.
constexpr std::uint64_t digestH(std::uint64_t canonical) { return reduceMod(mix64(kHDomain ^ canonical)); }

inline constexpr std::uint32_t kDigestCells = 64u * 64u * 64u;

// K = sum_c W(c), one constant for every Brick.
inline std::uint64_t digestK() {
    static const std::uint64_t k = [] {
        std::uint64_t s = 0;
        for (std::uint32_t c = 0; c < kDigestCells; ++c) s = addMod(s, digestW(c));
        return s;
    }();
    return k;
}

}  // namespace proto
