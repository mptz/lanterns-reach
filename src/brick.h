#pragma once
// A Brick (world/brick): the dense 1 ft representation of one chunk's cells.
// v0.2.1 has the palette representation at one width only: 16-bit indices
// into a palette of fixed capacity 2¹⁶ (uniform, the other widths, growth and
// compaction are v0.2.2). Only chunk code (chunk.h) touches a Brick; nothing
// outside it sees cells, indices or the palette.
//
// Generic over its palette entry type. Palette entry 0 is the fill the Brick
// was made with. A Brick is never resized in place.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

inline constexpr int kChunkBits = 6;
inline constexpr int kChunkSize = 1 << kChunkBits;  // 64 cells per chunk edge: the one constant

// Layout (world/brick): cell index = x + 64·y + 64²·z (z up), chosen for
// bitmask meshing. Behind this function; nothing else knows the formula.
constexpr std::size_t brickCellIndex(int x, int y, int z) {
    return static_cast<std::size_t>(x) + static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(y) +
           static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(z);
}

template <class Entry>
class Brick {
public:
    using Index = std::uint16_t;
    static constexpr std::size_t kCellCount = static_cast<std::size_t>(kChunkSize) * kChunkSize * kChunkSize;
    static constexpr std::size_t kPaletteCapacity = std::size_t{1} << 16;

    // Every cell starts as `fill`, palette entry 0.
    explicit Brick(const Entry& fill) : indices_(kCellCount, Index{0}), palette_{fill} {}

    // Local coordinates, each 0–63.
    Index indexAt(const glm::ivec3& local) const { return indices_[brickCellIndex(local.x, local.y, local.z)]; }
    void setIndex(const glm::ivec3& local, Index i) {
        assert(i < palette_.size());
        indices_[brickCellIndex(local.x, local.y, local.z)] = i;
    }
    const Entry& at(const glm::ivec3& local) const { return palette_[indexAt(local)]; }

    const Entry& entry(Index i) const { return palette_[i]; }
    std::size_t paletteSize() const { return palette_.size(); }
    // Appends an entry and returns its index. Capacity is asserted, not
    // handled: today's registry can't fill 2¹⁶ entries.
    Index append(const Entry& e) {
        assert(palette_.size() < kPaletteCapacity);
        palette_.push_back(e);
        return static_cast<Index>(palette_.size() - 1);
    }

private:
    std::vector<Index> indices_;
    std::vector<Entry> palette_;
};
