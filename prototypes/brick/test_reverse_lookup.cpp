// Every reverse-lookup candidate agrees with a linear scan through inserts,
// rebuilds and misses, across tail merges and table regrowths.
#include "../common/check.hpp"
#include "reverse_lookup.hpp"

#include <random>
#include <unordered_set>
#include <vector>

using namespace proto::rl;

template <class S>
static void exercise(unsigned seed, std::uint32_t n, bool sequentialIds) {
    std::mt19937_64 rng(seed);
    std::vector<std::uint32_t> pal;
    std::unordered_set<std::uint32_t> used;
    while (pal.size() < n) {
        std::uint32_t id = sequentialIds ? 1000 + static_cast<std::uint32_t>(pal.size())
                                         : static_cast<std::uint32_t>(rng() % (1u << 26));
        if (used.insert(id).second) pal.push_back(id);
    }
    S s;
    std::uint32_t built = n / 3;
    s.rebuild(pal.data(), built);
    auto checkUpTo = [&](std::uint32_t m) {
        for (int q = 0; q < 200; ++q) {
            std::uint32_t i = static_cast<std::uint32_t>(rng() % m);
            CHECK(s.find(pal.data(), pal[i]) == i);
        }
        for (int q = 0; q < 200; ++q) {
            std::uint32_t id = (1u << 27) + static_cast<std::uint32_t>(rng() % 100000);  // never in the palette
            CHECK(s.find(pal.data(), id) == kNone);
        }
        for (std::uint32_t i = m; i < std::min<std::uint32_t>(n, m + 50); ++i) CHECK(s.find(pal.data(), pal[i]) == kNone);
    };
    if (built > 0) checkUpTo(built);
    for (std::uint32_t i = built; i < n; ++i) {
        s.insert(pal.data(), i);
        CHECK(s.find(pal.data(), pal[i]) == i);
        if ((i & (i - 1)) == 0 || i % 997 == 0) checkUpTo(i + 1);
    }
    checkUpTo(n);
    for (std::uint32_t i = 0; i < n; ++i) CHECK(s.find(pal.data(), pal[i]) == i);
    CHECK(scanSse2(pal.data(), n, pal[n - 1]) == n - 1);
}

template <class S>
static void exerciseAll() {
    unsigned seed = 1;
    for (std::uint32_t n : {1u, 2u, 15u, 16u, 17u, 100u, 1000u, 5000u, 70000u}) {
        exercise<S>(seed++, n, false);
        exercise<S>(seed++, n, true);
    }
}

int main() {
    exerciseAll<Sorted>();
    exerciseAll<SortedTail>();
    exerciseAll<EytzingerTail>();
    exerciseAll<LinearPairs>();
    exerciseAll<LinearIdx>();
    exerciseAll<LinearIdxAt<3, 4>>();
    exerciseAll<LinearTagged<1, 2>>();
    exerciseAll<LinearTagged<3, 4>>();
    exerciseAll<Swiss>();
    std::vector<std::uint32_t> pal(1000);
    for (std::uint32_t i = 0; i < 1000; ++i) pal[i] = i * 7 + 3;
    for (std::uint32_t n : {0u, 1u, 15u, 16u, 33u, 1000u})
        for (std::uint32_t i = 0; i < n; ++i) {
            CHECK(scanSse2(pal.data(), n, pal[i]) == i);
#if defined(__x86_64__)
            if (__builtin_cpu_supports("avx2")) CHECK(scanAvx2(pal.data(), n, pal[i]) == i);
#endif
        }
    return check::result("test_reverse_lookup");
}
