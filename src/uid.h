#pragma once
// The one identifier format (data/stable-identity): every durable id in the
// game is 128 bits, written as 26 characters of Crockford base32, most
// significant bits first (so the first character is 0–7), upper case on
// output, case-insensitive on input, with no I, L, O or U. ULIDs (minted, a
// timestamp then random bits) and state hashes (derived, a BLAKE3 content
// hash) are both this value type, so one parser, printer and key type serves
// both; the aliases say which one a value is.

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct Uid {
    std::array<std::uint8_t, 16> bytes{};  // big-endian: bytes[0] holds the most significant bits

    constexpr auto operator<=>(const Uid&) const = default;

    // Bits 0–63 and 64–127 as numbers.
    std::uint64_t high() const;
    std::uint64_t low() const;
    static Uid fromParts(std::uint64_t high, std::uint64_t low);

    // 26 characters of Crockford base32.
    std::string text() const;
    // The value of 26 characters of Crockford base32 (either case), or
    // nothing for any other length, a character outside the alphabet (I, L,
    // O and U included) or a first character above 7.
    static std::optional<Uid> parse(std::string_view text);
};

using Ulid = Uid;       // minted once: a definition, a scarce instance
using StateHash = Uid;  // derived: a block state's content hash (world/block-states)

// BLAKE3 in derive_key mode (deps/hashing): the first 16 bytes of the key
// derived from `material` under `context`. A context string names one use and
// its version, so hashes of different uses or versions never collide.
Uid blake3DeriveKey128(std::string_view context, std::string_view material);
// The full 32-byte output, for checking against BLAKE3's published vectors.
std::array<std::uint8_t, 32> blake3DeriveKey256(std::string_view context, std::string_view material);

// In-memory hashing for unordered containers (never persisted or compared
// across processes; deps/toolchain).
struct UidHash {
    std::size_t operator()(const Uid& id) const { return static_cast<std::size_t>(id.low() ^ (id.high() * 0x9E3779B97F4A7C15ull)); }
};
