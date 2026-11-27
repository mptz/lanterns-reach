// Prototype of world/brick (design/brick.md) and v0.2.2's representations:
// a 64^3 Brick, uniform or palette at aligned widths 1..32 bits, generic over
// the palette entry type, with the data/content-digest.
//
// The growth rule is brick.md's: palettes keep no per-entry counts; an edit
// that needs a new entry appends it, and a full palette doubles its width;
// no edit compacts. Compaction (renumber the live entries, pick uniform or
// the smallest width) is explicit, or discretionary at a turn boundary
// (turnEnd), driven by O(1) signals (prototypes/brick-research.md).
//
// Single-threaded (v0.2.2): a representation change builds the new object
// from the old, swaps the pointer and frees the old one directly.
#pragma once

#include "../common/fatal.hpp"
#include "digest.hpp"

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace proto {

inline constexpr std::uint32_t kBrickEdge = 64;
inline constexpr std::uint32_t kBrickCells = kBrickEdge * kBrickEdge * kBrickEdge;  // 2^18
inline constexpr std::uint32_t kNoIndex = 0xFFFFFFFFu;

// world/brick's layout: x + 64*y + 64^2*z (z up).
constexpr std::uint32_t cellIndex(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return x + kBrickEdge * y + kBrickEdge * kBrickEdge * z;
}

// A palette entry: a regular value with a canonical 64-bit encoding.
template <class E>
concept PaletteEntry = std::regular<E> && requires(const E& e) {
    { canonicalEncoding(e) } -> std::same_as<std::uint64_t>;
};

template <PaletteEntry E>
std::uint64_t stateHash(const E& e) {
    return digestH(canonicalEncoding(e));
}

// In-process hashing for the reverse lookup only.
template <PaletteEntry E>
std::uint64_t entryHash(const E& e) {
    return mix64(canonicalEncoding(e));
}

inline constexpr unsigned kWidths[] = {1, 2, 4, 8, 16, 32};

// Entries a palette at width w can address: 2^w; at 32 bits, 2^32 - 1
// (kNoIndex is reserved), so a 32-bit palette may grow past 2^18 entries.
constexpr std::uint32_t paletteCapacity(unsigned w) {
    return w >= 32 ? 0xFFFFFFFFu : (std::uint32_t{1} << w);
}

// Smallest aligned width holding n >= 1 entries (n = 1 gives 1).
constexpr unsigned smallestWidth(std::uint32_t n) {
    for (unsigned w : kWidths)
        if (paletteCapacity(w) >= n) return w;
    fatal("smallestWidth: unreachable");
}

// The reverse lookup's scan threshold (a prototype knob; see the proposal).
inline std::uint32_t gReverseScanMax = 32;
// How edits maintain the possible-deaths signal (a knob for measuring it):
// 0 off, 1 the 64-byte block only, 2 the block then the y and z faces.
inline int gWitnessMode = 2;

template <PaletteEntry E>
class Rep {
public:
    virtual ~Rep() = default;
    virtual E get(std::uint32_t cell) const = 0;
    virtual unsigned width() const = 0;  // 0 = uniform
    virtual std::uint64_t fullDigest() const = 0;
    virtual std::size_t indexBytes() const = 0;
    virtual std::size_t paletteBytes() const = 0;
    virtual std::size_t lookupBytes() const = 0;
};

template <PaletteEntry E>
class UniformRep final : public Rep<E> {
public:
    explicit UniformRep(E s) : s_(s) {}
    E get(std::uint32_t) const override { return s_; }
    unsigned width() const override { return 0; }
    std::uint64_t fullDigest() const override { return mulMod(stateHash(s_), digestK()); }
    std::size_t indexBytes() const override { return 0; }
    std::size_t paletteBytes() const override { return sizeof(E); }
    std::size_t lookupBytes() const override { return 0; }
    const E& state() const { return s_; }

private:
    E s_;
};

// The reverse lookup above the scan threshold: open addressing with linear
// probing over 4-byte palette indices, compared through the palette, load
// at most 1/2, power-of-two capacity. Owner-only (readers never use it).
template <PaletteEntry E>
class ReverseTable {
public:
    bool built() const { return !slots_.empty(); }
    std::uint32_t find(const std::vector<E>& pal, const E& e) const {
        const std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        for (std::uint32_t s = slot(e);; s = (s + 1) & mask) {
            std::uint32_t v = slots_[s];
            if (v == kNoIndex) return kNoIndex;
            if (pal[v] == e) return v;
        }
    }
    // Index i was just appended (pal[i]).
    void insert(const std::vector<E>& pal, std::uint32_t i) {
        if (2 * (std::uint64_t{i} + 1) > slots_.size()) return rebuild(pal);
        place(pal[i], i);
    }
    void rebuild(const std::vector<E>& pal) {
        const std::uint64_t n = pal.size();
        std::uint32_t cap = static_cast<std::uint32_t>(std::bit_ceil(std::max<std::uint64_t>(16, 2 * n)));
        slots_.assign(cap, kNoIndex);
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(cap));
        for (std::uint32_t i = 0; i < n; ++i) place(pal[i], i);
        ++rebuilds_;
    }
    void drop() {
        slots_.clear();
        slots_.shrink_to_fit();
    }
    std::size_t bytes() const { return slots_.capacity() * sizeof(std::uint32_t); }
    std::uint64_t rebuilds() const { return rebuilds_; }

private:
    std::uint32_t slot(const E& e) const {
        return static_cast<std::uint32_t>((entryHash(e) * 0x9E3779B97F4A7C15ull) >> shift_);
    }
    void place(const E& e, std::uint32_t i) {
        const std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        std::uint32_t s = slot(e);
        while (slots_[s] != kNoIndex) s = (s + 1) & mask;
        slots_[s] = i;
    }
    std::vector<std::uint32_t> slots_;
    unsigned shift_ = 64;
    std::uint64_t rebuilds_ = 0;
};

// The palette and its reverse lookup, shared by every width. Palettes keep
// no per-entry counts, so entries stay (dead or live) until a compaction.
template <PaletteEntry E>
class PaletteBase : public Rep<E> {
public:
    virtual std::uint32_t getIdx(std::uint32_t cell) const = 0;
    virtual void setIdx(std::uint32_t cell, std::uint32_t idx) = 0;
    // Whether a witness cell near cell (its 64-byte block of index words,
    // then its y and z face neighbours inside the Brick) holds index v.
    virtual bool neighbourHolds(std::uint32_t cell, std::uint32_t v, bool faces) const = 0;
    // Every cell's index, in cell order (the export stream).
    virtual void exportIdx(std::uint32_t* out) const = 0;
    // Set every cell's index to remap[in[cell]] (the build stream).
    virtual void importIdx(const std::uint32_t* in, const std::uint32_t* remap) = 0;
    // The next width, every index repacked; fatal at 32 bits.
    virtual std::unique_ptr<PaletteBase> grown() = 0;

    E get(std::uint32_t cell) const final { return palette_[getIdx(cell)]; }

    const E& entry(std::uint32_t i) const { return palette_[i]; }
    std::uint32_t entries() const { return static_cast<std::uint32_t>(palette_.size()); }
    bool full() const { return entries() >= paletteCapacity(this->width()); }

    // Reverse lookup: a linear scan up to the threshold, then the table,
    // built on the first lookup that needs it.
    std::uint32_t find(const E& e) {
        const std::uint32_t n = entries();
        if (n <= gReverseScanMax) {
            for (std::uint32_t i = 0; i < n; ++i)
                if (palette_[i] == e) return i;
            return kNoIndex;
        }
        if (!table_.built()) table_.rebuild(palette_);
        return table_.find(palette_, e);
    }

    // Invariant 3: entries are distinct (asserted on insertion, in the table's
    // range; below it by the scan the caller has just done).
    std::uint32_t append(const E& e) {
        PROTO_ASSERT(!full(), "palette append when full");
        const std::uint32_t i = entries();
        palette_.push_back(e);
        if (table_.built()) table_.insert(palette_, i);
        return i;
    }

    std::size_t paletteBytes() const final { return palette_.capacity() * sizeof(E); }
    std::size_t lookupBytes() const final { return table_.bytes(); }
    std::uint64_t tableRebuilds() const { return table_.rebuilds(); }
    void dropTable() { table_.drop(); }

    void adoptPaletteFrom(PaletteBase& other) {
        palette_ = std::move(other.palette_);
        table_ = std::move(other.table_);
    }

protected:
    std::vector<E> palette_;
    ReverseTable<E> table_;
};

template <PaletteEntry E, unsigned B>
class PaletteRep final : public PaletteBase<E> {
    static_assert(B == 1 || B == 2 || B == 4 || B == 8 || B == 16 || B == 32);
    static constexpr unsigned kPerWord = 64 / B;
    static constexpr unsigned kShift = std::countr_zero(kPerWord);
    static constexpr std::uint64_t kMask = (std::uint64_t{1} << B) - 1;

public:
    PaletteRep() : words_(kBrickCells / kPerWord, 0) {}

    // The width-specific accessors: a shift and a mask.
    std::uint32_t idx(std::uint32_t cell) const {
        std::uint64_t w = words_[cell >> kShift];
        unsigned sh = (cell & (kPerWord - 1)) * B;
        return static_cast<std::uint32_t>((w >> sh) & kMask);
    }
    void setIdxFast(std::uint32_t cell, std::uint32_t v) {
        std::uint64_t& w = words_[cell >> kShift];
        unsigned sh = (cell & (kPerWord - 1)) * B;
        w = (w & ~(kMask << sh)) | (static_cast<std::uint64_t>(v) << sh);
    }

    std::uint32_t getIdx(std::uint32_t cell) const override { return idx(cell); }
    void setIdx(std::uint32_t cell, std::uint32_t v) override {
        PROTO_ASSERT(v < this->entries(), "invariant 4: index past the palette");
        setIdxFast(cell, v);
    }
    unsigned width() const override { return B; }

    // Whether another cell in the same 64-byte block of index words as cell
    // (8 words, 512 / B cells: x-runs of 16..64 cells, or several rows at
    // narrow widths) holds index v. Bit-parallel; touches no other cache
    // line than the edit's own write (with 64-byte-aligned storage).
    bool lineHolds(std::uint32_t cell, std::uint32_t v) const {
        constexpr std::uint64_t kOnes = ~std::uint64_t{0} / kMask;  // 1 in each lane's low bit
        constexpr std::uint64_t kHigh = kOnes << (B - 1);
        const std::uint32_t wi = cell >> kShift;
        const std::uint32_t base = wi & ~7u;
        const std::uint64_t pattern = kOnes * v;
        for (std::uint32_t k = base; k < base + 8; ++k) {
            std::uint64_t x = words_[k] ^ pattern;  // a zero lane holds v
            if (k == wi) x |= std::uint64_t{1} << ((cell & (kPerWord - 1)) * B);  // not the cell itself
            if (((x - kOnes) & ~x & kHigh) != 0) return true;
        }
        return false;
    }

    bool neighbourHolds(std::uint32_t cell, std::uint32_t v, bool faces) const override {
        if (lineHolds(cell, v)) return true;
        if (!faces) return false;
        const std::uint32_t y = (cell >> 6) & 63, z = cell >> 12;
        return (y > 0 && idx(cell - 64) == v) || (y < 63 && idx(cell + 64) == v) ||
               (z > 0 && idx(cell - 4096) == v) || (z < 63 && idx(cell + 4096) == v);
    }

    void exportIdx(std::uint32_t* out) const override {
        for (std::size_t wi = 0; wi < words_.size(); ++wi) {
            std::uint64_t w = words_[wi];
            for (unsigned j = 0; j < kPerWord; ++j) out[wi * kPerWord + j] = static_cast<std::uint32_t>((w >> (j * B)) & kMask);
        }
    }
    void importIdx(const std::uint32_t* in, const std::uint32_t* remap) override {
        for (std::size_t wi = 0; wi < words_.size(); ++wi) {
            std::uint64_t w = 0;
            for (unsigned j = 0; j < kPerWord; ++j) w |= static_cast<std::uint64_t>(remap[in[wi * kPerWord + j]]) << (j * B);
            words_[wi] = w;
        }
    }

    std::unique_ptr<PaletteBase<E>> grown() override {
        if constexpr (B == 32) {
            fatal("palette growth past 32 bits");
        } else {
            auto g = std::make_unique<PaletteRep<E, 2 * B>>();
            for (std::uint32_t c = 0; c < kBrickCells; ++c) g->setIdxFast(c, idx(c));
            g->adoptPaletteFrom(*this);
            return g;
        }
    }

    std::uint64_t fullDigest() const override {
        std::vector<std::uint64_t> h(this->entries());
        for (std::uint32_t s = 0; s < this->entries(); ++s) h[s] = stateHash(this->palette_[s]);
        std::uint64_t d = 0;
        for (std::uint32_t c = 0; c < kBrickCells; ++c) d = addMod(d, mulMod(digestW(c), h[idx(c)]));
        return d;
    }

    std::size_t indexBytes() const override { return words_.size() * sizeof(std::uint64_t); }

private:
    std::vector<std::uint64_t> words_;
};

template <PaletteEntry E>
std::unique_ptr<PaletteBase<E>> makePaletteRep(unsigned w) {
    switch (w) {
        case 1: return std::make_unique<PaletteRep<E, 1>>();
        case 2: return std::make_unique<PaletteRep<E, 2>>();
        case 4: return std::make_unique<PaletteRep<E, 4>>();
        case 8: return std::make_unique<PaletteRep<E, 8>>();
        case 16: return std::make_unique<PaletteRep<E, 16>>();
        case 32: return std::make_unique<PaletteRep<E, 32>>();
        default: fatal("makePaletteRep: not an aligned width");
    }
}

// When turnEnd compacts (prototypes/brick-research.md). Parameters are
// placeholders unless the proposal says they are derived.
struct CompactionPolicy {
    enum class Kind {
        None,      // only explicit compactions (saves, bulk fills)
        Appends,   // appends since the last compaction >= max(floor, fraction * live then)
        Witness,   // possible deaths >= max(floor, fraction * palette size)
    };
    Kind kind = Kind::None;
    std::uint32_t floor = 4096;
    double fraction = 0.5;
    // Width clause: after a growth, compact if the lower bound on live
    // entries would fit the previous width with this much headroom.
    bool widthClause = false;
    // Narrow only with room for max(floor, headroom * capacity) appends after.
    double headroom = 0.25;

    // The proposal's recommendation: appends >= max(4096, live / 4), no
    // width clause (it needs the witness signal and never fired usefully).
    static CompactionPolicy recommended() {
        CompactionPolicy p;
        p.kind = Kind::Appends;
        p.floor = 4096;
        p.fraction = 0.25;
        return p;
    }
};

// O(1)-per-edit signals, reset at every compaction (and fill).
struct CompactionSignals {
    std::uint64_t edits = 0;           // non-trivial edits
    std::uint64_t appends = 0;         // new palette entries
    std::uint64_t possibleDeaths = 0;  // edits whose old index had no face neighbour holding it
    std::uint32_t liveAtCompaction = 1;
    bool grew = false;                 // a growth since the last compaction
};

struct BrickTestAccess;

template <PaletteEntry E>
class Brick {
public:
    Brick() : rep_(std::make_unique<UniformRep<E>>(E{})) {}
    explicit Brick(const E& s) : rep_(std::make_unique<UniformRep<E>>(s)) {}

    E get(std::uint32_t x, std::uint32_t y, std::uint32_t z) const { return getCell(checkedIndex(x, y, z)); }
    E getCell(std::uint32_t cell) const { return rep_->get(cell); }

    void set(std::uint32_t x, std::uint32_t y, std::uint32_t z, const E& e) { setCell(checkedIndex(x, y, z), e); }

    // A single edit: only ever upgrades (uniform -> palette, narrower ->
    // wider); never compacts (invariant 7).
    void setCell(std::uint32_t cell, const E& e) {
        PROTO_ASSERT(cell < kBrickCells, "cell out of range");
        if (rep_->width() == 0) {
            const E s = static_cast<UniformRep<E>&>(*rep_).state();
            if (s == e) return;
            auto p = makePaletteRep<E>(1);  // promotion: all indices 0 = s
            p->append(s);
            p->setIdx(cell, p->append(e));
            rep_ = std::move(p);
            ++sig_.edits;
            ++sig_.appends;
            noteEdit(cell, s, e);
            return;
        }
        auto* p = static_cast<PaletteBase<E>*>(rep_.get());
        const std::uint32_t oldIdx = p->getIdx(cell);
        const E old = p->entry(oldIdx);
        if (old == e) return;
        ++sig_.edits;
        if (gWitnessMode != 0 && !p->neighbourHolds(cell, oldIdx, gWitnessMode == 2)) ++sig_.possibleDeaths;
        std::uint32_t ni = p->find(e);  // a dead entry for e is reused
        if (ni == kNoIndex) {
            if (p->full()) {
                ++stats_.growths;
                sig_.grew = true;
                rep_ = p->grown();
                p = static_cast<PaletteBase<E>*>(rep_.get());
            }
            ni = p->append(e);
            ++sig_.appends;
        }
        p->setIdx(cell, ni);
        noteEdit(cell, old, e);
    }

    // Bulk fill: ends in the most compact representation (uniform if one
    // state, else the smallest width). Leaves the digest lazy.
    void fill(std::span<const E> cells) {
        PROTO_ASSERT(cells.size() == kBrickCells, "fill: wrong cell count");
        std::unordered_map<std::uint64_t, std::uint32_t> seen;  // by canonical encoding
        std::vector<E> order;
        std::vector<std::uint32_t> idx(kBrickCells);
        for (std::uint32_t c = 0; c < kBrickCells; ++c) {
            auto [it, fresh] = seen.emplace(canonicalEncoding(cells[c]), static_cast<std::uint32_t>(order.size()));
            if (fresh) order.push_back(cells[c]);
            idx[c] = it->second;
        }
        digest_.reset();
        resetSignals(static_cast<std::uint32_t>(order.size()));
        if (order.size() == 1) {
            rep_ = std::make_unique<UniformRep<E>>(order[0]);
            return;
        }
        auto p = makePaletteRep<E>(smallestWidth(static_cast<std::uint32_t>(order.size())));
        for (const E& e : order) p->append(e);
        std::vector<std::uint32_t> identity(order.size());
        for (std::uint32_t i = 0; i < identity.size(); ++i) identity[i] = i;
        p->importIdx(idx.data(), identity.data());
        rep_ = std::move(p);
    }

    void fillUniform(const E& s) {
        rep_ = std::make_unique<UniformRep<E>>(s);
        digest_.reset();
        resetSignals(1);
    }

    // Lazy, then incremental (data/content-digest).
    std::uint64_t digest() const {
        if (!digest_) digest_ = rep_->fullDigest();
        return *digest_;
    }

    std::size_t memoryBytes() const { return rep_->indexBytes() + rep_->paletteBytes() + rep_->lookupBytes(); }

    // The owner's turn boundary, outside every edit's write window: compact
    // if the policy says so. O(1) unless it compacts. Returns whether it did.
    bool turnEnd(const CompactionPolicy& pol) {
        if (pol.kind == CompactionPolicy::Kind::None || rep_->width() == 0) return false;
        const auto* p = static_cast<const PaletteBase<E>*>(rep_.get());
        const std::uint32_t n = p->entries();
        bool go = false;
        switch (pol.kind) {
            case CompactionPolicy::Kind::None: break;
            case CompactionPolicy::Kind::Appends:
                go = sig_.appends >= std::max<double>(pol.floor, pol.fraction * sig_.liveAtCompaction);
                break;
            case CompactionPolicy::Kind::Witness:
                go = sig_.possibleDeaths >= std::max<double>(pol.floor, pol.fraction * n);
                break;
        }
        if (!go && pol.widthClause && sig_.grew && rep_->width() > 1) {
            // live >= n - possibleDeaths: compact only if that lower bound
            // leaves the previous width the room narrowing requires.
            const std::uint64_t liveLow = n > sig_.possibleDeaths ? n - sig_.possibleDeaths : 0;
            go = narrowingRoom(rep_->width() / 2, liveLow, pol);
            // Disarmed until the next growth; later deaths are the dead-entry trigger's job.
            if (!go) sig_.grew = false;
        }
        if (!go) return false;
        ++stats_.discretionary;
        compact(&pol);
        return true;
    }

    // A discretionary compaction narrows to width w only if, after it, at
    // least max(floor, headroom * capacity) appends fit before the next
    // growth, so a narrow-then-regrow cycle is paid for by >= floor appends.
    static bool narrowingRoom(unsigned w, std::uint64_t live, const CompactionPolicy& pol) {
        const double cap = paletteCapacity(w);
        const double room = std::max<double>(pol.floor, pol.headroom * cap);
        return static_cast<double>(live) + room <= cap;
    }

    const CompactionSignals& signals() const { return sig_; }

    struct Stats {
        std::uint64_t growths = 0, discretionary = 0, explicitCompactions = 0;
    };

private:
    friend struct BrickTestAccess;

    static std::uint32_t checkedIndex(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        PROTO_ASSERT(x < kBrickEdge && y < kBrickEdge && z < kBrickEdge, "cell out of range");
        return cellIndex(x, y, z);
    }

    void noteEdit(std::uint32_t cell, const E& a, const E& b) {
        if (digest_) *digest_ = addMod(*digest_, mulMod(digestW(cell), subMod(stateHash(b), stateHash(a))));
    }

    void resetSignals(std::uint32_t live) {
        sig_ = CompactionSignals{};
        sig_.liveAtCompaction = live;
    }

    // Live entries of a palette rep: marks, the exported indices and the count.
    struct LiveSet {
        std::vector<std::uint32_t> idx;
        std::vector<std::uint8_t> live;
        std::uint32_t n = 0;
    };
    LiveSet liveSet() const {
        const auto* p = static_cast<const PaletteBase<E>*>(rep_.get());
        LiveSet s;
        s.idx.resize(kBrickCells);
        p->exportIdx(s.idx.data());
        s.live.assign(p->entries(), 0);
        for (std::uint32_t c = 0; c < kBrickCells; ++c) s.live[s.idx[c]] = 1;
        for (std::uint8_t l : s.live) s.n += l;
        return s;
    }

    // Rebuild at width w (or uniform when w == 0) from the live entries,
    // renumbered in ascending old-index order. The digest carries over.
    // Built new and swapped in (the representation swap); never in place.
    void rebuild(unsigned w) {
        if (rep_->width() == 0) {
            const E s = static_cast<UniformRep<E>&>(*rep_).state();
            if (w == 0) return;
            auto q = makePaletteRep<E>(w);
            q->append(s);
            rep_ = std::move(q);
            return;
        }
        LiveSet ls = liveSet();
        rebuildFrom(ls, w);
    }
    void rebuildFrom(LiveSet& ls, unsigned w) {
        auto* p = static_cast<PaletteBase<E>*>(rep_.get());
        if (w == 0) {
            PROTO_ASSERT(ls.n == 1, "uniform needs exactly one live state");
            std::uint32_t s = 0;
            while (!ls.live[s]) ++s;
            rep_ = std::make_unique<UniformRep<E>>(p->entry(s));
            resetSignals(1);
            return;
        }
        PROTO_ASSERT(ls.n <= paletteCapacity(w), "width too narrow for the live entries");
        auto q = makePaletteRep<E>(w);
        std::vector<std::uint32_t> remap(p->entries(), 0);
        for (std::uint32_t s = 0; s < p->entries(); ++s)
            if (ls.live[s]) remap[s] = q->append(p->entry(s));
        q->importIdx(ls.idx.data(), remap.data());
        rep_ = std::move(q);
        resetSignals(ls.n);
    }

    std::uint32_t liveCount() const {
        if (rep_->width() == 0) return 1;
        return liveSet().n;
    }

    // Compaction: uniform if one state is live, else the smallest width (a
    // save, a bulk fill, the test API). A discretionary compaction (pol set)
    // never widens, and narrows only where narrowingRoom allows (one live
    // state still goes uniform: promotion and the small widths are cheap).
    void compact(const CompactionPolicy* pol = nullptr) {
        if (rep_->width() == 0) return;
        LiveSet ls = liveSet();
        unsigned w;
        if (ls.n == 1) {
            w = 0;
        } else {
            w = smallestWidth(ls.n);
            if (pol != nullptr) {
                while (w < rep_->width() && !narrowingRoom(w, ls.n, *pol)) w *= 2;
                w = std::min(w, rep_->width());
            }
        }
        rebuildFrom(ls, w);
    }

    std::unique_ptr<Rep<E>> rep_;
    mutable std::optional<std::uint64_t> digest_;
    CompactionSignals sig_;
    Stats stats_;
};

// Test-only access (v0.2.2's test access type).
struct BrickTestAccess {
    template <class E> static unsigned width(const Brick<E>& b) { return b.rep_->width(); }
    template <class E> static bool isUniform(const Brick<E>& b) { return b.rep_->width() == 0; }
    template <class E> static std::uint32_t paletteSize(const Brick<E>& b) {
        if (b.rep_->width() == 0) return 1;
        return static_cast<const PaletteBase<E>&>(*b.rep_).entries();
    }
    template <class E> static std::uint32_t liveEntries(const Brick<E>& b) { return b.liveCount(); }
    template <class E> static bool paletteFull(const Brick<E>& b) {
        return b.rep_->width() != 0 && static_cast<const PaletteBase<E>&>(*b.rep_).full();
    }
    template <class E> static void forceWidth(Brick<E>& b, unsigned w) { b.rebuild(w); }
    template <class E> static void compact(Brick<E>& b) {
        ++b.stats_.explicitCompactions;
        b.compact();
    }
    template <class E> static void dropDigest(Brick<E>& b) { b.digest_.reset(); }
    template <class E> static bool digestComputed(const Brick<E>& b) { return b.digest_.has_value(); }
    template <class E> static typename Brick<E>::Stats stats(const Brick<E>& b) { return b.stats_; }
    template <class E> static std::size_t lookupBytes(const Brick<E>& b) { return b.rep_->lookupBytes(); }
    template <class E> static bool tableBuilt(const Brick<E>& b) {
        return b.rep_->width() != 0 && static_cast<const PaletteBase<E>&>(*b.rep_).lookupBytes() != 0;
    }
};

}  // namespace proto
