#include "test_support.h"

const BlockRegistry& registry() {
    static const BlockRegistry reg = BlockRegistry::load(SANDBOX_ASSET_DIR "/blocks");
    return reg;
}

StateTable& testStates() { return registry().states(); }

StateId stateOf(const char* name, orient::Rot orientation) { return registry().state(*registry().find(name), orientation); }

StateId solidState() { return stateOf("stone"); }

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
const std::vector<StateId>& validStates() {
    static const std::vector<StateId> states = [] {
        std::vector<StateId> v{kAirState};
        for (BlockTypeId t = 0; t < registry().defs().size(); ++t)
            for (int r = 0; r < orient::kRotCount; ++r) {
                const StateId s = registry().state(t, static_cast<orient::Rot>(r));
                if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
            }
        return v;
    }();
    return states;
}
}  // namespace

std::uint32_t validStateCount() { return static_cast<std::uint32_t>(validStates().size()); }

StateId testState(std::uint32_t k) {
    if (k < validStateCount()) return validStates()[k];
    // Synthetic definitions in the shared table, each with one property of
    // 2¹⁰ values, added as needed; their states are interned on first sight.
    static std::vector<std::uint32_t> synthetic;
    const std::uint32_t j = k - validStateCount();
    const std::uint32_t which = j / StateTable::kMaxStatesPerDefinition;
    while (synthetic.size() <= which) {
        const auto n = static_cast<std::uint32_t>(synthetic.size());
        const auto type = static_cast<std::uint32_t>(registry().defs().size()) + n;
        FATAL_UNLESS(type < kAirType, "testState: out of synthetic states");
        PropertyDecl v{"v", {}, /*mandatory=*/true, 0};
        for (std::uint32_t i = 0; i < StateTable::kMaxStatesPerDefinition; ++i) v.values.push_back("v" + std::to_string(i));
        synthetic.push_back(testStates().addDefinition(Uid::fromParts(0x7E57000000000000ull, n), "synthetic_" + std::to_string(n),
                                                       static_cast<BlockTypeId>(type), nullptr, orient::Symmetry::Uniform, {v}, false));
    }
    const std::uint32_t value = j % StateTable::kMaxStatesPerDefinition;
    return testStates().intern(synthetic[which], orient::kIdentity, std::span<const std::uint32_t>(&value, 1));
}

glm::ivec3 localOfIndex(std::uint32_t cell) {
    return {static_cast<int>(cell & 63u), static_cast<int>((cell >> 6) & 63u), static_cast<int>(cell >> 12)};
}

std::uint64_t oracleDigest(const Cells& cells) {
    std::uint64_t d = 0;
    for (std::uint32_t c = 0; c < kBrickCells; ++c)
        d = addMod(d, mulMod(digestWeight(c), testStates().digestTerm(cells[c])));
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

void fillChunk(Chunk& data, const Cells& cells, bool finish) {
    for (std::uint32_t c = 0; c < kBrickCells; ++c) data.set(localOfIndex(c), cells[c]);
    if (finish) data.finishBulkFill();
}

namespace {
void decompose(const std::vector<std::uint64_t>& quads, bool translucent, std::vector<UnitFace>& out) {
    for (const std::uint64_t packed : quads) {
        const quad::Quad q = quad::unpack(packed);
        const int dir = static_cast<int>(q.face);
        const FacePayload payload = testStates().payload(q.appearance);
        const bool along = quad::kRightAlongBit[dir];
        for (int i = 0; i < q.w; ++i)
            for (int j = 0; j < q.h; ++j) {
                glm::ivec3 c = q.cell;
                c[quad::kBitAxis[dir]] += i;
                c[quad::kRowAxis[dir]] += j;
                // The unit face's framed edges: those on a framed side of the quad.
                const bool lowBit = i == 0, highBit = i == q.w - 1;
                std::uint32_t frame = 0;
                if ((along ? lowBit : highBit) && (q.sides & quad::kSideNegR)) frame |= quad::kSideNegR;
                if ((along ? highBit : lowBit) && (q.sides & quad::kSidePosR)) frame |= quad::kSidePosR;
                if (j == 0 && (q.sides & quad::kSideNegU)) frame |= quad::kSideNegU;
                if (j == q.h - 1 && (q.sides & quad::kSidePosU)) frame |= quad::kSidePosU;
                // Its corner bits: the quad's, at the quad's corner cells.
                std::uint32_t corners = 0;
                for (std::uint32_t k = 0; k < 4; ++k) {
                    const bool posR = (k & 1) != 0, posU = (k & 2) != 0;
                    const bool atR = posR ? (along ? highBit : lowBit) : (along ? lowBit : highBit);
                    const bool atU = posU ? j == q.h - 1 : j == 0;
                    if (atR && atU && (q.corners & (1u << k))) corners |= 1u << k;
                }
                const bool outside = c.x > 63 || c.y > 63 || c.z > 63;
                out.push_back({c.x, c.y, c.z, outside ? -1 : dir, translucent, payload, frame, corners});
            }
    }
}
}  // namespace

std::vector<UnitFace> unitFaces(const WorldMesh& mesh) {
    std::vector<UnitFace> out;
    decompose(mesh.quads, false, out);
    decompose(mesh.translucentQuads, true, out);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<QuadSize> quadSizes(const WorldMesh& mesh) {
    std::vector<QuadSize> out;
    for (const auto* quads : {&mesh.quads, &mesh.translucentQuads})
        for (const std::uint64_t packed : *quads) {
            const quad::Quad q = quad::unpack(packed);
            out.push_back({static_cast<int>(q.face), q.w, q.h});
        }
    return out;
}

WorldMesh meshWith(ChunkWorld& world, Mesher mesher) {
    const Mesher saved = world.mesher();
    world.setMesher(mesher);
    WorldMesh m = world.buildMesh(ChunkWorld::kChunk);
    world.setMesher(saved);
    return m;
}

bool sameMesh(const WorldMesh& a, const WorldMesh& b) {
    auto same = [](const auto& x, const auto& y) {
        return x.size() == y.size() && (x.empty() || std::memcmp(x.data(), y.data(), x.size() * sizeof(x[0])) == 0);
    };
    return a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.origin.z == b.origin.z && same(a.quads, b.quads) &&
           same(a.translucentQuads, b.translucentQuads);
}

MeshContext testContext() { return {registry(), registry().states()}; }
