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

void fillChunk(Chunk& data, const Cells& cells, bool finish) {
    for (std::uint32_t c = 0; c < kBrickCells; ++c) data.set(localOfIndex(c), cells[c]);
    if (finish) data.finishBulkFill();
}

namespace {
// The face axes as the mesher's planes use them (bit axis, row axis).
constexpr int kTestBitAxis[6] = {1, 1, 0, 0, 0, 0};
constexpr int kTestRowAxis[6] = {2, 2, 2, 2, 1, 1};

void decompose(const std::vector<MeshVertex>& verts, bool translucent, std::vector<UnitFace>& out) {
    for (std::size_t q = 0; q + 4 <= verts.size(); q += 4) {
        const MeshVertex* v = &verts[q];
        int dir = -1;
        for (int d = 0; d < 6; ++d)
            if (v[0].normal == glm::vec3(orient::faceNormal(static_cast<orient::Face>(d)))) dir = d;
        auto bad = [&] { out.push_back({0, 0, 0, -1, translucent, {}}); };
        if (dir < 0) {
            bad();
            continue;
        }
        const int a = dir / 2, b = kTestBitAxis[dir], r = kTestRowAxis[dir];
        glm::vec3 lo = v[0].pos, hi = v[0].pos;
        bool ok = true;
        for (int k = 0; k < 4; ++k) {
            lo = glm::min(lo, v[k].pos);
            hi = glm::max(hi, v[k].pos);
            ok = ok && v[k].normal == v[0].normal && v[k].layer == v[0].layer && v[k].material == v[0].material &&
                 v[k].texInfo == v[0].texInfo && v[k].pos[a] == v[0].pos[a];
        }
        // The four corners of the rectangle, once each, wound counter-clockwise from outside.
        const glm::vec3 e1 = v[1].pos - v[0].pos, e2 = v[2].pos - v[0].pos;
        ok = ok && glm::dot(glm::cross(e1, e2), v[0].normal) > 0.0f;
        for (int k = 0; k < 4; ++k) {
            const bool onB = v[k].pos[b] == lo[b] || v[k].pos[b] == hi[b], onR = v[k].pos[r] == lo[r] || v[k].pos[r] == hi[r];
            ok = ok && onB && onR;
            for (int m = k + 1; m < 4; ++m) ok = ok && !(v[k].pos == v[m].pos);
        }
        const float wf = hi[b] - lo[b], hf = hi[r] - lo[r];
        ok = ok && wf >= 1.0f && hf >= 1.0f && wf == std::floor(wf) && hf == std::floor(hf);
        if (!ok) {
            bad();
            continue;
        }
        const bool positive = dir % 2 == 0;
        glm::ivec3 base(0);
        base[a] = static_cast<int>(v[0].pos[a]) - (positive ? 1 : 0);
        for (int i = 0; i < static_cast<int>(wf); ++i)
            for (int j = 0; j < static_cast<int>(hf); ++j) {
                glm::ivec3 c = base;
                c[b] = static_cast<int>(lo[b]) + i;
                c[r] = static_cast<int>(lo[r]) + j;
                out.push_back({c.x, c.y, c.z, dir, translucent, {v[0].layer, v[0].material, v[0].texInfo}});
            }
    }
}
}  // namespace

std::vector<UnitFace> unitFaces(const WorldMesh& mesh) {
    std::vector<UnitFace> out;
    decompose(mesh.vertices, false, out);
    decompose(mesh.translucentVertices, true, out);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<QuadSize> quadSizes(const WorldMesh& mesh) {
    std::vector<QuadSize> out;
    for (const auto* verts : {&mesh.vertices, &mesh.translucentVertices})
        for (std::size_t q = 0; q + 4 <= verts->size(); q += 4) {
            const MeshVertex* v = &(*verts)[q];
            int dir = 0;
            for (int d = 0; d < 6; ++d)
                if (v[0].normal == glm::vec3(orient::faceNormal(static_cast<orient::Face>(d)))) dir = d;
            glm::vec3 lo = v[0].pos, hi = v[0].pos;
            for (int k = 1; k < 4; ++k) {
                lo = glm::min(lo, v[k].pos);
                hi = glm::max(hi, v[k].pos);
            }
            out.push_back({dir, static_cast<int>(hi[kTestBitAxis[dir]] - lo[kTestBitAxis[dir]]),
                           static_cast<int>(hi[kTestRowAxis[dir]] - lo[kTestRowAxis[dir]])});
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
    return a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.origin.z == b.origin.z && same(a.vertices, b.vertices) &&
           same(a.indices, b.indices) && same(a.translucentVertices, b.translucentVertices) &&
           same(a.translucentIndices, b.translucentIndices) && same(a.edgeLines, b.edgeLines);
}

const AppearanceTable& testAppearance() {
    static const AppearanceTable table(registry());
    return table;
}
MeshContext testContext() { return {registry(), testAppearance()}; }
