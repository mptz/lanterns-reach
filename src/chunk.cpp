#include "chunk.h"

#include <algorithm>

ChunkData::ChunkData() : brick_(std::make_unique<UniformBrick<BlockState>>(BlockState::air())) {}

ChunkData::ChunkData(const ChunkData& o) : brick_(o.brick_->clone()), digest_(o.digest_), growths_(o.growths_) {}

ChunkData& ChunkData::operator=(const ChunkData& o) {
    if (this != &o) {
        brick_ = o.brick_->clone();
        lookup_.drop();
        digest_ = o.digest_;
        growths_ = o.growths_;
    }
    return *this;
}

std::optional<BlockState> ChunkData::uniformState() const {
    if (brick_->width() != 0) return std::nullopt;
    return static_cast<const UniformBrick<BlockState>&>(*brick_).state();
}

std::uint64_t ChunkData::digest() const {
    if (!digest_) digest_ = brick_->fullDigest();
    return *digest_;
}

bool ChunkData::set(const glm::ivec3& local, const BlockState& state) {
    const auto cell = static_cast<std::uint32_t>(brickCellIndex(local.x, local.y, local.z));
    const BlockState old = brick_->at(cell);
    if (old == state) return false;
    if (brick_->width() == 0) {
        // Promotion: a uniform chunk edited to a second state becomes a
        // palette at 1 bit, all indices 0 = the old state.
        auto p = makePaletteBrick<BlockState>(1);
        p->append(old);
        p->setIndex(cell, p->append(state));
        brick_ = std::move(p);
        lookup_.drop();
    } else {
        PaletteBrick<BlockState>* p = &palette();
        std::uint32_t index = lookup_.find(*p, state);
        if (index == kNoIndex) {
            if (p->full()) {
                // Growth: a new Brick at the next width (or capacity), swapped
                // in. No scan for dead entries; the old Brick is freed here.
                brick_ = p->widened();
                p = &palette();
                lookup_.drop();
                ++growths_;
            }
            index = p->append(state);
            lookup_.inserted(*p, index);
        }
        p->setIndex(cell, index);
    }
    if (digest_) digest_ = addMod(*digest_, mulMod(digestWeight(cell), subMod(digestStateHash(canonicalEncoding(state)),
                                                                              digestStateHash(canonicalEncoding(old)))));
    return true;
}

std::uint32_t ChunkData::liveEntries() const {
    if (brick_->width() == 0) return 1;
    const PaletteBrick<BlockState>& p = palette();
    std::vector<std::uint8_t> live(p.entries(), 0);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) live[p.indexAt(c)] = 1;
    return static_cast<std::uint32_t>(std::count(live.begin(), live.end(), std::uint8_t{1}));
}

void ChunkData::rebuild(unsigned width, bool keepDead) {
    if (brick_->width() == 0) {
        if (width == 0) return;
        const BlockState s = static_cast<const UniformBrick<BlockState>&>(*brick_).state();
        auto q = makePaletteBrick<BlockState>(width);
        q->append(s);
        brick_ = std::move(q);
        lookup_.drop();
        return;
    }
    const PaletteBrick<BlockState>& p = palette();
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
        brick_ = std::make_unique<UniformBrick<BlockState>>(p.entry(s));
        lookup_.drop();
        return;
    }
    std::uint32_t capacity32 = kBrickCapacity32;
    if (width == 32) capacity32 = brickCapacity32For(n);
    FATAL_UNLESS(n <= (width == 32 ? capacity32 : brickPaletteCapacity(width)), "width too narrow for the live entries");
    auto q = makePaletteBrick<BlockState>(width, capacity32);
    std::vector<std::uint32_t> remap(p.entries(), 0);
    for (std::uint32_t s = 0; s < p.entries(); ++s)
        if (live[s]) remap[s] = q->append(p.entry(s));
    q->importIndices(idx.data(), remap.data());
    brick_ = std::move(q);  // the old Brick is freed here
    lookup_.drop();
}

void ChunkData::compact() {
    const std::uint32_t live = liveEntries();
    rebuild(live == 1 ? 0u : brickCompactedWidth(live), false);
}

void ChunkData::finishBulkFill() { compact(); }

std::uint32_t ChunkTestAccess::paletteSize(const ChunkData& c) {
    return c.brick_->width() == 0 ? 1u : c.palette().entries();
}

std::uint32_t ChunkTestAccess::paletteCapacity(const ChunkData& c) {
    return c.brick_->width() == 0 ? 1u : c.palette().capacity();
}

void ChunkTestAccess::forceWidth(ChunkData& c, unsigned width) {
    // Keep dead entries when the palette fits the new width (a repack), else compact.
    const bool fits = c.brick_->width() != 0 && width != 0 && c.palette().entries() <= brickPaletteCapacity(width);
    c.rebuild(width, fits);
}
