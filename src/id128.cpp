#include "id128.h"

#include <blake3.h>

namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford base32

// A character's 5-bit value, or −1 (no I, L, O or U; either case).
int digitValue(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (int v = 0; v < 32; ++v)
        if (kAlphabet[v] == c) return v;
    return -1;
}

}  // namespace

std::uint64_t Id128::high() const {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | bytes[static_cast<std::size_t>(i)];
    return v;
}

std::uint64_t Id128::low() const {
    std::uint64_t v = 0;
    for (int i = 8; i < 16; ++i) v = (v << 8) | bytes[static_cast<std::size_t>(i)];
    return v;
}

Id128 Id128::fromParts(std::uint64_t high, std::uint64_t low) {
    Id128 id;
    for (int i = 7; i >= 0; --i, high >>= 8) id.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(high);
    for (int i = 15; i >= 8; --i, low >>= 8) id.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(low);
    return id;
}

// 128 bits as 26 digits of 5 bits: the first digit holds the top 3 bits.
std::string Id128::text() const {
    std::string out(26, '0');
    const unsigned __int128 v = (static_cast<unsigned __int128>(high()) << 64) | low();
    for (int i = 25; i >= 0; --i)
        out[static_cast<std::size_t>(i)] = kAlphabet[static_cast<unsigned>((v >> (5 * (25 - i))) & 31u)];
    return out;
}

std::optional<Id128> Id128::parse(std::string_view text) {
    if (text.size() != 26) return std::nullopt;
    unsigned __int128 v = 0;
    for (std::size_t i = 0; i < 26; ++i) {
        const int d = digitValue(text[i]);
        if (d < 0 || (i == 0 && d > 7)) return std::nullopt;
        v = (v << 5) | static_cast<unsigned>(d);
    }
    return fromParts(static_cast<std::uint64_t>(v >> 64), static_cast<std::uint64_t>(v));
}

std::array<std::uint8_t, 32> blake3DeriveKey256(std::string_view context, std::string_view material) {
    blake3_hasher hasher;
    blake3_hasher_init_derive_key_raw(&hasher, context.data(), context.size());
    blake3_hasher_update(&hasher, material.data(), material.size());
    std::array<std::uint8_t, 32> out{};
    blake3_hasher_finalize(&hasher, out.data(), out.size());
    return out;
}

Id128 blake3DeriveKey128(std::string_view context, std::string_view material) {
    const auto full = blake3DeriveKey256(context, material);
    Id128 id;
    for (std::size_t i = 0; i < 16; ++i) id.bytes[i] = full[i];
    return id;
}
