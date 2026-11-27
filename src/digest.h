#pragma once
// data/content-digest: a chunk's block content digest,
//
//   digest = Σ over cells c of  W(c) · H(state_c)   (mod p, p = 2⁶¹ − 1).
//
// W and H are our own fixed functions (SplitMix64's finaliser, reduced mod p),
// never std::hash, because digests are compared across processes later. W is
// defined on the cell's local coordinates, not on the Brick's storage layout.
// Uniform chunks have digest H(s) · K, K = Σ W(c), one constant.

#include <cstdint>

#include "hash.h"

inline constexpr std::uint64_t kDigestP = (std::uint64_t{1} << 61) - 1;

// Exact x mod p for any 64-bit x: x = hi·2⁶¹ + lo ≡ hi + lo (mod p).
constexpr std::uint64_t reduceMod(std::uint64_t x) {
    const std::uint64_t r = (x & kDigestP) + (x >> 61);  // ≤ p + 7
    return r >= kDigestP ? r - kDigestP : r;
}
constexpr std::uint64_t addMod(std::uint64_t a, std::uint64_t b) {  // a, b < p
    const std::uint64_t s = a + b;                                   // < 2⁶², no overflow
    return s >= kDigestP ? s - kDigestP : s;
}
constexpr std::uint64_t subMod(std::uint64_t a, std::uint64_t b) {  // a, b < p
    return a >= b ? a - b : a + kDigestP - b;
}
constexpr std::uint64_t mulMod(std::uint64_t a, std::uint64_t b) {  // a, b < p
    const unsigned __int128 z = static_cast<unsigned __int128>(a) * b;  // < 2¹²²
    const std::uint64_t lo = static_cast<std::uint64_t>(z) & kDigestP;
    const std::uint64_t hi = static_cast<std::uint64_t>(z >> 61);       // < 2⁶¹
    std::uint64_t r = lo + hi;                                           // < 2⁶²
    r = (r & kDigestP) + (r >> 61);
    return r >= kDigestP ? r - kDigestP : r;
}

// W(x, y, z) = 1 + (mix64(x | y << 6 | z << 12) mod (p − 1)): nonzero for
// every cell by construction (a zero weight would make a cell invisible).
constexpr std::uint64_t digestWeightAt(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return 1 + mix64(std::uint64_t{x} | std::uint64_t{y} << 6 | std::uint64_t{z} << 12) % (kDigestP - 1);
}
// W at a Brick layout index (x + 64·y + 64²·z). The layout packs the
// coordinates as W does today; this converts back explicitly, so a layout
// change edits only this function, never the digest's definition.
constexpr std::uint64_t digestWeight(std::uint32_t cell) {
    return digestWeightAt(cell & 63u, (cell >> 6) & 63u, (cell >> 12) & 63u);
}

// H(s), a state's term, comes from its state hash (StateTable::digestTerm):
// already uniform, so it is only reduced mod p.

inline constexpr std::uint32_t kDigestCells = 64u * 64u * 64u;

// K = Σ W(c) over a chunk: one constant for every chunk. A literal, so no
// first-use computation (0.7 ms) hides in the first uniform digest; the digest
// tests recompute it from W and compare.
inline constexpr std::uint64_t kDigestK = 11124190129113306ull;
inline std::uint64_t digestK() { return kDigestK; }
