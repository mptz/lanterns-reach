#pragma once
// A Brick (world/brick): the dense 1 ft representation of one chunk's cells.
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
// A Brick is never resized in place. Palettes keep no per-entry counts, so a
// Brick can't tell which entries are dead; an edit that needs an entry
// appends it (the entry is stored before any index that refers to it), and a
// full palette is replaced by `widened()`, a new Brick at the next width that
// the owner swaps in. The reverse lookup (entry to index) belongs to the
// owner, not the Brick.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/vec3.hpp>

#include "digest.h"
#include "fatal.h"

inline constexpr int kChunkBits = 6;
inline constexpr int kChunkSize = 1 << kChunkBits;  // 64 cells per chunk edge: the one constant
inline constexpr std::uint32_t kBrickCells = 1u << (3 * kChunkBits);  // 2¹⁸

// Layout (world/brick): cell index = x + 64·y + 64²·z (z up), chosen for
// bitmask meshing. Behind this function; nothing else knows the formula.
constexpr std::size_t brickCellIndex(int x, int y, int z) {
    return static_cast<std::size_t>(x) + static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(y) +
           static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(z);
}

inline constexpr unsigned kBrickWidths[] = {1, 2, 4, 8, 16, 32};
inline constexpr std::uint32_t kNoIndex = 0xFFFFFFFFu;  // never a palette index
// A 32-bit palette's capacity when built (it may exceed 2¹⁸: dead entries
// accumulate until a compaction); a full one is replaced by one of twice the
// capacity, up to the 2³² − 1 indices there are.
inline constexpr std::uint32_t kBrickCapacity32 = 1u << 19;

// Entries a palette at storage width w can address; at 32 bits, the capacity
// it is built with.
constexpr std::uint32_t brickPaletteCapacity(unsigned w, std::uint32_t capacity32 = kBrickCapacity32) {
    return w >= 32 ? capacity32 : std::uint32_t{1} << w;
}

// A 32-bit palette's capacity for `entries` live entries: kBrickCapacity32,
// doubled until the entries sit at or below the low-water mark of 7/8.
inline std::uint32_t brickCapacity32For(std::uint32_t entries) {
    std::uint64_t cap = kBrickCapacity32;
    while (std::uint64_t{entries} * 8 > cap * 7 && cap < 0x80000000ull) cap *= 2;
    return static_cast<std::uint32_t>(cap);
}

// The smallest aligned width whose palette holds `entries` at or below the
// low-water mark of 7/8 of its capacity (a placeholder, world/brick). At 32
// bits `capacityOut`, if given, receives the capacity to build with.
inline unsigned brickCompactedWidth(std::uint32_t entries, std::uint32_t* capacityOut = nullptr) {
    for (unsigned w : kBrickWidths) {
        if (w == 32) break;
        if (std::uint64_t{entries} * 8 <= std::uint64_t{brickPaletteCapacity(w)} * 7) return w;
    }
    if (capacityOut) *capacityOut = brickCapacity32For(entries);
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
    // A new Brick with the same cells and palette at the next width (at 32
    // bits, at twice the capacity): the owner swaps it in and frees this one.
    virtual std::unique_ptr<PaletteBrick> widened() const = 0;

    const E& at(std::uint32_t cell) const final { return palette_[indexAt(cell)]; }

    const E& entry(std::uint32_t i) const { return palette_[i]; }
    std::uint32_t entries() const { return static_cast<std::uint32_t>(palette_.size()); }
    std::uint32_t capacity() const { return capacity_; }
    bool full() const { return entries() >= capacity_; }
    const std::vector<E>& palette() const { return palette_; }

    // Appends an entry and returns its index; fatal when full. The entry is
    // stored before any index refers to it.
    std::uint32_t append(const E& e) {
        FATAL_UNLESS(!full(), "palette append when full");
        palette_.push_back(e);
        return entries() - 1;
    }

    std::size_t paletteBytes() const final { return palette_.capacity() * sizeof(E); }

protected:
    explicit PaletteBrick(std::uint32_t capacity) : capacity_(capacity) {}
    std::vector<E> palette_;
    std::uint32_t capacity_;
};

template <class E, unsigned B>
class PaletteBrickAt final : public PaletteBrick<E> {
    static_assert(B == 1 || B == 2 || B == 4 || B == 8 || B == 16 || B == 32);
    static constexpr unsigned kPerWord = 64 / B;
    static constexpr unsigned kShift = static_cast<unsigned>(std::countr_zero(kPerWord));
    static constexpr std::uint64_t kMask = (std::uint64_t{1} << B) - 1;

public:
    explicit PaletteBrickAt(std::uint32_t capacity = brickPaletteCapacity(B))
        : PaletteBrick<E>(capacity), words_(kBrickCells / kPerWord, 0) {}

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

    std::unique_ptr<PaletteBrick<E>> widened() const override {
        if constexpr (B == 32) {
            FATAL_UNLESS(this->capacity_ < 0x80000000u, "palette growth past 2^32 - 1 entries");
            auto g = std::make_unique<PaletteBrickAt<E, 32>>(this->capacity_ * 2);
            g->words_ = words_;
            g->palette_ = this->palette_;
            return g;
        } else {
            auto g = std::make_unique<PaletteBrickAt<E, 2 * B>>();
            for (std::uint32_t c = 0; c < kBrickCells; ++c) g->put(c, indexAt(c));
            g->palette_ = this->palette_;
            return g;
        }
    }

    std::uint64_t fullDigest() const override {
        std::vector<std::uint64_t> h(this->palette_.size());
        for (std::size_t s = 0; s < h.size(); ++s) h[s] = digestStateHash(canonicalEncoding(this->palette_[s]));
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

// A palette Brick at width w (one of kBrickWidths), every index 0 and an empty
// palette. At 32 bits, `capacity32` is its capacity.
template <class E>
std::unique_ptr<PaletteBrick<E>> makePaletteBrick(unsigned w, std::uint32_t capacity32 = kBrickCapacity32) {
    switch (w) {
        case 1: return std::make_unique<PaletteBrickAt<E, 1>>();
        case 2: return std::make_unique<PaletteBrickAt<E, 2>>();
        case 4: return std::make_unique<PaletteBrickAt<E, 4>>();
        case 8: return std::make_unique<PaletteBrickAt<E, 8>>();
        case 16: return std::make_unique<PaletteBrickAt<E, 16>>();
        case 32: return std::make_unique<PaletteBrickAt<E, 32>>(capacity32);
        default: fatal("makePaletteBrick: not an aligned width");
    }
}
