#pragma once
// The reverse lookup (world/brick): an entry to its palette index, which an
// edit needs to find whether its state is already an entry. It belongs to the
// Brick's owner (chunk data), not the Brick: only the owner's turn edits and
// no reader uses it, so it needs no synchronisation.
//
// A linear scan of the palette up to a threshold; above it, a flat
// linear-probing table of palette indices at load at most 1/2, comparing
// through the palette, sized to the palette's capacity when built (so it never
// regrows within one Brick's life), built lazily on the first lookup that needs
// it. Dead entries are found like live ones, so an edit reuses them.
//
// The hash is in-process only: it leaves nothing persisted or compared.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "brick.h"
#include "digest.h"

// The scan's threshold: 32 entries, measured on one machine in the research
// (prototypes/brick-research.md); a placeholder until both reference machines
// are measured (versions.md v0.2.2, measurements).
inline constexpr std::uint32_t kReverseScanMax = 32;

// The threshold in force: kReverseScanMax, except that measurements change it
// (sandbox_tests --time-chunk). Nothing else writes it.
inline std::uint32_t gReverseScanMax = kReverseScanMax;

template <class E>
class ReverseLookup {
public:
    bool tableBuilt() const { return !slots_.empty(); }
    std::size_t tableBytes() const { return slots_.capacity() * sizeof(std::uint32_t); }

    // The palette index of `e`, or kNoIndex.
    std::uint32_t find(const PaletteBrick<E>& brick, const E& e) {
        const std::uint32_t n = brick.entries();
        if (n <= gReverseScanMax) {
            for (std::uint32_t i = 0; i < n; ++i)
                if (brick.entry(i) == e) return i;
            return kNoIndex;
        }
        if (!tableBuilt()) build(brick);
        const std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        for (std::uint32_t s = slot(e);; s = (s + 1) & mask) {
            const std::uint32_t v = slots_[s];
            if (v == kNoIndex) return kNoIndex;
            if (brick.entry(v) == e) return v;
        }
    }

    // Entry `index` was just appended to the palette.
    void inserted(const PaletteBrick<E>& brick, std::uint32_t index) {
        if (tableBuilt()) place(brick.entry(index), index);
    }

    // The palette changed under the table (a swap to another Brick, a
    // compaction): drop it; the next lookup that needs it rebuilds it.
    void drop() {
        slots_.clear();
        slots_.shrink_to_fit();
    }

private:
    void build(const PaletteBrick<E>& brick) {
        const std::size_t cap = std::bit_ceil(std::max<std::size_t>(16, std::size_t{2} * brick.capacity()));
        slots_.assign(cap, kNoIndex);
        shift_ = 64u - static_cast<unsigned>(std::countr_zero(cap));
        for (std::uint32_t i = 0; i < brick.entries(); ++i) place(brick.entry(i), i);
    }
    std::uint32_t slot(const E& e) const {
        return static_cast<std::uint32_t>((mix64(canonicalEncoding(e)) * 0x9E3779B97F4A7C15ull) >> shift_);
    }
    void place(const E& e, std::uint32_t index) {
        const std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        std::uint32_t s = slot(e);
        while (slots_[s] != kNoIndex) s = (s + 1) & mask;
        slots_[s] = index;
    }

    std::vector<std::uint32_t> slots_;
    unsigned shift_ = 64;
};
