#include "chunk.h"

#include <algorithm>
#include <cmath>
#include <limits>

Chunk::Chunk(const StateTable& states, ChunkId id)
    : states_(&states), id_(id), brick_(std::make_unique<UniformBrick<StateId>>(kAirState)) {}

Chunk::Chunk(const Chunk& o)
    : states_(o.states_), id_(o.id_), brick_(o.brick_->clone()), digest_(o.digest_), growths_(o.growths_),
      widenings_(o.widenings_), emitters_(o.emitters_) {}

Chunk& Chunk::operator=(const Chunk& o) {
    if (this != &o) {
        states_ = o.states_;
        id_ = o.id_;
        emitters_ = o.emitters_;
        brick_ = o.brick_->clone();
        lookup_.drop();
        digest_ = o.digest_;
        growths_ = o.growths_;
        widenings_ = o.widenings_;
    }
    return *this;
}

std::optional<StateId> Chunk::uniformState() const {
    if (brick_->width() != 0) return std::nullopt;
    return static_cast<const UniformBrick<StateId>&>(*brick_).state();
}

glm::vec3 Chunk::emission(StateId s) const {
    const BlockDef* def = states_->definition(s).def;
    if (!def) return glm::vec3(0.0f);
    return def->translucent() ? def->translucency->emission : def->material.emissive;
}

std::vector<Emitter> Chunk::emitters() const {
    std::vector<Emitter> out;
    out.reserve(emitters_.size());
    for (const CellPos& c : emitters_) out.push_back({c, emission(get(localOf(c)))});
    return out;
}

std::uint64_t Chunk::digest() const {
    if (!digest_) digest_ = brick_->fullDigest([this](StateId s) { return states_->digestTerm(s); });
    return *digest_;
}

bool Chunk::set(const glm::ivec3& local, StateId state) {
    const auto cell = static_cast<std::uint32_t>(brickCellIndex(local.x, local.y, local.z));
    const StateId old = brick_->at(cell);
    if (old == state) return false;
    if (brick_->width() == 0) {
        // Promotion: a uniform chunk edited to a second state becomes a
        // palette at 1 bit, all indices 0 = the old state.
        auto p = makePaletteBrick<StateId>(1, brickBuildCapacity(1, 2));
        p->append(old);
        p->setIndex(cell, p->append(state));
        brick_ = std::move(p);
        lookup_.drop();
    } else {
        PaletteBrick<StateId>* p = &palette();
        std::uint32_t index = lookup_.find(*p, state);
        if (index == kNoIndex) {
            if (p->full()) {
                // Growth: a new Brick at twice the capacity (or, at its
                // width's limit, the next width), swapped in. No scan for dead
                // entries; the old Brick is freed here.
                if (p->atWidthLimit()) ++widenings_;
                brick_ = p->grown();
                p = &palette();
                lookup_.drop();
                ++growths_;
            }
            index = p->append(state);
            lookup_.inserted(*p, index);
        }
        p->setIndex(cell, index);
    }
    if (digest_) digest_ = addMod(*digest_, mulMod(digestWeight(cell), subMod(states_->digestTerm(state), states_->digestTerm(old))));
    const CellPos world = cellAt(id_.coord, local);
    if (const glm::vec3 was = emission(old); was.x > 0.0f || was.y > 0.0f || was.z > 0.0f) emitters_.erase(world);
    if (const glm::vec3 now = emission(state); now.x > 0.0f || now.y > 0.0f || now.z > 0.0f) emitters_.insert(world);
    return true;
}

std::uint32_t Chunk::liveEntries() const {
    if (brick_->width() == 0) return 1;
    const PaletteBrick<StateId>& p = palette();
    std::vector<std::uint8_t> live(p.entries(), 0);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) live[p.indexAt(c)] = 1;
    return static_cast<std::uint32_t>(std::count(live.begin(), live.end(), std::uint8_t{1}));
}

void Chunk::rebuild(unsigned width, bool keepDead) {
    if (brick_->width() == 0) {
        if (width == 0) return;
        const StateId s = static_cast<const UniformBrick<StateId>&>(*brick_).state();
        auto q = makePaletteBrick<StateId>(width, brickBuildCapacity(width, 1));
        q->append(s);
        brick_ = std::move(q);
        lookup_.drop();
        return;
    }
    const PaletteBrick<StateId>& p = palette();
    std::vector<std::uint32_t> idx(kBrickCells);
    p.exportIndices(idx.data());
    std::vector<std::uint8_t> live(p.entries(), keepDead ? 1 : 0);
    if (!keepDead)
        for (std::uint32_t c = 0; c < kBrickCells; ++c) live[idx[c]] = 1;
    const auto n = static_cast<std::uint32_t>(std::count(live.begin(), live.end(), std::uint8_t{1}));
    if (width == 0) {
        FATAL_UNLESS(n == 1, "uniform needs exactly one live state");
        std::uint32_t s = 0;
        while (!live[s]) ++s;
        brick_ = std::make_unique<UniformBrick<StateId>>(p.entry(s));
        lookup_.drop();
        return;
    }
    FATAL_UNLESS(n <= brickIndexLimit(width), "width too narrow for the live entries");
    auto q = makePaletteBrick<StateId>(width, brickBuildCapacity(width, n));
    std::vector<std::uint32_t> remap(p.entries(), 0);
    for (std::uint32_t s = 0; s < p.entries(); ++s)
        if (live[s]) remap[s] = q->append(p.entry(s));
    q->importIndices(idx.data(), remap.data());
    brick_ = std::move(q);  // the old Brick is freed here
    lookup_.drop();
}

void Chunk::compact() {
    const std::uint32_t live = liveEntries();
    rebuild(live == 1 ? 0u : brickCompactedWidth(live), false);
}

void Chunk::finishBulkFill() { compact(); }

std::uint32_t ChunkTestAccess::paletteSize(const Chunk& c) {
    return c.brick_->width() == 0 ? 1u : c.palette().entries();
}

bool ChunkTestAccess::paletteDistinct(const Chunk& c) {
    if (c.brick_->width() == 0) return true;
    const PaletteBrick<StateId>& p = c.palette();
    std::vector<StateId> entries;
    entries.reserve(p.entries());
    for (std::uint32_t i = 0; i < p.entries(); ++i) entries.push_back(p.entry(i));
    std::sort(entries.begin(), entries.end());
    return std::adjacent_find(entries.begin(), entries.end()) == entries.end();
}

std::uint32_t ChunkTestAccess::paletteCapacity(const Chunk& c) {
    return c.brick_->width() == 0 ? 1u : c.palette().capacity();
}

void ChunkTestAccess::forceWidth(Chunk& c, unsigned width) {
    // Keep dead entries when the palette fits the new width (a repack), else compact.
    const bool fits = c.brick_->width() != 0 && width != 0 && c.palette().entries() <= brickIndexLimit(width);
    c.rebuild(width, fits);
}

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

// Amanatides–Woo DDA in chunk-local f64 coordinates. A ray starting outside
// the chunk is first clipped to its box (slab test) and enters through a face.
std::optional<RayHit> Chunk::raycast(const WorldPos& worldOrigin, const glm::dvec3& rawDir,
                                         double maxDistance) const {
    const double len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
    if (!(len > 0.0)) return std::nullopt;
    const glm::dvec3 d = rawDir / len;
    const glm::dvec3 o = worldOrigin - cellCorner(chunkOrigin(id_.coord));  // chunk-local
    const double s = static_cast<double>(kChunkSize);

    // Entry into [0, s]³.
    double tEnter = 0.0, tExit = kInf;
    int enterAxis = -1;
    for (int a = 0; a < 3; ++a) {
        if (d[a] == 0.0) {
            if (o[a] < 0.0 || o[a] >= s) return std::nullopt;  // parallel and outside this slab
            continue;
        }
        double t0 = (0.0 - o[a]) / d[a], t1 = (s - o[a]) / d[a];
        if (t0 > t1) std::swap(t0, t1);
        // An origin exactly on a face but outside the half-open cell range
        // (e.g. x == size) enters through that face at t = 0.
        const bool outsideSlab = o[a] < 0.0 || o[a] >= s;
        if (t0 > tEnter || (enterAxis < 0 && outsideSlab && t0 >= tEnter)) {
            tEnter = t0;
            enterAxis = a;
        }
        tExit = std::min(tExit, t1);
    }
    if (tEnter > tExit || tEnter > maxDistance || tExit <= 0.0) return std::nullopt;

    glm::dvec3 p = o + d * tEnter;
    glm::ivec3 step(0), normal(0);
    glm::i64vec3 cell;
    for (int a = 0; a < 3; ++a) {
        step[a] = d[a] > 0.0 ? 1 : (d[a] < 0.0 ? -1 : 0);
        if (a == enterAxis) p[a] = d[a] > 0.0 ? 0.0 : s;  // exactly on the entered face
        cell[a] = std::clamp(static_cast<std::int64_t>(std::floor(p[a])), std::int64_t{0},
                             static_cast<std::int64_t>(kChunkSize - 1));
    }
    if (enterAxis >= 0) normal[enterAxis] = -step[enterAxis];

    auto local = [&] { return glm::ivec3(static_cast<int>(cell.x), static_cast<int>(cell.y), static_cast<int>(cell.z)); };
    auto hitAt = [&](double t) {
        const CellPos c = cellAt(id_.coord, local());
        return RayHit{c, normal, cellCorner(chunkOrigin(id_.coord)) + o + d * t, get(local()), t};
    };
    if (!states_->isAir(get(local()))) return hitAt(tEnter);  // inside (normal 0) or the entered cell

    glm::dvec3 tMax, tDelta;
    for (int a = 0; a < 3; ++a) {
        if (step[a] == 0) {
            tMax[a] = kInf;
            tDelta[a] = kInf;
            continue;
        }
        const double boundary = static_cast<double>(cell[a] + (step[a] > 0 ? 1 : 0));
        tMax[a] = tEnter + (boundary - p[a]) / d[a];
        tDelta[a] = 1.0 / std::abs(d[a]);
    }
    for (;;) {
        // Smallest tMax; exact ties step x before y before z.
        int a = 0;
        if (tMax[1] < tMax[a]) a = 1;
        if (tMax[2] < tMax[a]) a = 2;
        const double t = tMax[a];
        if (t > maxDistance) return std::nullopt;
        cell[a] += step[a];
        if (cell[a] < 0 || cell[a] >= kChunkSize) return std::nullopt;  // left the chunk
        tMax[a] += tDelta[a];
        normal = glm::ivec3(0);
        normal[a] = -step[a];
        if (!states_->isAir(get(local()))) return hitAt(t);
    }
}
