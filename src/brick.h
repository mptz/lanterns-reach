#pragma once
// A Brick (world/brick): the dense, full-detail representation of one chunk's cells.
// Two representations, behind one interface:
//   - uniform: one entry fills the Brick, no per-cell storage;
//   - palette: bit-packed indices into a palette of entries, at an aligned
//     width of 1, 2, 4, 8, 16 or 32 bits (so an index never straddles a
//     64-bit word and an access is a shift and a mask), one instantiation
//     per width.
// Only chunk code (chunk.h) touches a Brick; nothing outside it sees cells,
// indices, widths or the palette (core/opaque-chunk-queries).
//
// Generic over its palette entry type E: E is regular and has a
// `std::uint64_t canonicalEncoding(const E&)` found by argument-dependent
// lookup (the digest and the reverse lookup hash it).
//
// A Brick is never resized in place. Its palette array has a fixed physical
// capacity, set when the Brick is built and independent of the index width
// (the width only limits how many entries the indices can name). Palettes keep
// no per-entry counts, so a Brick can't tell which entries are dead; an edit
// that needs an entry appends it (the entry is stored before any index that
// refers to it), and a full palette is replaced by `grown()`, a new Brick
// the owner swaps in: twice the capacity at the same width, or, once the
// capacity reaches what the width can name, the next width. The reverse
// lookup (entry to index) belongs to the owner, not the Brick.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/vec3.hpp>

#include "chunk_coord.h"
#include "digest.h"
#include "fatal.h"

inline constexpr std::uint32_t kBrickCells = 1u << (3 * kChunkBits);  // 2¹⁸
static_assert(kBrickCells == kDigestCells, "the digest's weights cover exactly a Brick's cells");

// Layout (world/brick): cell index = x + 64·y + 64²·z (z up), chosen for
// bitmask meshing. Behind this function; nothing else knows the formula.
constexpr std::size_t brickCellIndex(int x, int y, int z) {
    return static_cast<std::size_t>(x) + static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(y) +
           static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(z);
}

inline constexpr unsigned kBrickWidths[] = {1, 2, 4, 8, 16, 32};
inline constexpr std::uint32_t kNoIndex = 0xFFFFFFFFu;  // never a palette index
inline constexpr std::uint32_t kBrickMinCapacity = 16;  // a palette's smallest physical capacity (a placeholder)

// Entries the indices at storage width w can name (2^w; at 32 bits, every
// index but kNoIndex). A palette's physical capacity never exceeds it.
constexpr std::uint32_t brickIndexLimit(unsigned w) { return w >= 32 ? kNoIndex : std::uint32_t{1} << w; }

// The capacity a palette is built with to hold `entries`: the next power of
// two at or above them, at least kBrickMinCapacity, at most the width's limit.
inline std::uint32_t brickBuildCapacity(unsigned w, std::uint32_t entries) {
    const std::uint64_t want = std::bit_ceil(std::max<std::uint64_t>(kBrickMinCapacity, entries));
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(want, brickIndexLimit(w)));
}

// The smallest aligned width whose indices name `entries` at or below the
// low-water mark of 7/8 of their limit (a placeholder, world/brick).
inline unsigned brickCompactedWidth(std::uint32_t entries) {
    for (unsigned w : kBrickWidths) {
        if (w == 32) break;
        if (std::uint64_t{entries} * 8 <= std::uint64_t{brickIndexLimit(w)} * 7) return w;
    }
    return 32;
}

template <class E>
class Brick {
public:
    virtual ~Brick() = default;

    // The entry at a cell index (brickCellIndex).
    virtual const E& at(std::uint32_t cell) const = 0;
    // Storage width in bits; 0 is uniform.
    virtual unsigned width() const = 0;
    // The content digest by a full pass (data/content-digest).
    virtual std::uint64_t fullDigest() const = 0;
    virtual std::size_t indexBytes() const = 0;
    virtual std::size_t paletteBytes() const = 0;
    virtual std::unique_ptr<Brick> clone() const = 0;
};

template <class E>
class UniformBrick final : public Brick<E> {
public:
    explicit UniformBrick(const E& s) : s_(s) {}
    const E& at(std::uint32_t) const override { return s_; }
    unsigned width() const override { return 0; }
    std::uint64_t fullDigest() const override { return mulMod(digestStateHash(canonicalEncoding(s_)), digestK()); }
    std::size_t indexBytes() const override { return 0; }
    std::size_t paletteBytes() const override { return sizeof(E); }
    std::unique_ptr<Brick<E>> clone() const override { return std::make_unique<UniformBrick>(*this); }
    const E& state() const { return s_; }

private:
    E s_;
};

// The palette and what every width shares.
template <class E>
class PaletteBrick : public Brick<E> {
public:
    virtual std::uint32_t indexAt(std::uint32_t cell) const = 0;
    // Writes an index (fatal if it refers past the palette).
    virtual void setIndex(std::uint32_t cell, std::uint32_t index) = 0;
    // Every cell's index, in cell order (the export stream).
    virtual void exportIndices(std::uint32_t* out) const = 0;
    // Sets every cell's index to remap[in[cell]] (the build stream).
    virtual void importIndices(const std::uint32_t* in, const std::uint32_t* remap) = 0;
    // A new Brick with the same cells and palette, for a full palette: twice
    // the capacity at the same width, or, when the capacity already equals
    // what the width can name, the next width. The owner swaps it in and
    // retires this one, which is never written again.
    virtual std::unique_ptr<PaletteBrick> grown() const = 0;
    // Whether grown() changes the width (the palette is at its width's limit).
    bool atWidthLimit() const { return capacity_ >= brickIndexLimit(this->width()); }

    const E& at(std::uint32_t cell) const final { return palette_[indexAt(cell)]; }

    const E& entry(std::uint32_t i) const { return palette_[i]; }
    std::uint32_t entries() const { return size_; }
    std::uint32_t capacity() const { return capacity_; }
    bool full() const { return size_ >= capacity_; }

    // Appends an entry and returns its index; fatal when full. The entry is
    // stored before any index refers to it.
    std::uint32_t append(const E& e) {
        FATAL_UNLESS(!full(), "palette append when full");
        palette_[size_] = e;
        return size_++;
    }

    std::size_t paletteBytes() const final { return std::size_t{capacity_} * sizeof(E); }

protected:
    // `capacity` entries of storage, allocated once: the array never moves.
    PaletteBrick(unsigned width, std::uint32_t capacity)
        : palette_(std::make_unique<E[]>(capacity)), capacity_(capacity) {
        FATAL_UNLESS(capacity >= 1 && capacity <= brickIndexLimit(width), "palette capacity beyond its width's limit");
    }
    PaletteBrick(const PaletteBrick& o)
        : Brick<E>(o), palette_(std::make_unique<E[]>(o.capacity_)), size_(o.size_), capacity_(o.capacity_) {
        std::copy(o.palette_.get(), o.palette_.get() + o.size_, palette_.get());
    }
    // The grown Brick's palette: this one's entries, in a larger array.
    void copyPaletteTo(PaletteBrick& g) const {
        std::copy(palette_.get(), palette_.get() + size_, g.palette_.get());
        g.size_ = size_;
    }

private:
    std::unique_ptr<E[]> palette_;
    std::uint32_t size_ = 0;
    std::uint32_t capacity_;
};

template <class E, unsigned B>
class PaletteBrickAt final : public PaletteBrick<E> {
    static_assert(B == 1 || B == 2 || B == 4 || B == 8 || B == 16 || B == 32);
    static constexpr unsigned kPerWord = 64 / B;
    static constexpr unsigned kShift = static_cast<unsigned>(std::countr_zero(kPerWord));
    static constexpr std::uint64_t kMask = (std::uint64_t{1} << B) - 1;

public:
    explicit PaletteBrickAt(std::uint32_t capacity = kBrickMinCapacity)
        : PaletteBrick<E>(B, std::min(capacity, brickIndexLimit(B))), words_(kBrickCells / kPerWord, 0) {}

    // The width-specific accessors: a shift and a mask.
    std::uint32_t indexAt(std::uint32_t cell) const override {
        const std::uint64_t w = words_[cell >> kShift];
        const unsigned sh = (cell & (kPerWord - 1)) * B;
        return static_cast<std::uint32_t>((w >> sh) & kMask);
    }
    void setIndex(std::uint32_t cell, std::uint32_t index) override {
        FATAL_UNLESS(index < this->entries(), "index refers past the palette");
        put(cell, index);
    }
    unsigned width() const override { return B; }

    void exportIndices(std::uint32_t* out) const override {
        for (std::size_t wi = 0; wi < words_.size(); ++wi)
            for (unsigned j = 0; j < kPerWord; ++j)
                out[wi * kPerWord + j] = static_cast<std::uint32_t>((words_[wi] >> (j * B)) & kMask);
    }
    void importIndices(const std::uint32_t* in, const std::uint32_t* remap) override {
        for (std::size_t wi = 0; wi < words_.size(); ++wi) {
            std::uint64_t w = 0;
            for (unsigned j = 0; j < kPerWord; ++j)
                w |= static_cast<std::uint64_t>(remap[in[wi * kPerWord + j]]) << (j * B);
            words_[wi] = w;
        }
    }

    std::unique_ptr<PaletteBrick<E>> grown() const override {
        const std::uint64_t doubled = std::uint64_t{this->capacity()} * 2;
        if (!this->atWidthLimit()) {  // room in the width: a bigger palette, the same indices
            auto g = std::make_unique<PaletteBrickAt>(
                static_cast<std::uint32_t>(std::min<std::uint64_t>(doubled, brickIndexLimit(B))));
            g->words_ = words_;
            this->copyPaletteTo(*g);
            return g;
        }
        if constexpr (B == 32) {
            fatal("palette growth past 2^32 - 1 entries");
        } else {
            auto g = std::make_unique<PaletteBrickAt<E, 2 * B>>(
                static_cast<std::uint32_t>(std::min<std::uint64_t>(doubled, brickIndexLimit(2 * B))));
            for (std::uint32_t c = 0; c < kBrickCells; ++c) g->put(c, indexAt(c));
            this->copyPaletteTo(*g);
            return g;
        }
    }

    std::uint64_t fullDigest() const override {
        std::vector<std::uint64_t> h(this->entries());
        for (std::size_t s = 0; s < h.size(); ++s) h[s] = digestStateHash(canonicalEncoding(this->entry(static_cast<std::uint32_t>(s))));
        std::uint64_t d = 0;
        for (std::uint32_t c = 0; c < kBrickCells; ++c) d = addMod(d, mulMod(digestWeight(c), h[indexAt(c)]));
        return d;
    }

    std::size_t indexBytes() const override { return words_.size() * sizeof(std::uint64_t); }
    std::unique_ptr<Brick<E>> clone() const override { return std::make_unique<PaletteBrickAt>(*this); }

private:
    template <class, unsigned>
    friend class PaletteBrickAt;

    void put(std::uint32_t cell, std::uint32_t v) {
        std::uint64_t& w = words_[cell >> kShift];
        const unsigned sh = (cell & (kPerWord - 1)) * B;
        w = (w & ~(kMask << sh)) | (static_cast<std::uint64_t>(v) << sh);
    }

    std::vector<std::uint64_t> words_;
};

// A palette Brick at width w (one of kBrickWidths), every index 0, an empty
// palette and room for `capacity` entries (at most what the width can name).
template <class E>
std::unique_ptr<PaletteBrick<E>> makePaletteBrick(unsigned w, std::uint32_t capacity = kBrickMinCapacity) {
    switch (w) {
        case 1: return std::make_unique<PaletteBrickAt<E, 1>>(capacity);
        case 2: return std::make_unique<PaletteBrickAt<E, 2>>(capacity);
        case 4: return std::make_unique<PaletteBrickAt<E, 4>>(capacity);
        case 8: return std::make_unique<PaletteBrickAt<E, 8>>(capacity);
        case 16: return std::make_unique<PaletteBrickAt<E, 16>>(capacity);
        case 32: return std::make_unique<PaletteBrickAt<E, 32>>(capacity);
        default: fatal("makePaletteBrick: not an aligned width");
    }
}
