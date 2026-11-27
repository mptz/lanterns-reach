// Reverse-lookup candidates (reverse_lookup.hpp) at palette sizes 8 .. 2^20:
// find (hit, miss), insert at size n, build from empty, rebuild, memory.
// Usage: bench_lookup [--quick]
#include "reverse_lookup.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <unordered_set>
#include <vector>

using namespace proto::rl;
using Clock = std::chrono::steady_clock;

static volatile std::uint64_t gSink;

static double nsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct Data {
    std::vector<std::uint32_t> pal, hits, misses;
};

// Distinct random ids below 2^26 (a large state table), plus query streams.
static Data makeData(std::uint32_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    Data d;
    std::unordered_set<std::uint32_t> used;
    used.reserve(n * 2);
    while (d.pal.size() < n) {
        std::uint32_t id = static_cast<std::uint32_t>(rng() % (1u << 26));
        if (used.insert(id).second) d.pal.push_back(id);
    }
    const std::uint32_t q = 1u << 20;
    d.hits.resize(q);
    d.misses.resize(q);
    for (std::uint32_t i = 0; i < q; ++i) {
        d.hits[i] = d.pal[rng() % n];
        d.misses[i] = (1u << 27) + static_cast<std::uint32_t>(rng() % (1u << 26));
    }
    return d;
}

struct Row {
    double hit, hitDep, miss, insertAt, buildEmpty, worstInsert, rebuild;
    std::size_t bytes;
};

template <class F>
static double perQuery(const Data& d, const std::vector<std::uint32_t>& qs, F&& find) {
    std::vector<double> ts;
    for (int rep = 0; rep < 3; ++rep) {
        std::uint64_t acc = 0;
        auto t0 = Clock::now();
        for (std::uint32_t id : qs) acc += find(id);
        ts.push_back(nsSince(t0) / static_cast<double>(qs.size()));
        gSink = acc;
    }
    (void)d;
    return median(ts);
}

// Latency: each query's id depends on the previous answer, so lookups
// cannot overlap (an edit does one lookup, then uses the result).
template <class F>
static double perQueryDep(const std::vector<std::uint32_t>& qs, F&& find) {
    std::vector<double> ts;
    const std::uint32_t mask = static_cast<std::uint32_t>(qs.size()) - 1;
    for (int rep = 0; rep < 3; ++rep) {
        std::uint32_t r = 0;
        std::uint64_t acc = 0;
        auto t0 = Clock::now();
        for (std::uint32_t i = 0; i < qs.size(); ++i) {
            r = find(qs[(i + (r & 1)) & mask]);
            acc += r;
        }
        ts.push_back(nsSince(t0) / static_cast<double>(qs.size()));
        gSink = acc;
    }
    return median(ts);
}

template <class S>
static Row measure(const Data& d, std::uint32_t n, bool buildFromEmpty) {
    Row r{};
    const std::uint32_t* pal = d.pal.data();
    {
        std::vector<double> ts;
        for (int rep = 0; rep < 5; ++rep) {
            S s;
            auto t0 = Clock::now();
            s.rebuild(pal, n);
            ts.push_back(nsSince(t0));
            gSink = s.bytes();
        }
        r.rebuild = median(ts);
    }
    S s;
    s.rebuild(pal, n);
    r.bytes = s.bytes();
    r.hit = perQuery(d, d.hits, [&](std::uint32_t id) { return s.find(pal, id); });
    r.hitDep = perQueryDep(d.hits, [&](std::uint32_t id) { return s.find(pal, id); });
    r.miss = perQuery(d, d.misses, [&](std::uint32_t id) { return s.find(pal, id); });
    {  // insert at size ~n: rebuild n-k, then insert k
        std::uint32_t k = std::min<std::uint32_t>(4096, n);
        std::vector<double> ts;
        for (int rep = 0; rep < 3; ++rep) {
            S t;
            t.rebuild(pal, n - k);
            auto t0 = Clock::now();
            for (std::uint32_t i = n - k; i < n; ++i) t.insert(pal, i);
            ts.push_back(nsSince(t0) / k);
            gSink = t.bytes();
        }
        r.insertAt = median(ts);
    }
    if (buildFromEmpty) {
        std::vector<double> ts;
        for (int rep = 0; rep < 3; ++rep) {
            S t;
            t.rebuild(pal, 0);
            auto t0 = Clock::now();
            for (std::uint32_t i = 0; i < n; ++i) t.insert(pal, i);
            ts.push_back(nsSince(t0) / n);
            gSink = t.bytes();
        }
        r.buildEmpty = median(ts);
        S t;  // the worst single insert while building from empty (a regrowth or a merge)
        t.rebuild(pal, 0);
        double worst = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            auto t0 = Clock::now();
            t.insert(pal, i);
            worst = std::max(worst, nsSince(t0));
        }
        r.worstInsert = worst;
    } else {
        r.buildEmpty = -1;
        r.worstInsert = -1;
    }
    return r;
}

template <class S>
static void row(const Data& d, std::uint32_t n) {
    bool fromEmpty = !(std::strcmp(S::kName, Sorted::kName) == 0 && n > 65536);
    Row r = measure<S>(d, n, fromEmpty);
    char be[32];
    if (r.buildEmpty < 0) std::snprintf(be, sizeof be, "     - |        -");
    else std::snprintf(be, sizeof be, "%6.1f | %8.1f", r.buildEmpty, r.worstInsert / 1000.0);
    std::printf("| %-26s | %8u | %6.1f | %6.1f | %6.1f | %8.1f | %s | %9.1f | %10zu | %5.1f |\n", S::kName, n, r.hit,
                r.hitDep, r.miss,
                r.insertAt, be, r.rebuild / 1000.0, r.bytes, static_cast<double>(r.bytes) / n);
}

int main(int argc, char** argv) {
    bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
    std::printf("== scan: ns per find (hit at a uniformly random index; miss scans all n)\n");
    std::printf("| n | sse2 hit | sse2 hit (dep) | sse2 miss | avx2 hit | avx2 miss | scalar miss | table hit (dep) | table miss |\n"
                "|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (std::uint32_t n : {4u, 8u, 16u, 24u, 32u, 48u, 64u, 96u, 128u, 192u, 256u, 512u, 1024u, 2048u, 4096u}) {
        Data d = makeData(n, n);
        const std::uint32_t* pal = d.pal.data();
        double sh = perQuery(d, d.hits, [&](std::uint32_t id) { return scanSse2(pal, n, id); });
        double shd = perQueryDep(d.hits, [&](std::uint32_t id) { return scanSse2(pal, n, id); });
        LinearIdx t;
        t.rebuild(pal, n);
        double thd = perQueryDep(d.hits, [&](std::uint32_t id) { return t.find(pal, id); });
        double tm = perQuery(d, d.misses, [&](std::uint32_t id) { return t.find(pal, id); });
        double sm = perQuery(d, d.misses, [&](std::uint32_t id) { return scanSse2(pal, n, id); });
        double ah = -1, am = -1;
#if defined(__x86_64__)
        if (__builtin_cpu_supports("avx2")) {
            ah = perQuery(d, d.hits, [&](std::uint32_t id) { return scanAvx2(pal, n, id); });
            am = perQuery(d, d.misses, [&](std::uint32_t id) { return scanAvx2(pal, n, id); });
        }
#endif
        double sc = perQuery(d, d.misses, [&](std::uint32_t id) {
            std::uint32_t r = kNone;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (pal[i] == id) {
                    r = i;
                    break;
                }
                __asm__ volatile("" ::: "memory");  // keep it scalar
            }
            return r;
        });
        std::printf("| %u | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f |\n", n, sh, shd, sm, ah, am, sc, thd, tm);
    }

    std::printf("\n== structures: find ns (hit, miss), insert ns at size n (mean of up to 4096), build from empty "
                "ns/insert, rebuild us, bytes\n");
    std::printf("| structure | n | hit | hit (dep) | miss | insert@n | build/ins | worst ins us | rebuild us | bytes | B/entry |\n"
                "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    std::vector<std::uint32_t> ns = {16, 64, 256, 1024, 4096, 16384, 65536, 262144, 524288, 1048576};
    if (quick) ns = {64, 4096, 262144};
    for (std::uint32_t n : ns) {
        Data d = makeData(n, 1000 + n);
        row<Sorted>(d, n);
        row<SortedTail>(d, n);
        row<EytzingerTail>(d, n);
        row<LinearPairs>(d, n);
        row<LinearIdx>(d, n);
        row<LinearIdxAt<3, 4>>(d, n);
        row<LinearTagged<1, 2>>(d, n);
        row<LinearTagged<3, 4>>(d, n);
        row<Swiss>(d, n);
    }

    std::printf("\n== hash tables at n = 3/4 of a power of two (load 3/8 at @1/2, 3/4 at @3/4)\n");
    std::printf("| structure | n | hit | hit (dep) | miss | insert@n | build/ins | worst ins us | rebuild us | bytes | B/entry |\n"
                "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (std::uint32_t n : {768u, 12288u, 196608u}) {
        Data d = makeData(n, 2000 + n);
        row<LinearIdx>(d, n);
        row<LinearIdxAt<3, 4>>(d, n);
        row<LinearTagged<1, 2>>(d, n);
        row<LinearTagged<3, 4>>(d, n);
        row<Swiss>(d, n);
    }

    std::printf("\n== sequential ids (one block's states interned together): hash tables only, n = 2^18\n");
    {
        std::uint32_t n = 262144;
        Data d = makeData(n, 7);
        for (std::uint32_t i = 0; i < n; ++i) d.pal[i] = 5000 + i;
        std::mt19937_64 rng(3);
        for (auto& h : d.hits) h = d.pal[rng() % n];
        std::printf("| structure | n | hit | hit (dep) | miss | insert@n | build/ins | worst ins us | rebuild us | bytes | B/entry |\n"
                    "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
        row<LinearPairs>(d, n);
        row<LinearIdx>(d, n);
        row<LinearTagged<1, 2>>(d, n);
        row<Swiss>(d, n);
    }
    return 0;
}
