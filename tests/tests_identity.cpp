// Tests: the one identifier format and BLAKE3 (data/stable-identity,
// deps/hashing; versions.md v0.2.4 acceptance 3). Registered through
// sandbox_tests.cpp.

#include "id128.h"
#include "test_support.h"

namespace {

std::string hex(const std::uint8_t* p, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += digits[p[i] >> 4];
        s += digits[p[i] & 15];
    }
    return s;
}

void testText() {
    const Id128 zero{};
    CHECK(zero.text() == "00000000000000000000000000");
    const Id128 max = Id128::fromParts(~std::uint64_t{0}, ~std::uint64_t{0});
    CHECK(max.text() == "7ZZZZZZZZZZZZZZZZZZZZZZZZZ");
    CHECK(Id128::parse(zero.text()) == zero && Id128::parse(max.text()) == max);
    // A pinned value: 1 in the lowest bit, and a value spanning both halves.
    CHECK(Id128::fromParts(0, 1).text() == "00000000000000000000000001");
    CHECK(Id128::fromParts(1, 0).text() == "0000000000000G000000000000");
    std::mt19937_64 rng(17);
    bool ok = true;
    for (int i = 0; i < 2000; ++i) {
        const Id128 v = Id128::fromParts(rng(), rng());
        const std::string t = v.text();
        ok = ok && t.size() == 26 && t[0] <= '7' && Id128::parse(t) == v && v.high() == Id128::parse(t)->high();
        std::string lower = t;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        ok = ok && Id128::parse(lower) == v;  // case-insensitive on input
    }
    CHECK(ok);
    // Rejected: I, L, O, U (either case), other characters, wrong lengths, a first character above 7.
    for (const char* bad : {"0000000000000000000000000I", "0000000000000000000000000l", "0000000000000000000000000O",
                            "0000000000000000000000000u", "000000000000000000000000-0", "0000000000000000000000000",
                            "000000000000000000000000000", "80000000000000000000000000", "Z0000000000000000000000000", ""})
        CHECK(!Id128::parse(bad).has_value());
    CHECK(Id128::parse("7ZZZZZZZZZZZZZZZZZZZZZZZZZ").has_value());
}

// BLAKE3's published derive_key vectors (test_vectors/test_vectors.json at the
// pinned commit): input byte i is i mod 251.
void testBlake3Vectors() {
    const std::string_view context = "BLAKE3 2019-12-27 16:29:52 test vectors context";
    struct Vector {
        std::size_t length;
        const char* derived;
    };
    for (const Vector& v : {Vector{0, "2cc39783c223154fea8dfb7c1b1660f2ac2dcbd1c1de8277b0b0dd39b7e50d7d"},
                            Vector{1, "b3e2e340a117a499c6cf2398a19ee0d29cca2bb7404c73063382693bf66cb06c"},
                            Vector{1023, "74a16c1c3d44368a86e1ca6df64be6a2f64cce8f09220787450722d85725dea5"},
                            Vector{1024, "7356cd7720d5b66b6d0697eb3177d9f8d73a4a5c5e968896eb6a689684302706"},
                            Vector{1025, "effaa245f065fbf82ac186839a249707c3bddf6d3fdda22d1b95a3c970379bcb"}}) {
        std::string input(v.length, '\0');
        for (std::size_t i = 0; i < v.length; ++i) input[i] = static_cast<char>(i % 251);
        const auto out = blake3DeriveKey256(context, input);
        CHECK(hex(out.data(), out.size()) == v.derived);
        const Id128 truncated = blake3DeriveKey128(context, input);
        CHECK(hex(truncated.bytes.data(), 16) == std::string(v.derived).substr(0, 32));
    }
    // Another context, another key.
    CHECK(blake3DeriveKey128("a", "x") != blake3DeriveKey128("b", "x"));
}

}  // namespace

void runIdentityTests() {
    testText();
    testBlake3Vectors();
}
