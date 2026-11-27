#include "test_support.h"

const BlockRegistry& registry() {
    static const BlockRegistry reg = BlockRegistry::load(SANDBOX_ASSET_DIR "/blocks");
    return reg;
}

BlockState solidState() { return {*registry().find("stone"), orient::kIdentity}; }

BlockTypeId typeOf(const char* name) { return *registry().find(name); }

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

ChunkWorld worldWith(std::initializer_list<CellPos> solid) {
    ChunkWorld w(registry());
    EditSet e;
    for (const CellPos& c : solid) e.edits.push_back({c, solidState()});
    w.apply(e);
    return w;
}

std::optional<ToolTarget> aim(const WorldQuery& w, WorldPos eye, WorldPos at) {
    const auto hit = w.raycast(eye, at - eye, 64.0);
    if (!hit) return std::nullopt;
    return ToolTarget{*hit, eye};
}

namespace {
const std::vector<BlockState>& validStates() {
    static const std::vector<BlockState> states = [] {
        std::vector<BlockState> v{BlockState::air()};
        for (BlockTypeId t = 0; t < registry().defs().size(); ++t)
            for (int r = 0; r < orient::kRotCount; ++r) {
                const BlockState s = registry().state(t, static_cast<orient::Rot>(r));
                if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
            }
        return v;
    }();
    return states;
}
}  // namespace

std::uint32_t validStateCount() { return static_cast<std::uint32_t>(validStates().size()); }

BlockState testState(std::uint32_t k) {
    if (k < validStateCount()) return validStates()[k];
    const std::uint32_t j = k - validStateCount();
    const std::uint32_t type = static_cast<std::uint32_t>(registry().defs().size()) + j / 24;
    FATAL_UNLESS(type < BlockState::kAirType, "testState: out of synthetic states");
    return {static_cast<BlockTypeId>(type), static_cast<orient::Rot>(j % 24)};
}

glm::ivec3 localOfIndex(std::uint32_t cell) {
    return {static_cast<int>(cell & 63u), static_cast<int>((cell >> 6) & 63u), static_cast<int>(cell >> 12)};
}

std::uint64_t oracleDigest(const Cells& cells) {
    std::uint64_t d = 0;
    for (std::uint32_t c = 0; c < kBrickCells; ++c)
        d = addMod(d, mulMod(digestWeight(c), digestStateHash(canonicalEncoding(cells[c]))));
    return d;
}

Cells makeContent(std::uint32_t n, std::mt19937_64& rng, std::vector<std::uint32_t>& pool) {
    pool.clear();
    const std::uint32_t nv = validStateCount();
    const std::uint32_t base = n + 1 < nv ? static_cast<std::uint32_t>(rng() % (nv - n - 1)) : 0;
    const bool withAir = (rng() & 1) != 0;
    for (std::uint32_t k = 0; k < n; ++k) pool.push_back(k == 0 && withAir ? 0 : base + 1 + k);
    Cells cells(kBrickCells);
    const std::uint32_t ground = 16 + static_cast<std::uint32_t>(rng() % 32);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) {
        const std::uint32_t z = c / (kChunkSize * kChunkSize);
        std::uint32_t pick;
        if (z > ground && withAir) pick = 0;
        else if (rng() % 8 != 0) pick = std::min<std::uint32_t>(z % 4, n - 1);
        else pick = static_cast<std::uint32_t>(rng() % n);
        cells[c] = testState(pool[pick]);
    }
    // Every state appears at least once, at random cells.
    std::vector<std::uint32_t> perm(kBrickCells);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) perm[c] = c;
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t j = k + static_cast<std::uint32_t>(rng() % (kBrickCells - k));
        std::swap(perm[k], perm[j]);
        cells[perm[k]] = testState(pool[k]);
    }
    return cells;
}

Cells distinctContent(std::uint32_t n, std::uint32_t base) {
    Cells cells(kBrickCells);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) cells[c] = testState(base + c % n);
    return cells;
}

void fillChunk(ChunkData& data, const Cells& cells, bool finish) {
    for (std::uint32_t c = 0; c < kBrickCells; ++c) data.set(localOfIndex(c), cells[c]);
    if (finish) data.finishBulkFill();
}
