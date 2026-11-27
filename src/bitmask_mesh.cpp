#include "bitmask_mesh.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>

#include <glm/vector_relational.hpp>

#include "block_mesh.h"
#include "cube.h"
#include "fatal.h"

namespace {

using orient::Face;

constexpr int kEdge = kChunkSize;                                       // 64 cells per edge
constexpr std::uint32_t kRows = static_cast<std::uint32_t>(kEdge * kEdge);  // rows (z, y) per mask: 4096
constexpr std::uint64_t kAllBits = ~std::uint64_t{0};

// A palette entry's visibility class (world/chunk-meshing, layer 1).
constexpr std::uint8_t kAir = 0, kOpaque = 1, kTranslucent = 2;

// Everything a mesh builds, kept per thread and reused: allocated on a thread's
// first mesh, grown only when a palette has more entries or translucent groups
// than before, never freed per call, never shared. No pointer into it outlives
// a call, and meshing is not re-entrant on one thread.
struct Work {
    std::vector<std::uint8_t> kind;         // per palette entry: air, opaque or translucent
    std::vector<std::uint32_t> appearance;  // per palette entry: six appearance ids
    std::vector<std::int16_t> slotOfEntry;  // per palette entry: its translucent group's slot, or −1
    std::vector<std::int16_t> slotOfType;   // per block type: a translucent group's slot, or −1
    std::vector<BlockTypeId> typeOfSlot;    // per group slot: its block type (the group)
    std::vector<std::uint64_t> opaque;      // O[z][y]: one word of x bits per row
    std::vector<std::uint64_t> groups;      // slot × 4096 words: G_g[z][y]
    std::vector<std::uint64_t> planeX;      // the ±x face planes after transposing: [x][z], bits y
    std::vector<std::uint64_t> faceWords;   // one row's translucent face words: [slot][6]
    bool busy = false;
    std::size_t allocations = 0;
    MeshTimings* timings = nullptr;
};
thread_local Work gWork;

template <class V>
void sizeAtLeast(Work& w, V& v, std::size_t n) {
    if (v.size() >= n) return;
    if (n > v.capacity()) ++w.allocations;
    v.resize(n);
}

struct Guard {
    explicit Guard(Work& w) : work(w) {
        FATAL_UNLESS(!work.busy, "meshing is not re-entrant on one thread");
        work.busy = true;
    }
    ~Guard() { work.busy = false; }
    Work& work;
};

// Lap timer for MeshTimings; free when no one is listening.
class Phases {
public:
    explicit Phases(MeshTimings* t) : t_(t) {
        if (t_) last_ = std::chrono::steady_clock::now();
    }
    void lap(double MeshTimings::* field) {
        if (!t_) return;
        const auto now = std::chrono::steady_clock::now();
        t_->*field += std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
    }

private:
    MeshTimings* t_;
    std::chrono::steady_clock::time_point last_;
};

std::uint64_t runMask(int bit, int width) {  // `width` set bits from `bit`; a full row is not a shift by 64
    return width >= 64 ? kAllBits : (((std::uint64_t{1} << width) - 1) << bit);
}

// The word of row (z, y) holding, per cell, whether the cell next to it in
// direction d is set in `plane` (rows (z, y)); outside the chunk, `outside`.
std::uint64_t neighbourWord(const std::uint64_t* plane, int d, int z, int y, bool outside) {
    const std::uint64_t fill = outside ? kAllBits : 0;
    const std::uint64_t w = plane[z * kEdge + y];
    switch (d) {
        case orient::PosX: return (w >> 1) | (fill & (std::uint64_t{1} << 63));
        case orient::NegX: return (w << 1) | (fill & 1);
        case orient::PosY: return y == kEdge - 1 ? fill : plane[z * kEdge + y + 1];
        case orient::NegY: return y == 0 ? fill : plane[z * kEdge + y - 1];
        case orient::PosZ: return z == kEdge - 1 ? fill : plane[(z + 1) * kEdge + y];
        default: return z == 0 ? fill : plane[(z - 1) * kEdge + y];
    }
}

// A face plane's axes: faces ±x run bits along y and rows along z (slice x);
// ±y bits along x, rows along z (slice y); ±z bits along x, rows along y
// (slice z).
constexpr int kBitAxis[6] = {1, 1, 0, 0, 0, 0};
constexpr int kRowAxis[6] = {2, 2, 2, 2, 1, 1};

glm::ivec3 cellOf(int d, int slice, int bit, int row) {
    glm::ivec3 c(0);
    c[(d / 2)] = slice;
    c[kBitAxis[d]] = bit;
    c[kRowAxis[d]] = row;
    return c;
}

// Appends one quad of width w along the face's bit axis and h along its row
// axis, its four vertices in the unit cube's order (so a 1×1 quad is exactly
// the per-cell rule's face), with its payload; and its four outline edges.
void appendQuad(WorldMesh& out, int face, const glm::ivec3& cell, int w, int h, const FacePayload& payload) {
    const auto& cv = cube::vertices();
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (int k = 0; k < 4; ++k) {
        const Vertex& v = cv[static_cast<std::size_t>(face * 4 + k)];
        glm::vec3 pos(static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z));
        for (int a = 0; a < 3; ++a) {
            const float scale = a == kBitAxis[face] ? static_cast<float>(w) : a == kRowAxis[face] ? static_cast<float>(h) : 1.0f;
            pos[a] += (v.pos[a] + 0.5f) * scale;
        }
        out.vertices.push_back({pos, v.normal, payload.layer, payload.material, payload.texInfo});
    }
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) out.indices.push_back(base + i);
    for (std::uint32_t k = 0; k < 4; ++k) {
        out.edgeLines.push_back(out.vertices[base + k].pos);
        out.edgeLines.push_back(out.vertices[base + (k + 1) % 4].pos);
    }
}

// One face direction's greedy merge. `rowsFor(slice, rows)` fills the slice's
// 64 row words (bits set where a face exists) and returns whether any is set;
// `idAt(slice, bit, row)` is the appearance id of the face there. The fixed
// rule: slices ascending, rows ascending, take the lowest set bit, extend
// along the bits while set and of equal id, then across the following rows
// while the same bits are set and every cell has that id, clearing what the
// quad covers. Deterministic, not optimal; quads are at most 64×64.
template <class RowsFn, class IdFn>
void greedyDirection(const MeshContext& ctx, Phases& phases, int d, bool merge, WorldMesh& out, RowsFn&& rowsFor,
                     IdFn&& idAt) {
    std::uint64_t rows[kEdge];
    for (int slice = 0; slice < kEdge; ++slice) {
        if (!rowsFor(slice, rows)) continue;
        phases.lap(&MeshTimings::culling);
        for (int r = 0; r < kEdge; ++r) {
            while (rows[r] != 0) {
                const int b = std::countr_zero(rows[r]);
                const std::uint32_t id = idAt(slice, b, r);
                int w = 1, h = 1;
                if (merge) {
                    while (b + w < kEdge && ((rows[r] >> (b + w)) & 1) != 0 && idAt(slice, b + w, r) == id) ++w;
                    const std::uint64_t mask = runMask(b, w);
                    while (r + h < kEdge && (rows[r + h] & mask) == mask) {
                        bool same = true;
                        for (int k = 0; k < w && same; ++k) same = idAt(slice, b + k, r + h) == id;
                        if (!same) break;
                        ++h;
                    }
                    for (int k = 0; k < h; ++k) rows[r + k] &= ~mask;
                } else {
                    rows[r] &= rows[r] - 1;
                }
                phases.lap(&MeshTimings::merging);
                appendQuad(out, d, cellOf(d, slice, b, r), w, h, ctx.states.payload(id));
                phases.lap(&MeshTimings::emission);
            }
        }
    }
}

// --- The translucent pass --------------------------------------------------

// Whether a cell is in group `slot` and shows a surface layer toward face d:
// its neighbour there isn't the same group, and is air or a translucent group
// of higher type id (never opaque). From the masks.
bool surfaceFace(const Work& w, int slot, int d, const glm::ivec3& c) {
    auto inside = [](const glm::ivec3& p) {
        return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < kEdge && p.y < kEdge && p.z < kEdge;
    };
    auto set = [&](const std::uint64_t* plane, const glm::ivec3& p) {
        return ((plane[p.z * kEdge + p.y] >> p.x) & 1) != 0;
    };
    const std::uint64_t* mine = &w.groups[static_cast<std::size_t>(slot) * kRows];
    if (!inside(c) || !set(mine, c)) return false;
    const glm::ivec3 n = c + orient::faceNormal(static_cast<Face>(d));
    if (!inside(n)) return true;  // outside the chunk is air
    if (set(mine, n) || set(w.opaque.data(), n)) return false;
    for (std::size_t h = 0; h < w.typeOfSlot.size(); ++h)
        if (static_cast<int>(h) != slot && set(&w.groups[h * kRows], n))
            return w.typeOfSlot[static_cast<std::size_t>(slot)] < w.typeOfSlot[h];
    return true;
}

// The connected-frame bits of a translucent face (block_mesh.h connectedTile):
// bit 4 the surface layer, bits 0–3 the framed edges (−R, +R, −U, +U), an
// edge being framed unless the neighbour across it shows a surface face too.
std::uint32_t frameBitsFromMasks(const Work& w, int slot, int d, const glm::ivec3& c) {
    if (!surfaceFace(w, slot, d, c)) return 0;
    const glm::ivec3 n = orient::faceNormal(static_cast<Face>(d));
    const glm::ivec3 up = n.z != 0 ? glm::ivec3(0, 1, 0) : glm::ivec3(0, 0, 1);
    const glm::ivec3 right(up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x);
    const glm::ivec3 sides[4] = {-right, right, -up, up};
    std::uint32_t bits = 16u;
    for (int i = 0; i < 4; ++i)
        if (!surfaceFace(w, slot, d, c + sides[i])) bits |= 1u << i;
    return bits;
}

// Appends the translucent faces, one quad per face, in the per-cell rule's
// order (cell index, then face index): every row's face words for every group
// are combined, the set bits visited in order, and each cell's faces emitted.
// `stateAt(cell index)` is the cell's state (a palette lookup, or the uniform
// state, which needs no cell read).
template <class StateAt>
void emitTranslucent(Work& w, const MeshContext& ctx, const ChunkId& id, WorldMesh& out, Phases& phases,
                     StateAt&& stateAt) {
    const std::size_t slots = w.typeOfSlot.size();
    sizeAtLeast(w, w.faceWords, slots * 6);
    for (int z = 0; z < kEdge; ++z)
        for (int y = 0; y < kEdge; ++y) {
            std::uint64_t any = 0;
            for (std::size_t s = 0; s < slots; ++s) {
                const std::uint64_t* g = &w.groups[s * kRows];
                for (int d = 0; d < 6; ++d) {
                    const std::uint64_t f = g[z * kEdge + y] & ~neighbourWord(g, d, z, y, false);
                    w.faceWords[s * 6 + static_cast<std::size_t>(d)] = f;
                    any |= f;
                }
            }
            phases.lap(&MeshTimings::culling);
            while (any != 0) {
                const int x = std::countr_zero(any);
                any &= any - 1;
                const StateId st = stateAt(static_cast<std::uint32_t>(brickCellIndex(x, y, z)));
                const auto slot = static_cast<std::size_t>(w.slotOfType[ctx.states.type(st)]);
                const BlockDef& def = *ctx.states.definition(st).def;
                const glm::ivec3 local(x, y, z);
                const CellPos world = cellAt(id.coord, local);
                const glm::vec3 corner(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                for (int d = 0; d < 6; ++d) {
                    if (((w.faceWords[slot * 6 + static_cast<std::size_t>(d)] >> x) & 1) == 0) continue;
                    const auto face = static_cast<Face>(d);
                    FaceTexture tex = resolvePositional(faceTexture(ctx.states, st, face), world, face);
                    if (tex.set == VariantSet::Connected && glm::any(glm::greaterThan(def.translucency->absorption, glm::vec3(0.0f))))
                        tex.layer += connectedTile(frameBitsFromMasks(w, static_cast<int>(slot), d, local), tex.turn);
                    const auto base = static_cast<std::uint32_t>(out.translucentVertices.size());
                    appendFace(out.translucentVertices, out.translucentIndices, tex, face, corner,
                               BlockRegistry::materialId(ctx.states.type(st)));
                    for (std::uint32_t k = 0; k < 4; ++k) {
                        out.edgeLines.push_back(out.translucentVertices[base + k].pos);
                        out.edgeLines.push_back(out.translucentVertices[base + (k + 1) % 4].pos);
                    }
                }
            }
            phases.lap(&MeshTimings::emission);
        }
}

// --- The palette path -------------------------------------------------------

std::int16_t slotFor(Work& w, BlockTypeId type) {
    if (w.slotOfType[type] < 0) {
        const std::size_t slot = w.typeOfSlot.size();
        if (w.typeOfSlot.size() == w.typeOfSlot.capacity()) ++w.allocations;
        w.typeOfSlot.push_back(type);
        sizeAtLeast(w, w.groups, (slot + 1) * kRows);
        std::fill_n(w.groups.begin() + static_cast<std::ptrdiff_t>(slot * kRows), kRows, std::uint64_t{0});
        w.slotOfType[type] = static_cast<std::int16_t>(slot);
    }
    return w.slotOfType[type];
}

void planeFaces(const Work& w, int d, int slice, std::uint64_t* rows, std::uint64_t& any) {
    // Rows of the plane for ±y and ±z (±x comes from the transposed planes).
    for (int r = 0; r < kEdge; ++r) {
        const int z = d >= 4 ? slice : r;
        const int y = d >= 4 ? r : slice;
        rows[r] = w.opaque[static_cast<std::size_t>(z * kEdge + y)] & ~neighbourWord(w.opaque.data(), d, z, y, false);
        any |= rows[r];
    }
}

template <unsigned B>
void meshAt(const PaletteBrickAt<StateId, B>& brick, const MeshContext& ctx, const ChunkId& id, bool merge,
            WorldMesh& out) {
    Work& w = gWork;
    Guard guard(w);
    Phases phases(w.timings);

    // One pass over the palette (never over cells): each entry's class and
    // six appearance ids, into tables indexed by palette index.
    const std::uint32_t entries = brick.entries();
    sizeAtLeast(w, w.kind, entries);
    sizeAtLeast(w, w.appearance, std::size_t{entries} * 6);
    sizeAtLeast(w, w.slotOfEntry, entries);
    sizeAtLeast(w, w.slotOfType, ctx.registry.defs().size());
    sizeAtLeast(w, w.opaque, kRows);
    std::fill(w.slotOfType.begin(), w.slotOfType.begin() + static_cast<std::ptrdiff_t>(ctx.registry.defs().size()), std::int16_t{-1});
    std::fill_n(w.slotOfEntry.begin(), entries, std::int16_t{-1});
    w.typeOfSlot.clear();
    for (std::uint32_t i = 0; i < entries; ++i) {
        const StateId st = brick.entry(i);
        if (ctx.states.isAir(st)) {
            w.kind[i] = kAir;
        } else if (ctx.states.translucent(st)) {
            w.kind[i] = kTranslucent;
        } else {
            w.kind[i] = kOpaque;
            for (int f = 0; f < 6; ++f) w.appearance[i * 6 + static_cast<std::size_t>(f)] = ctx.states.appearance(st, static_cast<Face>(f));
        }
    }

    // One pass over the cells, reading each index once: the opaque mask per
    // row and a mask per translucent group, recording which groups occur
    // (presence is taken from the cells; dead palette entries count for nothing).
    bool anyOpaque = false;
    for (int z = 0; z < kEdge; ++z)
        for (int y = 0; y < kEdge; ++y) {
            const auto row = static_cast<std::uint32_t>(z * kEdge + y);
            std::uint64_t o = 0;
            for (int x = 0; x < kEdge; ++x) {
                const std::uint32_t index = brick.indexAt(row * kEdge + static_cast<std::uint32_t>(x));
                const std::uint8_t k = w.kind[index];
                if (k == kOpaque) {
                    o |= std::uint64_t{1} << x;
                } else if (k == kTranslucent) {
                    std::int16_t slot = w.slotOfEntry[index];
                    if (slot < 0) w.slotOfEntry[index] = slot = slotFor(w, ctx.states.type(brick.entry(index)));
                    w.groups[static_cast<std::size_t>(slot) * kRows + row] |= std::uint64_t{1} << x;
                }
            }
            w.opaque[row] = o;
            anyOpaque = anyOpaque || o != 0;
        }
    phases.lap(&MeshTimings::masks);

    auto idAt = [&](int d) {
        return [&brick, &w, d](int slice, int bit, int row) {
            const glm::ivec3 c = cellOf(d, slice, bit, row);
            return w.appearance[static_cast<std::size_t>(brick.indexAt(static_cast<std::uint32_t>(brickCellIndex(c.x, c.y, c.z)))) * 6 +
                                static_cast<std::size_t>(d)];
        };
    };

    if (anyOpaque) {
        for (int d = 0; d < 6; ++d) {
            if (d < 2) {  // ±x: each z-layer of the face masks (rows y, bits x) is transposed (rows x, bits y)
                sizeAtLeast(w, w.planeX, kRows);
                for (int z = 0; z < kEdge; ++z) {
                    std::uint64_t a[kEdge];
                    for (int y = 0; y < kEdge; ++y)
                        a[y] = w.opaque[static_cast<std::size_t>(z * kEdge + y)] & ~neighbourWord(w.opaque.data(), d, z, y, false);
                    phases.lap(&MeshTimings::culling);
                    transpose64(a);
                    phases.lap(&MeshTimings::transposes);
                    for (int x = 0; x < kEdge; ++x) w.planeX[static_cast<std::size_t>(x * kEdge + z)] = a[x];
                }
                greedyDirection(
                    ctx, phases, d, merge, out,
                    [&](int slice, std::uint64_t* rows) {
                        std::uint64_t any = 0;
                        for (int r = 0; r < kEdge; ++r) any |= rows[r] = w.planeX[static_cast<std::size_t>(slice * kEdge + r)];
                        return any != 0;
                    },
                    idAt(d));
            } else {
                greedyDirection(
                    ctx, phases, d, merge, out,
                    [&](int slice, std::uint64_t* rows) {
                        std::uint64_t any = 0;
                        planeFaces(w, d, slice, rows, any);
                        return any != 0;
                    },
                    idAt(d));
            }
        }
    }

    if (!w.typeOfSlot.empty())
        emitTranslucent(w, ctx, id, out, phases,
                        [&brick](std::uint32_t cell) { return brick.entry(brick.indexAt(cell)); });
}

}  // namespace

void transpose64(std::uint64_t a[64]) {
    static constexpr std::uint64_t kMasks[6] = {0x00000000FFFFFFFFull, 0x0000FFFF0000FFFFull, 0x00FF00FF00FF00FFull,
                                                0x0F0F0F0F0F0F0F0Full, 0x3333333333333333ull, 0x5555555555555555ull};
    // Swap the off-diagonal j×j blocks of each 2j×2j block, for j = 32 … 1.
    for (int level = 0, j = 32; level < 6; ++level, j >>= 1)
        for (int k = 0; k < 64; ++k) {
            if ((k & j) != 0) continue;
            const std::uint64_t t = ((a[k] >> j) ^ a[k + j]) & kMasks[level];
            a[k + j] ^= t;
            a[k] ^= t << j;
        }
}

void bitmaskMeshPalette(const PaletteBrick<StateId>& brick, const MeshContext& context, const ChunkId& id,
                        bool merge, WorldMesh& out) {
    switch (brick.width()) {
        case 1: meshAt<1>(static_cast<const PaletteBrickAt<StateId, 1>&>(brick), context, id, merge, out); break;
        case 2: meshAt<2>(static_cast<const PaletteBrickAt<StateId, 2>&>(brick), context, id, merge, out); break;
        case 4: meshAt<4>(static_cast<const PaletteBrickAt<StateId, 4>&>(brick), context, id, merge, out); break;
        case 8: meshAt<8>(static_cast<const PaletteBrickAt<StateId, 8>&>(brick), context, id, merge, out); break;
        case 16: meshAt<16>(static_cast<const PaletteBrickAt<StateId, 16>&>(brick), context, id, merge, out); break;
        default: meshAt<32>(static_cast<const PaletteBrickAt<StateId, 32>&>(brick), context, id, merge, out); break;
    }
}

void bitmaskMeshUniform(StateId state, const MeshContext& context, const ChunkId& id, bool merge,
                        WorldMesh& out) {
    if (context.states.isAir(state)) return;
    Work& w = gWork;
    Guard guard(w);
    Phases phases(w.timings);
    if (!context.states.translucent(state)) {
        // Six sides: only the boundary slice has faces, every row full.
        for (int d = 0; d < 6; ++d) {
            const int boundary = d % 2 == 0 ? kEdge - 1 : 0;
            const std::uint32_t id0 = context.states.appearance(state, static_cast<Face>(d));
            greedyDirection(
                context, phases, d, merge, out,
                [&](int slice, std::uint64_t* rows) {
                    if (slice != boundary) return false;
                    std::fill_n(rows, kEdge, kAllBits);
                    return true;
                },
                [id0](int, int, int) { return id0; });
        }
        return;
    }
    // Translucent: the pass with the group mask set to all ones, no cell read.
    sizeAtLeast(w, w.slotOfType, context.registry.defs().size());
    sizeAtLeast(w, w.opaque, kRows);
    std::fill(w.slotOfType.begin(), w.slotOfType.begin() + static_cast<std::ptrdiff_t>(context.registry.defs().size()), std::int16_t{-1});
    w.typeOfSlot.clear();
    slotFor(w, context.states.type(state));
    std::fill_n(w.groups.begin(), kRows, kAllBits);
    std::fill_n(w.opaque.begin(), kRows, std::uint64_t{0});
    emitTranslucent(w, context, id, out, phases, [state](std::uint32_t) { return state; });
}

void setMeshTimings(MeshTimings* timings) { gWork.timings = timings; }

std::size_t meshWorkingAllocations() { return gWork.allocations; }

std::size_t meshWorkingBytes() {
    const Work& w = gWork;
    return w.kind.capacity() + w.appearance.capacity() * 4 + w.slotOfEntry.capacity() * 2 + w.slotOfType.capacity() * 2 +
           w.typeOfSlot.capacity() * sizeof(BlockTypeId) + (w.opaque.capacity() + w.groups.capacity() + w.planeX.capacity() + w.faceWords.capacity()) * 8;
}
