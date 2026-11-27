// Discretionary compaction between saves: workloads x policies, with an
// exact oracle (per-state counts, kept by the harness, never by the Brick)
// for live entries. Reports palette size against live entries, memory,
// compactions and their cost over edit count.
// Usage: bench_compaction [edits]   (default 500000)
#include "brick.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace proto;
using namespace proto::test;
using TA = BrickTestAccess;
using B = Brick<State>;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::uint32_t kEditsPerTurn = 32;  // placeholder: edits a Brick takes per owner turn

// Stand-in state ids by role (stateK numbering: k = 1 + 24 * type + orientation).
State st(std::uint32_t type, std::uint32_t orient = 0) { return stateK(1 + 24 * type + orient); }
const State kAir{};
enum : std::uint32_t { kStone = 1, kDirt, kGrass, kOre0, kSand = 10, kCobble = 20, kTorch, kBuild0 = 30, kPaint0 = 1000 };

std::uint32_t c3(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return cellIndex(x & 63, y & 63, z & 63); }

// Generated terrain: stone with ore pockets, dirt, grass, air above a
// ground line around z = 32. 8 states.
Cells terrain(std::mt19937_64& rng, std::vector<std::uint32_t>& ground) {
    Cells cells(kBrickCells, kAir);
    ground.assign(64 * 64, 0);
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            double h = 32 + 4 * std::sin(x * 0.15) + 3 * std::cos(y * 0.11 + 1.0);
            std::uint32_t g = static_cast<std::uint32_t>(h);
            ground[x + 64 * y] = g;
            for (std::uint32_t z = 0; z <= g; ++z)
                cells[cellIndex(x, y, z)] = z == g ? st(kGrass) : (z + 3 >= g ? st(kDirt) : st(kStone));
        }
    for (int p = 0; p < 60; ++p) {  // ore pockets
        std::uint32_t ore = kOre0 + static_cast<std::uint32_t>(rng() % 4);
        std::uint32_t x = rng() % 64, y = rng() % 64, z = rng() % 24;
        for (int i = 0; i < 12; ++i) {
            cells[c3(x, y, z)] = st(ore);
            x += rng() % 3 - 1;
            y += rng() % 3 - 1;
            z = std::min<std::uint32_t>(23, z + rng() % 3 - 1);
        }
    }
    return cells;
}

struct Edit {
    std::uint32_t cell;
    State s;
};

struct Workload {
    std::string name;
    // Returns the next edit given the current oracle content.
    std::function<Edit(const Cells&, std::mt19937_64&)> next;
};

std::vector<Workload> workloads(const std::vector<std::uint32_t>& ground) {
    std::vector<Workload> w;
    // Building: 48 building states (12 types x 4 orientations), placed next to
    // the previous block (structures grow in walks), 15 % removals to air.
    {
        auto pos = std::make_shared<std::array<std::uint32_t, 3>>(std::array<std::uint32_t, 3>{32, 32, 45});
        w.push_back({"build", [pos, &ground](const Cells& cells, std::mt19937_64& rng) {
                         auto& p = *pos;
                         int axis = static_cast<int>(rng() % 3);
                         p[axis] = (p[axis] + (rng() & 1 ? 1 : 63)) & 63;
                         if (rng() % 200 == 0) p = {static_cast<std::uint32_t>(rng() % 64), static_cast<std::uint32_t>(rng() % 64), 0};
                         p[2] = std::max<std::uint32_t>(p[2], ground[p[0] + 64 * p[1]] + 1);
                         std::uint32_t c = c3(p[0], p[1], p[2]);
                         if (rng() % 100 < 15 && !(cells[c] == kAir)) return Edit{c, kAir};
                         return Edit{c, st(kBuild0 + static_cast<std::uint32_t>(rng() % 12), static_cast<std::uint32_t>(rng() % 4))};
                     }});
    }
    // Mining: tunnels through the ground (to air); a torch every ~10 edits
    // on the tunnel, cobblestone placed back 5 % of the time.
    {
        auto pos = std::make_shared<std::array<std::uint32_t, 3>>(std::array<std::uint32_t, 3>{10, 10, 20});
        w.push_back({"mine", [pos](const Cells&, std::mt19937_64& rng) {
                         auto& p = *pos;
                         int axis = static_cast<int>(rng() % 4);
                         if (axis == 3) axis = 0;
                         p[axis] = (p[axis] + (rng() % 5 == 0 ? 63 : 1)) & 63;
                         p[2] = std::min<std::uint32_t>(p[2], 28);
                         std::uint32_t c = c3(p[0], p[1], p[2]);
                         std::uint32_t r = static_cast<std::uint32_t>(rng() % 100);
                         if (r < 10) return Edit{c, st(kTorch)};
                         if (r < 15) return Edit{c, st(kCobble)};
                         return Edit{c, kAir};
                     }});
    }
    // Terrain edits: spheres of radius 2..6 around the ground line, half
    // explosions (air), half fills with one of 4 terrain materials.
    {
        struct S {
            std::vector<Edit> q;
            std::size_t i = 0;
        };
        auto s = std::make_shared<S>();
        w.push_back({"terrain", [s, &ground](const Cells&, std::mt19937_64& rng) {
                         if (s->i == s->q.size()) {
                             s->q.clear();
                             s->i = 0;
                             int x = static_cast<int>(rng() % 64), y = static_cast<int>(rng() % 64);
                             int z = static_cast<int>(ground[x + 64 * y]) + static_cast<int>(rng() % 9) - 4;
                             int r = 2 + static_cast<int>(rng() % 5);
                             State m = rng() & 1 ? kAir : st(kSand + static_cast<std::uint32_t>(rng() % 4));
                             for (int dz = -r; dz <= r; ++dz)
                                 for (int dy = -r; dy <= r; ++dy)
                                     for (int dx = -r; dx <= r; ++dx)
                                         if (dx * dx + dy * dy + dz * dz <= r * r && z + dz >= 0 && z + dz < 64 &&
                                             x + dx >= 0 && x + dx < 64 && y + dy >= 0 && y + dy < 64)
                                             s->q.push_back({cellIndex(x + dx, y + dy, z + dz), m});
                         }
                         return s->q[s->i++];
                     }});
    }
    // Palette painting: each edit gives a random cell of a region a brand-new
    // state, orphaning the old one.
    for (std::uint32_t side : {4u, 16u}) {
        auto next = std::make_shared<std::uint32_t>(0);
        w.push_back({"paint-" + std::to_string(side * side * side), [next, side](const Cells&, std::mt19937_64& rng) {
                         std::uint32_t c = c3(8 + rng() % side, 8 + rng() % side, 40 + rng() % side);
                         std::uint32_t k = (*next)++;
                         return Edit{c, stateK(1 + 24 * kPaint0 + k)};
                     }});
    }
    // Orientation sweep: 100 isolated blocks of distinct types, each edit
    // rotates one to its next of 24 orientations.
    {
        auto orient = std::make_shared<std::vector<std::uint32_t>>(100, 0);
        w.push_back({"orient-sweep", [orient](const Cells&, std::mt19937_64& rng) {
                         std::uint32_t b = static_cast<std::uint32_t>(rng() % 100);
                         std::uint32_t o = (*orient)[b] = ((*orient)[b] + 1) % 24;
                         std::uint32_t c = cellIndex(2 + 6 * (b % 10), 2 + 6 * (b / 10), 50);
                         return Edit{c, st(200 + b, o)};
                     }});
    }
    // Random states: any cell, a state from a pool of 2^20.
    w.push_back({"random", [](const Cells&, std::mt19937_64& rng) {
                     return Edit{static_cast<std::uint32_t>(rng() % kBrickCells),
                                 stateK(1 + 24 * 2000 + static_cast<std::uint32_t>(rng() % (1u << 20)))};
                 }});
    return w;
}

struct Policy {
    std::string name;
    CompactionPolicy p;
    int witnessMode = 2;  // gWitnessMode for the run
};

std::vector<Policy> policies() {
    std::vector<Policy> v;
    v.push_back({"none, no witness", {}, 0});
    v.push_back({"none, block witness", {}, 1});
    v.push_back({"none", {}, 2});
    CompactionPolicy a;
    a.kind = CompactionPolicy::Kind::Appends;
    a.floor = 4096;
    a.fraction = 0.5;
    v.push_back({"appends 4096/0.5", a, 0});
    CompactionPolicy aw = a;
    aw.widthClause = true;
    aw.headroom = 0.25;
    v.push_back({"appends 4096/0.5 +width", aw, 0});
    CompactionPolicy a1 = a;
    a1.floor = 1024;
    a1.fraction = 0.25;
    v.push_back({"appends 1024/0.25", a1, 0});
    CompactionPolicy a16 = a;
    a16.floor = 16384;
    v.push_back({"appends 16384/0.5", a16, 0});
    CompactionPolicy a4q = a;
    a4q.fraction = 0.25;
    v.push_back({"appends 4096/0.25", a4q, 0});
    CompactionPolicy w;
    w.kind = CompactionPolicy::Kind::Witness;
    w.floor = 4096;
    w.fraction = 0.5;
    v.push_back({"witness 4096/0.5", w, 2});
    CompactionPolicy ww = w;
    ww.widthClause = true;
    ww.headroom = 0.25;
    v.push_back({"witness 4096/0.5 +width", ww, 2});
    v.push_back({"witness(block) 4096/0.5 +width", ww, 1});
    CompactionPolicy ws = ww;
    ws.floor = 256;
    ws.fraction = 0.25;
    v.push_back({"witness 256/0.25 +width", ws, 2});
    return v;
}

struct Result {
    std::uint64_t edits = 0, compactions = 0, growths = 0, deaths = 0, possibleDeaths = 0;
    double editNs = 0, compactNs = 0, maxCompactNs = 0;
    std::uint32_t finalP = 0, finalL = 0, finalW = 0, maxDead = 0;
    std::size_t finalMem = 0, maxMem = 0;
    double maxRatio = 0;
    struct Sample {
        std::uint64_t edits;
        std::uint32_t p, l, w;
        std::size_t mem;
    };
    std::vector<Sample> series;
};

Result run(const Workload& wl, const CompactionPolicy& pol, std::uint64_t totalEdits, bool series) {
    std::mt19937_64 rng(42);
    std::vector<std::uint32_t> ground;
    Cells oracle = terrain(rng, ground);
    // Fresh closures per run (workload state lives in shared_ptrs).
    auto all = workloads(ground);
    Workload w = *std::find_if(all.begin(), all.end(), [&](const Workload& x) { return x.name == wl.name; });
    std::unordered_map<std::uint64_t, std::uint32_t> counts;
    for (const State& s : oracle) ++counts[canonicalEncoding(s)];
    B b;
    b.fill(oracle);
    Result r;
    std::vector<Edit> batch(kEditsPerTurn);
    const std::uint64_t checkpoints[] = {10000, 50000, 100000, 250000, 500000, 1000000, 2000000};
    std::size_t nextCp = 0;
    std::uint64_t possibleDeathsTotal = 0, lastSig = 0;
    while (r.edits < totalEdits) {
        for (auto& e : batch) {
            e = w.next(oracle, rng);
            const State old = oracle[e.cell];
            if (old == e.s) continue;
            oracle[e.cell] = e.s;
            if (--counts[canonicalEncoding(old)] == 0) {
                counts.erase(canonicalEncoding(old));
                ++r.deaths;
            }
            ++counts[canonicalEncoding(e.s)];
        }
        auto t0 = Clock::now();
        for (const auto& e : batch) b.setCell(e.cell, e.s);
        auto t1 = Clock::now();
        std::uint64_t sigBefore = b.signals().possibleDeaths;
        bool compacted = b.turnEnd(pol);
        auto t2 = Clock::now();
        r.editNs += std::chrono::duration<double, std::nano>(t1 - t0).count();
        if (compacted) {
            double ns = std::chrono::duration<double, std::nano>(t2 - t1).count();
            r.compactNs += ns;
            r.maxCompactNs = std::max(r.maxCompactNs, ns);
            ++r.compactions;
            possibleDeathsTotal += sigBefore;
            lastSig = 0;
        } else {
            lastSig = b.signals().possibleDeaths;
        }
        r.edits += kEditsPerTurn;
        const std::uint32_t p = TA::paletteSize(b);
        const std::uint32_t l = static_cast<std::uint32_t>(counts.size());
        const std::size_t mem = b.memoryBytes();
        r.maxMem = std::max(r.maxMem, mem);
        r.maxDead = std::max(r.maxDead, p - l);
        r.maxRatio = std::max(r.maxRatio, static_cast<double>(p) / l);
        if (series && nextCp < std::size(checkpoints) && r.edits >= checkpoints[nextCp]) {
            r.series.push_back({checkpoints[nextCp], p, l, TA::width(b), mem});
            ++nextCp;
        }
    }
    r.possibleDeaths = possibleDeathsTotal + lastSig;
    r.growths = TA::stats(b).growths;
    r.finalP = TA::paletteSize(b);
    r.finalL = static_cast<std::uint32_t>(counts.size());
    r.finalW = TA::width(b);
    r.finalMem = b.memoryBytes();
    if (TA::liveEntries(b) != r.finalL) {
        std::fprintf(stderr, "live mismatch: brick %u, oracle %u\n", TA::liveEntries(b), r.finalL);
        std::exit(1);
    }
    for (std::uint32_t c = 0; c < kBrickCells; c += 7)
        if (!(b.getCell(c) == oracle[c])) {
            std::fprintf(stderr, "content mismatch at %u\n", c);
            std::exit(1);
        }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t edits = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 500000;
    std::vector<std::uint32_t> ground;
    std::mt19937_64 rng0(42);
    (void)terrain(rng0, ground);
    auto wls = workloads(ground);
    auto pols = policies();
    std::printf("%llu edits per run, %u edits per turn, scan threshold %u\n", static_cast<unsigned long long>(edits),
                kEditsPerTurn, gReverseScanMax);
    std::printf("\n| workload | policy | final P | final L | width | mem KiB | max mem KiB | max dead | max P/L | "
                "compactions | growths | compact ms | max compact us | edit ns | compact share |\n"
                "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    std::vector<std::pair<std::string, Result>> series;
    for (const auto& wl : wls) {
        for (const auto& pol : pols) {
            bool wantSeries = pol.name == "none" || pol.name == "appends 4096/0.25";
            gWitnessMode = pol.witnessMode;
            Result r = run(wl, pol.p, edits, wantSeries);
            double total = r.editNs + r.compactNs;
            std::printf("| %s | %s | %u | %u | %u | %.0f | %.0f | %u | %.2f | %llu | %llu | %.1f | %.0f | %.1f | %.1f %% |\n",
                        wl.name.c_str(), pol.name.c_str(), r.finalP, r.finalL, r.finalW, r.finalMem / 1024.0,
                        r.maxMem / 1024.0, r.maxDead, r.maxRatio, static_cast<unsigned long long>(r.compactions),
                        static_cast<unsigned long long>(r.growths), r.compactNs / 1e6, r.maxCompactNs / 1e3,
                        r.editNs / static_cast<double>(r.edits), 100.0 * r.compactNs / total);
            if (pol.name == "none" || pol.name == "none, block witness")
                std::printf("|  | (%s: %llu possible deaths for %llu true deaths) | | | | | | | | | | | | | |\n",
                            pol.witnessMode == 1 ? "block witness" : "block+faces witness",
                            static_cast<unsigned long long>(r.possibleDeaths), static_cast<unsigned long long>(r.deaths));
            std::fflush(stdout);
            if (wantSeries) series.push_back({wl.name + " / " + pol.name, r});
        }
    }
    std::printf("\n== over edit count: palette size P / live L / width / memory KiB\n");
    std::printf("| run | 10k | 50k | 100k | 250k | 500k |\n|---|---|---|---|---|---|\n");
    for (const auto& [name, r] : series) {
        std::printf("| %s |", name.c_str());
        for (const auto& s : r.series)
            std::printf(" %u / %u / %u / %.0f |", s.p, s.l, s.w, s.mem / 1024.0);
        std::printf("\n");
    }
    return 0;
}
