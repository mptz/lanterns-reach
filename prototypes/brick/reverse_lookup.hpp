// Candidate reverse lookups for a Brick palette (state id -> palette index),
// over 4-byte state ids. Each structure indexes a palette it does not own:
// the palette is an append-only array of distinct ids, pal[i] the id at
// index i, and every structure supports find, insert (index n, whose id is
// pal[n], after indices 0..n-1) and rebuild (from the whole palette, as at
// a compaction). Nothing is deleted between rebuilds.
// Analysis and measurements: prototypes/brick-research.md.
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <vector>

#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace proto::rl {

inline constexpr std::uint32_t kNone = 0xFFFFFFFFu;

// Fibonacci hashing: the top bits of id * 2^64/phi.
inline std::uint64_t mulHash(std::uint32_t id) { return std::uint64_t{id} * 0x9E3779B97F4A7C15ull; }

// ---- Linear scan (SSE2, the x86-64 baseline), 16 entries per iteration;
// a hit's position comes from one 16-bit mask, with no scalar rescan.
inline std::uint32_t scanSse2(const std::uint32_t* pal, std::uint32_t n, std::uint32_t id) {
    const __m128i k = _mm_set1_epi32(static_cast<int>(id));
    std::uint32_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m128i a = _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pal + i)), k);
        __m128i b = _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pal + i + 4)), k);
        __m128i c = _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pal + i + 8)), k);
        __m128i d = _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pal + i + 12)), k);
        __m128i ab = _mm_packs_epi32(a, b), cd = _mm_packs_epi32(c, d);
        unsigned m = static_cast<unsigned>(_mm_movemask_epi8(_mm_packs_epi16(ab, cd)));
        if (m != 0) return i + static_cast<std::uint32_t>(std::countr_zero(m));
    }
    for (; i < n; ++i)
        if (pal[i] == id) return i;
    return kNone;
}

#if defined(__x86_64__)
// The same with AVX2 (x86-64-v3), 32 entries per iteration, for comparison.
__attribute__((target("avx2"))) inline std::uint32_t scanAvx2(const std::uint32_t* pal, std::uint32_t n,
                                                               std::uint32_t id) {
    const __m256i k = _mm256_set1_epi32(static_cast<int>(id));
    std::uint32_t i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i a = _mm256_cmpeq_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pal + i)), k);
        __m256i b = _mm256_cmpeq_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pal + i + 8)), k);
        __m256i c = _mm256_cmpeq_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pal + i + 16)), k);
        __m256i d = _mm256_cmpeq_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pal + i + 24)), k);
        __m256i any = _mm256_or_si256(_mm256_or_si256(a, b), _mm256_or_si256(c, d));
        if (!_mm256_testz_si256(any, any)) {
            // packs interleaves 128-bit lanes; recover the order with movemask per vector.
            std::uint32_t m = static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(a))) |
                              static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(b))) << 8 |
                              static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(c))) << 16 |
                              static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(d))) << 24;
            return i + static_cast<std::uint32_t>(std::countr_zero(m));
        }
    }
    for (; i < n; ++i)
        if (pal[i] == id) return i;
    return kNone;
}
#endif

// Branchless lower bound over a sorted array.
inline std::uint32_t lowerBound(const std::uint32_t* keys, std::uint32_t n, std::uint32_t id) {
    if (n == 0) return 0;
    const std::uint32_t* base = keys;
    std::uint32_t len = n;
    while (len > 1) {
        std::uint32_t half = len / 2;
        base = (base[half - 1] < id) ? base + half : base;
        len -= half;
    }
    return static_cast<std::uint32_t>(base - keys) + (*base < id ? 1u : 0u);
}

// Sorts (id, index) pairs of the palette range [from, to).
inline std::vector<std::uint64_t> sortedPairs(const std::uint32_t* pal, std::uint32_t from, std::uint32_t to) {
    std::vector<std::uint64_t> kv;
    kv.reserve(to - from);
    for (std::uint32_t i = from; i < to; ++i) kv.push_back((std::uint64_t{pal[i]} << 32) | i);
    std::sort(kv.begin(), kv.end());
    return kv;
}

// ---- Sorted array: ids sorted, each id's palette index alongside.
// find O(log n); insert O(n) (memmove of the tail); rebuild O(n log n).
class Sorted {
public:
    static constexpr const char* kName = "sorted+memmove";
    std::uint32_t find(const std::uint32_t*, std::uint32_t id) const {
        std::uint32_t n = static_cast<std::uint32_t>(keys_.size());
        std::uint32_t i = lowerBound(keys_.data(), n, id);
        return (i < n && keys_[i] == id) ? idx_[i] : kNone;
    }
    void insert(const std::uint32_t* pal, std::uint32_t index) {
        std::uint32_t id = pal[index];
        std::uint32_t pos = lowerBound(keys_.data(), static_cast<std::uint32_t>(keys_.size()), id);
        keys_.insert(keys_.begin() + pos, id);
        idx_.insert(idx_.begin() + pos, index);
    }
    void rebuild(const std::uint32_t* pal, std::uint32_t n) {
        auto kv = sortedPairs(pal, 0, n);
        keys_.resize(n);
        idx_.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            keys_[i] = static_cast<std::uint32_t>(kv[i] >> 32);
            idx_[i] = static_cast<std::uint32_t>(kv[i]);
        }
    }
    std::size_t bytes() const { return (keys_.capacity() + idx_.capacity()) * 4; }

private:
    std::vector<std::uint32_t> keys_, idx_;
};

// The unsorted tail of a hybrid: the palette's own last entries
// pal[covered, n), so it costs no memory. It is merged into the main
// structure when it reaches max(kMinTail, kTailFactor * sqrt(covered)).
inline constexpr std::uint32_t kMinTail = 64;
inline constexpr double kTailFactor = 2.0;
inline std::uint32_t tailCap(std::uint32_t covered) {
    return std::max<std::uint32_t>(kMinTail,
                                   static_cast<std::uint32_t>(kTailFactor * std::sqrt(static_cast<double>(covered))));
}

// ---- Hybrid: a sorted run plus the SIMD-scanned tail.
// find O(log n + sqrt n); insert amortised O(sqrt n), worst O(n) (a merge);
// rebuild O(n log n).
class SortedTail {
public:
    static constexpr const char* kName = "sorted+tail";
    std::uint32_t find(const std::uint32_t* pal, std::uint32_t id) const {
        std::uint32_t m = static_cast<std::uint32_t>(keys_.size());
        std::uint32_t i = lowerBound(keys_.data(), m, id);
        if (i < m && keys_[i] == id) return idx_[i];
        std::uint32_t t = scanSse2(pal + m, n_ - m, id);
        return t == kNone ? kNone : m + t;
    }
    void insert(const std::uint32_t* pal, std::uint32_t index) {
        n_ = index + 1;
        std::uint32_t m = static_cast<std::uint32_t>(keys_.size());
        if (n_ - m < tailCap(m)) return;
        auto tail = sortedPairs(pal, m, n_);
        // Merge from the back, in place, O(n).
        keys_.resize(n_);
        idx_.resize(n_);
        std::int64_t a = static_cast<std::int64_t>(m) - 1, b = static_cast<std::int64_t>(tail.size()) - 1;
        for (std::int64_t out = static_cast<std::int64_t>(n_) - 1; out >= 0; --out) {
            bool takeTail = a < 0 || (b >= 0 && static_cast<std::uint32_t>(tail[b] >> 32) > keys_[a]);
            if (takeTail) {
                keys_[out] = static_cast<std::uint32_t>(tail[b] >> 32);
                idx_[out] = static_cast<std::uint32_t>(tail[b]);
                --b;
            } else {
                keys_[out] = keys_[a];
                idx_[out] = idx_[a];
                --a;
            }
        }
    }
    void rebuild(const std::uint32_t* pal, std::uint32_t n) {
        auto kv = sortedPairs(pal, 0, n);
        keys_.resize(n);
        idx_.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            keys_[i] = static_cast<std::uint32_t>(kv[i] >> 32);
            idx_[i] = static_cast<std::uint32_t>(kv[i]);
        }
        n_ = n;
    }
    std::size_t bytes() const { return (keys_.capacity() + idx_.capacity()) * 4; }

private:
    std::vector<std::uint32_t> keys_, idx_;
    std::uint32_t n_ = 0;
};

// ---- Hybrid: an Eytzinger-layout (BFS-order) search array plus the tail.
// find O(log n + sqrt n), with prefetch; insert amortised O(sqrt n), worst
// O(n) (in-order gather, merge, re-layout); rebuild O(n log n).
class EytzingerTail {
public:
    static constexpr const char* kName = "eytzinger+tail";
    std::uint32_t find(const std::uint32_t* pal, std::uint32_t id) const {
        std::uint32_t m = m_;
        if (m != 0) {
            std::uint32_t k = 1;
            while (k <= m) {
                __builtin_prefetch(keys_.data() + 16 * k);
                k = 2 * k + (keys_[k] < id ? 1u : 0u);
            }
            k >>= std::countr_zero(~k) + 1;  // the last left turn: the lower bound
            if (k != 0 && keys_[k] == id) return idx_[k];
        }
        std::uint32_t t = scanSse2(pal + m, n_ - m, id);
        return t == kNone ? kNone : m + t;
    }
    void insert(const std::uint32_t* pal, std::uint32_t index) {
        n_ = index + 1;
        if (n_ - m_ < tailCap(m_)) return;
        std::vector<std::uint64_t> all;
        all.reserve(n_);
        gather(1, all);
        auto tail = sortedPairs(pal, m_, n_);
        std::vector<std::uint64_t> merged(all.size() + tail.size());
        std::merge(all.begin(), all.end(), tail.begin(), tail.end(), merged.begin());
        layout(merged);
    }
    void rebuild(const std::uint32_t* pal, std::uint32_t n) {
        n_ = n;
        layout(sortedPairs(pal, 0, n));
    }
    std::size_t bytes() const { return (keys_.capacity() + idx_.capacity()) * 4; }

private:
    void gather(std::uint32_t k, std::vector<std::uint64_t>& out) const {
        // Iterative in-order walk of the implicit tree.
        std::vector<std::uint32_t> stack;
        while (k <= m_ || !stack.empty()) {
            while (k <= m_) {
                stack.push_back(k);
                k = 2 * k;
            }
            k = stack.back();
            stack.pop_back();
            out.push_back((std::uint64_t{keys_[k]} << 32) | idx_[k]);
            k = 2 * k + 1;
        }
    }
    void layout(const std::vector<std::uint64_t>& sorted) {
        m_ = static_cast<std::uint32_t>(sorted.size());
        keys_.assign(m_ + 1, 0);
        idx_.assign(m_ + 1, 0);
        std::uint32_t i = 0;
        fill(sorted, i, 1);
    }
    void fill(const std::vector<std::uint64_t>& sorted, std::uint32_t& i, std::uint32_t k) {
        std::vector<std::uint32_t> stack;
        while (k <= m_ || !stack.empty()) {
            while (k <= m_) {
                stack.push_back(k);
                k = 2 * k;
            }
            k = stack.back();
            stack.pop_back();
            keys_[k] = static_cast<std::uint32_t>(sorted[i] >> 32);
            idx_[k] = static_cast<std::uint32_t>(sorted[i]);
            ++i;
            k = 2 * k + 1;
        }
    }
    std::vector<std::uint32_t> keys_, idx_;  // 1-based
    std::uint32_t m_ = 0, n_ = 0;
};

// Power-of-two capacity for n entries at load <= num/den, at least 16.
inline std::uint32_t tableCap(std::uint32_t n, std::uint32_t num, std::uint32_t den) {
    std::uint64_t need = (std::uint64_t{n} * den + num - 1) / num;  // load < 1 keeps an empty slot
    return static_cast<std::uint32_t>(std::bit_ceil(std::max<std::uint64_t>(16, need)));
}

// ---- Open addressing, linear probing, 8-byte slots (id, index); load <= 1/2.
// find/insert expected O(1), worst O(n) (a probe run or a regrowth);
// insert amortised O(1); rebuild O(n).
class LinearPairs {
public:
    static constexpr const char* kName = "linear (id,idx) 8B @1/2";
    std::uint32_t find(const std::uint32_t*, std::uint32_t id) const {
        if (slots_.empty()) return kNone;
        std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        for (std::uint32_t s = static_cast<std::uint32_t>(mulHash(id) >> shift_);; s = (s + 1) & mask) {
            std::uint64_t v = slots_[s];
            if (v == kEmpty) return kNone;
            if (static_cast<std::uint32_t>(v >> 32) == id) return static_cast<std::uint32_t>(v);
        }
    }
    void insert(const std::uint32_t* pal, std::uint32_t index) {
        if (2 * (std::uint64_t{index} + 1) > slots_.size()) return rebuild(pal, index + 1);
        place(pal[index], index);
    }
    void rebuild(const std::uint32_t* pal, std::uint32_t n) {
        std::uint32_t cap = tableCap(n, 1, 2);
        slots_.assign(cap, kEmpty);
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(cap));
        for (std::uint32_t i = 0; i < n; ++i) place(pal[i], i);
    }
    std::size_t bytes() const { return slots_.capacity() * 8; }

private:
    static constexpr std::uint64_t kEmpty = ~std::uint64_t{0};
    void place(std::uint32_t id, std::uint32_t index) {
        std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        std::uint32_t s = static_cast<std::uint32_t>(mulHash(id) >> shift_);
        while (slots_[s] != kEmpty) s = (s + 1) & mask;
        slots_[s] = (std::uint64_t{id} << 32) | index;
    }
    std::vector<std::uint64_t> slots_;
    unsigned shift_ = 64;
};

// ---- Open addressing, linear probing, 4-byte slots holding the palette
// index only, compared through the palette (the current brick.md design);
// load <= num/den. Complexity as LinearPairs; each probe costs a palette read.
template <std::uint32_t Num, std::uint32_t Den>
class LinearIdxAt {
public:
    static constexpr const char* kName = Num * 2 == Den ? "linear idx 4B @1/2" : "linear idx 4B @3/4";
    std::uint32_t find(const std::uint32_t* pal, std::uint32_t id) const {
        if (slots_.empty()) return kNone;
        std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        for (std::uint32_t s = static_cast<std::uint32_t>(mulHash(id) >> shift_);; s = (s + 1) & mask) {
            std::uint32_t v = slots_[s];
            if (v == kNone) return kNone;
            if (pal[v] == id) return v;
        }
    }
    void insert(const std::uint32_t* pal, std::uint32_t index) {
        if ((std::uint64_t{index} + 1) * Den > std::uint64_t{slots_.size()} * Num) return rebuild(pal, index + 1);
        place(pal[index], index);
    }
    void rebuild(const std::uint32_t* pal, std::uint32_t n) {
        std::uint32_t cap = tableCap(n, Num, Den);
        slots_.assign(cap, kNone);
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(cap));
        for (std::uint32_t i = 0; i < n; ++i) place(pal[i], i);
    }
    std::size_t bytes() const { return slots_.capacity() * 4; }

private:
    void place(std::uint32_t id, std::uint32_t index) {
        std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        std::uint32_t s = static_cast<std::uint32_t>(mulHash(id) >> shift_);
        while (slots_[s] != kNone) s = (s + 1) & mask;
        slots_[s] = index;
    }
    std::vector<std::uint32_t> slots_;
    unsigned shift_ = 64;
};
using LinearIdx = LinearIdxAt<1, 2>;

// ---- Linear probing, 4-byte slots: an 8-bit tag (more hash bits) above a
// 24-bit palette index, so most mismatches are rejected without reading the
// palette. Palettes up to 2^24 - 1 entries. Load <= num/den.
template <std::uint32_t Num, std::uint32_t Den>
class LinearTagged {
public:
    static constexpr const char* kName = Num * 2 == Den ? "linear tag+idx 4B @1/2" : "linear tag+idx 4B @3/4";
    static constexpr std::uint32_t kMaxEntries = (1u << 24) - 1;
    std::uint32_t find(const std::uint32_t* pal, std::uint32_t id) const {
        if (slots_.empty()) return kNone;
        std::uint64_t h = mulHash(id);
        std::uint32_t tag = static_cast<std::uint32_t>(h >> 24) & 0xFFu;
        std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        for (std::uint32_t s = static_cast<std::uint32_t>(h >> shift_);; s = (s + 1) & mask) {
            std::uint32_t v = slots_[s];
            if (v == kNone) return kNone;
            if ((v >> 24) == tag && pal[v & 0xFFFFFFu] == id) return v & 0xFFFFFFu;
        }
    }
    void insert(const std::uint32_t* pal, std::uint32_t index) {
        if ((std::uint64_t{index} + 1) * Den > std::uint64_t{slots_.size()} * Num) return rebuild(pal, index + 1);
        place(pal[index], index);
    }
    void rebuild(const std::uint32_t* pal, std::uint32_t n) {
        std::uint32_t cap = tableCap(n, Num, Den);
        slots_.assign(cap, kNone);
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(cap));
        for (std::uint32_t i = 0; i < n; ++i) place(pal[i], i);
    }
    std::size_t bytes() const { return slots_.capacity() * 4; }

private:
    void place(std::uint32_t id, std::uint32_t index) {
        std::uint64_t h = mulHash(id);
        std::uint32_t tag = static_cast<std::uint32_t>(h >> 24) & 0xFFu;
        std::uint32_t mask = static_cast<std::uint32_t>(slots_.size()) - 1;
        std::uint32_t s = static_cast<std::uint32_t>(h >> shift_);
        while (slots_[s] != kNone) s = (s + 1) & mask;
        slots_[s] = (tag << 24) | index;  // index < 2^24 - 1, so never kNone
    }
    std::vector<std::uint32_t> slots_;
    unsigned shift_ = 64;
};

// ---- Swiss-table style: 16-slot groups, one control byte per slot (0x80
// empty, else 7 hash bits), matched 16 at a time with SSE2; 4-byte slots
// holding the palette index, compared through the palette; load <= 7/8.
// Triangular probing over groups. No deletes, so no tombstones.
class Swiss {
public:
    static constexpr const char* kName = "swiss ctrl+idx 5B @7/8";
    std::uint32_t find(const std::uint32_t* pal, std::uint32_t id) const {
        if (slots_.empty()) return kNone;
        std::uint64_t h = mulHash(id);
        const __m128i h2 = _mm_set1_epi8(static_cast<char>((h >> 24) & 0x7F));
        const __m128i empty = _mm_set1_epi8(static_cast<char>(0x80));
        std::uint32_t g = static_cast<std::uint32_t>(h >> shift_);
        for (std::uint32_t step = 1;; g = (g + step++) & gmask_) {
            __m128i c = _mm_load_si128(reinterpret_cast<const __m128i*>(ctrl_.data() + 16 * g));
            unsigned m = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(c, h2)));
            while (m != 0) {
                unsigned b = static_cast<unsigned>(std::countr_zero(m));
                std::uint32_t v = slots_[16 * g + b];
                if (pal[v] == id) return v;
                m &= m - 1;
            }
            if (_mm_movemask_epi8(_mm_cmpeq_epi8(c, empty)) != 0) return kNone;
        }
    }
    void insert(const std::uint32_t* pal, std::uint32_t index) {
        if ((std::uint64_t{index} + 1) * 8 > std::uint64_t{slots_.size()} * 7) return rebuild(pal, index + 1);
        place(pal[index], index);
    }
    void rebuild(const std::uint32_t* pal, std::uint32_t n) {
        std::uint32_t cap = std::max<std::uint32_t>(32, tableCap(n, 7, 8));
        slots_.assign(cap, 0);
        ctrl_.assign(cap, static_cast<std::uint8_t>(0x80));
        gmask_ = cap / 16 - 1;
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(cap / 16));
        for (std::uint32_t i = 0; i < n; ++i) place(pal[i], i);
    }
    std::size_t bytes() const { return slots_.capacity() * 4 + ctrl_.capacity(); }

private:
    void place(std::uint32_t id, std::uint32_t index) {
        std::uint64_t h = mulHash(id);
        const __m128i empty = _mm_set1_epi8(static_cast<char>(0x80));
        std::uint32_t g = static_cast<std::uint32_t>(h >> shift_);
        for (std::uint32_t step = 1;; g = (g + step++) & gmask_) {
            __m128i c = _mm_load_si128(reinterpret_cast<const __m128i*>(ctrl_.data() + 16 * g));
            unsigned m = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(c, empty)));
            if (m != 0) {
                unsigned b = static_cast<unsigned>(std::countr_zero(m));
                ctrl_[16 * g + b] = static_cast<std::uint8_t>((h >> 24) & 0x7F);
                slots_[16 * g + b] = index;
                return;
            }
        }
    }
    struct alignas(16) Byte16 {
        std::uint8_t b[16];
    };
    // std::vector<uint8_t> is not guaranteed 16-aligned; use an aligned allocator-free trick.
    struct Ctrl {
        std::vector<Byte16> v;
        std::size_t n = 0;
        void assign(std::size_t count, std::uint8_t x) {
            v.assign((count + 15) / 16, Byte16{});
            for (auto& g : v) std::memset(g.b, x, 16);
            n = count;
        }
        std::uint8_t* data() { return v.empty() ? nullptr : v[0].b; }
        const std::uint8_t* data() const { return v.empty() ? nullptr : v[0].b; }
        std::uint8_t& operator[](std::size_t i) { return v[i / 16].b[i % 16]; }
        std::size_t capacity() const { return v.capacity() * 16; }
    };
    std::vector<std::uint32_t> slots_;
    Ctrl ctrl_;
    std::uint32_t gmask_ = 0;
    unsigned shift_ = 64;
};

}  // namespace proto::rl
