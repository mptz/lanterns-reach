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

// SplitMix64's finaliser (Steele, Lea, Flood 2014), with its increment.
constexpr std::uint64_t mix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
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

// H(s): the state's canonical 64-bit encoding, domain-separated and mixed,
// reduced mod p.
inline constexpr std::uint64_t kDigestHDomain = 0x48415348535F5348ull;
constexpr std::uint64_t digestStateHash(std::uint64_t canonicalEncoding) {
    return reduceMod(mix64(kDigestHDomain ^ canonicalEncoding));
}

inline constexpr std::uint32_t kDigestCells = 64u * 64u * 64u;

// K = Σ W(c) over a chunk: one constant for every chunk.
inline std::uint64_t digestK() {
    static const std::uint64_t k = [] {
        std::uint64_t s = 0;
        for (std::uint32_t c = 0; c < kDigestCells; ++c) s = addMod(s, digestWeight(c));
        return s;
    }();
    return k;
}
