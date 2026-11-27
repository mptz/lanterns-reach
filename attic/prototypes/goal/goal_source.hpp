// Prototype of engine/goal-source (design/goal-source.md) with the pointer
// rules' Borrow/Kept (design/concurrency.md, R1-R6). Single-threaded with an
// inline executor: the caller takes a batch and runs it; the epoch advances
// when the caller says no turn is in flight. The mutex and annotations are
// kept so the locking discipline is checked by -Wthread-safety anyway.
#pragma once

#include "../common/fatal.hpp"
#include "../common/thread_safety.hpp"

#include <array>
#include <climits>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace proto::goal {

// ---- type ids without RTTI: &tag<T>
template <class T>
inline constexpr char kTypeTag = 0;
using TypeId = const void*;
template <class T>
constexpr TypeId typeId() {
    return &kTypeTag<T>;
}

// ---- work-item serials (R4)
inline thread_local std::uint64_t tCurrentSerial = 0;
inline std::uint64_t nextSerial() {
    static std::uint64_t s = 0;
    return ++s;
}

using ActorId = std::uint32_t;

// A turn of a client actor (the World, the Surveyor): a work item with its
// own serial. Only one at a time on a thread.
class OwnerTurn {
public:
    explicit OwnerTurn(ActorId a) : actor_(a), serial_(nextSerial()) {
        PROTO_ASSERT(tCurrentSerial == 0, "nested turns");
        tCurrentSerial = serial_;
    }
    ~OwnerTurn() { tCurrentSerial = 0; }
    OwnerTurn(const OwnerTurn&) = delete;
    OwnerTurn& operator=(const OwnerTurn&) = delete;
    ActorId actor() const { return actor_; }

private:
    ActorId actor_;
    std::uint64_t serial_;
};

// ---- type-stable memory: slabs never unmapped; freeing bumps the generation
struct SlotHeader {
    std::uint32_t generation = 0;
    bool live = false;
};

template <class T>
struct Slot {
    SlotHeader h;
    alignas(T) unsigned char storage[sizeof(T)];
    T* obj() { return std::launder(reinterpret_cast<T*>(storage)); }
};

template <class T>
class Slab {
public:
    static Slab& instance() {
        static Slab s;
        return s;
    }
    template <class... A>
    Slot<T>* alloc(A&&... a) {
        Slot<T>* s;
        if (!free_.empty()) {
            s = free_.back();
            free_.pop_back();
        } else {
            s = &slots_.emplace_back();
        }
        ::new (static_cast<void*>(s->storage)) T(std::forward<A>(a)...);
        s->h.live = true;
        ++liveCount_;
        return s;
    }
    void release(Slot<T>* s) {
        PROTO_ASSERT(s->h.live, "slab double free");
        s->obj()->~T();
        s->h.live = false;
        ++s->h.generation;
        free_.push_back(s);
        --liveCount_;
    }
    std::size_t live() const { return liveCount_; }
    static void releaseErased(SlotHeader* h) { instance().release(reinterpret_cast<Slot<T>*>(h)); }

private:
    std::deque<Slot<T>> slots_;  // deque: element addresses are stable
    std::vector<Slot<T>*> free_;
    std::size_t liveCount_ = 0;
};

// An owning, unpublished slab object (R1: move-only).
template <class T>
class SlabPtr {
public:
    SlabPtr() = default;
    explicit SlabPtr(Slot<T>* s) : s_(s) {}
    SlabPtr(SlabPtr&& o) noexcept : s_(std::exchange(o.s_, nullptr)) {}
    SlabPtr& operator=(SlabPtr&& o) noexcept {
        reset();
        s_ = std::exchange(o.s_, nullptr);
        return *this;
    }
    ~SlabPtr() { reset(); }
    void reset() {
        if (s_) Slab<T>::instance().release(std::exchange(s_, nullptr));
    }
    Slot<T>* release() { return std::exchange(s_, nullptr); }
    T* get() const { return s_ ? s_->obj() : nullptr; }

private:
    Slot<T>* s_ = nullptr;
};

template <class T, class... A>
SlabPtr<T> makeSlab(A&&... a) {
    return SlabPtr<T>(Slab<T>::instance().alloc(std::forward<A>(a)...));
}

// A published result's slot record, type-erased.
struct ResultSlot {
    TypeId type = nullptr;
    SlotHeader* header = nullptr;
    const void* object = nullptr;
    std::uint32_t generation = 0;
    void (*release)(SlotHeader*) = nullptr;
};

template <class T>
ResultSlot toResultSlot(Slot<T>* s) {
    return ResultSlot{typeId<T>(), &s->h, s->obj(), s->h.generation, &Slab<T>::releaseErased};
}

// R4: a turn-scoped borrow. No default constructor, no raw-pointer escape.
template <class T>
class Borrow {
public:
    explicit operator bool() const { return p_ != nullptr; }
    T& operator*() const {
        check();
        return *p_;
    }
    T* operator->() const {
        check();
        return p_;
    }

private:
    template <class>
    friend class BorrowMaker;
    Borrow(const SlotHeader* h, T* p, std::uint32_t gen, std::uint64_t serial)
        : h_(h), p_(p), gen_(gen), serial_(serial) {}
    void check() const {
        PROTO_ASSERT(p_ != nullptr, "dereferencing an empty borrow");
        PROTO_ASSERT(serial_ != 0 && serial_ == tCurrentSerial, "R4: borrow used outside its turn");
        PROTO_ASSERT(h_->generation == gen_, "R3: borrowed object was freed");
    }
    const SlotHeader* h_;
    T* p_;
    std::uint32_t gen_;
    std::uint64_t serial_;
};

template <class T>
class BorrowMaker {
public:
    static Borrow<T> make(const SlotHeader* h, T* p, std::uint32_t g, std::uint64_t s) { return Borrow<T>(h, p, g, s); }
    static Borrow<T> empty() { return Borrow<T>(nullptr, nullptr, 0, 0); }
};

// R5: the keeper's pointer across its own turns, until it forgets the key.
template <class T>
class Kept {
public:
    Kept() = default;
    explicit operator bool() const { return p_ != nullptr; }
    T& operator*() const {
        check();
        return *p_;
    }
    T* operator->() const {
        check();
        return p_;
    }

private:
    friend class GoalSource;
    Kept(const SlotHeader* h, T* p, std::uint32_t gen) : h_(h), p_(p), gen_(gen) {}
    void check() const {
        PROTO_ASSERT(p_ != nullptr, "dereferencing an empty Kept");
        PROTO_ASSERT(h_->generation == gen_, "R5: kept object used after forget");
    }
    const SlotHeader* h_ = nullptr;
    T* p_ = nullptr;
    std::uint32_t gen_ = 0;
};

// Dependencies' slots, copied in take, in the order the deps were listed.
class DepView {
public:
    std::size_t size() const { return slots_.size(); }
    template <class T>
    Borrow<const T> get(std::size_t i) const {
        PROTO_ASSERT(i < slots_.size(), "dep index out of range");
        const ResultSlot& s = slots_[i];
        PROTO_ASSERT(s.type == typeId<T>(), "dep result type mismatch");
        return BorrowMaker<const T>::make(s.header, static_cast<const T*>(s.object), s.generation, serial_);
    }

private:
    friend class GoalSource;
    std::vector<ResultSlot> slots_;
    std::uint64_t serial_ = 0;
};

class GoalTurn {
public:
    std::string_view key() const { return key_; }
    const DepView& deps() const { return deps_; }
    template <class T, class... A>
    SlabPtr<T> make(A&&... a) {
        return makeSlab<T>(std::forward<A>(a)...);
    }
    template <class T>
    void publish(SlabPtr<T>&& p) {
        PROTO_ASSERT(!published_, "a turn publishes once");
        Slot<T>* s = p.release();
        PROTO_ASSERT(s != nullptr, "publishing nothing");
        published_ = toResultSlot(s);
    }
    void abandon() { abandoned_ = true; }

private:
    friend class GoalSource;
    GoalTurn(std::string_view k, const DepView& d) : key_(k), deps_(d) {}
    std::string_view key_;
    const DepView& deps_;
    std::optional<ResultSlot> published_;
    bool abandoned_ = false;
};

class GoalActor {
public:
    virtual ~GoalActor() = default;
    virtual void runTurn(GoalTurn&) = 0;
};

// What a factory gets besides the key. The spec names GoalInputs without
// defining it; here it is a client context registered with the kind.
struct GoalInputs {
    void* context = nullptr;
};

using Inflate = std::unique_ptr<GoalActor> (*)(std::string_view key, const GoalInputs&);

// A prefix-free trie of kinds, one character per level, 256-entry tables.
class KindRegistry {
public:
    struct Kind {
        Inflate inflate = nullptr;
        const GoalInputs* inputs = nullptr;
    };

    KindRegistry() { nodes_.emplace_back(); }

    void registerKind(std::string_view prefix, Inflate f, GoalInputs in = {}) {
        PROTO_ASSERT(!prefix.empty(), "empty kind prefix");
        PROTO_ASSERT(f != nullptr, "null factory");
        std::uint32_t n = 0;
        for (unsigned char c : prefix) {
            PROTO_ASSERT(!nodes_[n].terminal, "prefix-free: an existing kind is a prefix of this one");
            std::int32_t nx = nodes_[n].next[c];
            if (nx < 0) {
                nx = static_cast<std::int32_t>(nodes_.size());
                nodes_[n].next[c] = nx;
                nodes_.emplace_back();
            }
            n = static_cast<std::uint32_t>(nx);
        }
        PROTO_ASSERT(!nodes_[n].terminal, "kind registered twice");
        for (std::int32_t x : nodes_[n].next) PROTO_ASSERT(x < 0, "prefix-free: this kind is a prefix of an existing one");
        nodes_[n].terminal = true;
        nodes_[n].inflate = f;
        inputs_.push_back(std::make_unique<GoalInputs>(in));
        nodes_[n].inputs = inputs_.back().get();
    }

    std::optional<Kind> find(std::string_view key) const {
        std::uint32_t n = 0;
        for (unsigned char c : key) {
            std::int32_t nx = nodes_[n].next[c];
            if (nx < 0) return std::nullopt;
            n = static_cast<std::uint32_t>(nx);
            if (nodes_[n].terminal) return Kind{nodes_[n].inflate, nodes_[n].inputs};
        }
        return std::nullopt;
    }

    std::size_t nodeCount() const { return nodes_.size(); }

private:
    struct Node {
        std::array<std::int32_t, 256> next;
        bool terminal = false;
        Inflate inflate = nullptr;
        const GoalInputs* inputs = nullptr;
        Node() { next.fill(-1); }
    };
    std::vector<Node> nodes_;
    std::vector<std::unique_ptr<GoalInputs>> inputs_;
};

using Priority = std::int32_t;
enum class GoalState { pending, runnable, running, done };

struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

class GoalSource {
public:
    struct Batch {
        std::string key;
        DepView deps;
        KindRegistry::Kind kind;
        std::uint64_t serial = 0;
    };

    explicit GoalSource(const KindRegistry& kinds) : kinds_(kinds) {}
    ~GoalSource() {
        Lock l(m_);
        for (auto& [k, g] : goals_)
            if (g.result) retired_.push_back(*g.result);
        for (const ResultSlot& r : retired_) r.release(r.header);
    }
    GoalSource(const GoalSource&) = delete;
    GoalSource& operator=(const GoalSource&) = delete;

    void want(OwnerTurn& t, std::string key, std::vector<std::string> deps, Priority prio) TS_EXCLUDES(m_) {
        Lock l(m_);
        for (const std::string& d : deps) {
            PROTO_ASSERT(d != key, "invariant 7: a goal depends on itself");
            PROTO_ASSERT(!reaches(d, key), "invariant 7: dependency cycle");
        }
        Goal& g = ensure(key);
        bool newlyKnown = false;
        if (g.depsKnown) {
            PROTO_ASSERT(g.deps == deps, "invariant 2: deps are a function of the key");
        } else {
            g.deps = std::move(deps);
            g.depsKnown = true;
            newlyKnown = true;
        }
        const bool was = g.demanded();
        ++g.direct;
        if (!g.keeper) g.keeper = t.actor();
        if (was && newlyKnown) registerDeps(key, g);
        else if (!was) gainDemand(key);
        raise(key, prio);
        Goal& g2 = goals_.find(key)->second;
        if (g2.parked) g2.parked = false;  // "the next demand retries"
        if (g2.state == GoalState::pending && newlyKnown) recountUnmet(g2);
        refreshRunnable(key);
    }

    void drop(OwnerTurn&, std::string_view key) TS_EXCLUDES(m_) {
        Lock l(m_);
        auto it = goals_.find(key);
        PROTO_ASSERT(it != goals_.end() && it->second.direct > 0, "drop without a want");
        --it->second.direct;
        if (!it->second.demanded()) loseDemand(std::string(key));
    }

    // A given result: done at once, no deps, kept by the giver.
    template <class T>
    void give(OwnerTurn& t, std::string key, SlabPtr<T>&& r) TS_EXCLUDES(m_) {
        Lock l(m_);
        PROTO_ASSERT(!kinds_.find(key), "a given key must not belong to a computed kind");
        Goal& g = ensure(key);
        PROTO_ASSERT(g.state != GoalState::done, "given twice without forget");
        PROTO_ASSERT(!g.depsKnown || g.deps.empty(), "a given key has no deps");
        PROTO_ASSERT(!g.keeper || *g.keeper == t.actor(), "give by a non-keeper");
        g.keeper = t.actor();
        g.given = true;
        g.depsKnown = true;
        g.kept = true;
        Slot<T>* s = r.release();
        PROTO_ASSERT(s != nullptr, "giving nothing");
        complete(key, toResultSlot(s));
    }

    void forget(OwnerTurn& t, std::string_view key) TS_EXCLUDES(m_) {
        Lock l(m_);
        auto it = goals_.find(key);
        PROTO_ASSERT(it != goals_.end(), "forget of an unknown key");
        Goal& g = it->second;
        PROTO_ASSERT(g.keeper && *g.keeper == t.actor(), "forget by a non-keeper");
        if (g.state != GoalState::done) return;
        retired_.push_back(*g.result);
        g.result.reset();
        g.kept = false;
        std::string k(key);
        if (!g.demanded()) {
            goals_.erase(it);
            return;
        }
        ++forgetsWhileDemanded_;
        g.state = GoalState::pending;
        recountUnmet(g);
        for (const std::string& dk : g.dependants) {
            Goal& d = goals_.find(dk)->second;
            if (d.state == GoalState::runnable) {
                runnable_.erase({d.priority, dk});
                d.state = GoalState::pending;
            }
            if (d.state == GoalState::pending) ++d.unmet;
        }
        refreshRunnable(k);
    }

    template <class T>
    Borrow<const T> find(std::string_view key) const TS_EXCLUDES(m_) {
        Lock l(m_);
        PROTO_ASSERT(tCurrentSerial != 0, "find outside a turn");
        auto it = goals_.find(key);
        if (it == goals_.end() || it->second.state != GoalState::done) return BorrowMaker<const T>::empty();
        const ResultSlot& s = *it->second.result;
        PROTO_ASSERT(s.type == typeId<T>(), "find: result type mismatch");
        return BorrowMaker<const T>::make(s.header, static_cast<const T*>(s.object), s.generation, tCurrentSerial);
    }

    template <class T>
    Kept<const T> keep(OwnerTurn& t, std::string_view key) TS_EXCLUDES(m_) {
        Lock l(m_);
        auto it = goals_.find(key);
        PROTO_ASSERT(it != goals_.end(), "keep of an unknown key");
        Goal& g = it->second;
        PROTO_ASSERT(g.keeper && *g.keeper == t.actor(), "keep by a non-keeper");
        if (g.state != GoalState::done) return {};
        PROTO_ASSERT(g.result->type == typeId<T>(), "keep: result type mismatch");
        g.kept = true;
        return Kept<const T>(g.result->header, static_cast<const T*>(g.result->object), g.result->generation);
    }

    // ---- the Dispenser side (inline executor) ----
    // pick(n) chooses among the n runnable goals in rank order; nullptr = best.
    std::optional<Batch> take(const std::function<std::size_t(std::size_t)>& pick = nullptr) TS_EXCLUDES(m_) {
        Lock l(m_);
        if (runnable_.empty()) return std::nullopt;
        auto rit = runnable_.begin();
        if (pick) std::advance(rit, static_cast<std::ptrdiff_t>(pick(runnable_.size()) % runnable_.size()));
        std::string key = rit->second;
        runnable_.erase(rit);
        Goal& g = goals_.find(key)->second;
        PROTO_ASSERT(g.state == GoalState::runnable, "invariant 4: claiming a goal that isn't runnable");
        g.state = GoalState::running;
        Batch b;
        b.serial = nextSerial();
        b.deps.serial_ = b.serial;
        for (const std::string& d : g.deps) {
            auto dit = goals_.find(d);
            PROTO_ASSERT(dit != goals_.end() && dit->second.state == GoalState::done,
                         "invariant 4: a dependency isn't done at claim");
            b.deps.slots_.push_back(*dit->second.result);
        }
        auto kind = kinds_.find(key);
        PROTO_ASSERT(kind.has_value(), "runnable goal without a kind");
        b.kind = *kind;
        b.key = std::move(key);
        return b;
    }

    // Inflate and run outside the mutex, then complete under it.
    void run(Batch b) TS_EXCLUDES(m_) {
        PROTO_ASSERT(!m_.heldByThisThread(), "invariant 6: turn run under the mutex");
        PROTO_ASSERT(tCurrentSerial == 0, "run inside another turn");
        tCurrentSerial = b.serial;
        GoalTurn turn(b.key, b.deps);
        {
            std::unique_ptr<GoalActor> actor = b.kind.inflate(b.key, *b.kind.inputs);
            PROTO_ASSERT(actor != nullptr, "factory returned null");
            actor->runTurn(turn);
        }  // the actor lives one turn
        tCurrentSerial = 0;
        Lock l(m_);
        auto it = goals_.find(b.key);
        PROTO_ASSERT(it != goals_.end() && it->second.state == GoalState::running, "running goal vanished");
        if (turn.published_) {
            complete(b.key, *turn.published_);
            return;
        }
        PROTO_ASSERT(turn.abandoned_, "a turn ended without publishing or abandoning");
        Goal& g = it->second;
        if (g.demanded()) {
            g.state = GoalState::pending;
            g.parked = true;  // retried on the next want (NOTES.md)
            recountUnmet(g);
        } else {
            goals_.erase(it);
        }
    }

    bool runOne(const std::function<std::size_t(std::size_t)>& pick = nullptr) TS_EXCLUDES(m_) {
        auto b = take(pick);
        if (!b) return false;
        run(std::move(*b));
        advanceEpoch();
        return true;
    }

    std::size_t runAll(const std::function<std::size_t(std::size_t)>& pick = nullptr) TS_EXCLUDES(m_) {
        std::size_t n = 0;
        while (runOne(pick)) ++n;
        return n;
    }

    // Inline executor: called only when no turn is in flight.
    void advanceEpoch() TS_EXCLUDES(m_) {
        std::vector<ResultSlot> r;
        {
            Lock l(m_);
            r.swap(retired_);
        }
        for (const ResultSlot& s : r) s.release(s.header);
    }

    // ---- telemetry and test inspection ----
    std::uint64_t forgetsWhileDemanded() const TS_EXCLUDES(m_) {
        Lock l(m_);
        return forgetsWhileDemanded_;
    }
    std::optional<GoalState> state(std::string_view key) const TS_EXCLUDES(m_) {
        Lock l(m_);
        auto it = goals_.find(key);
        if (it == goals_.end()) return std::nullopt;
        return it->second.state;
    }
    std::vector<std::string> keys() const TS_EXCLUDES(m_) {
        Lock l(m_);
        std::set<std::string> s;
        for (auto& [k, g] : goals_) s.insert(k);
        return {s.begin(), s.end()};
    }
    std::size_t runnableCount() const TS_EXCLUDES(m_) {
        Lock l(m_);
        return runnable_.size();
    }
    // Debug audit: implicit demand equals the demanded dependants listing a key.
    bool audit() const TS_EXCLUDES(m_) {
        Lock l(m_);
        for (auto& [k, g] : goals_) {
            std::size_t n = 0;
            for (auto& [k2, g2] : goals_)
                if (g2.demanded() && g2.depsKnown)
                    for (auto& d : g2.deps) n += d == k;
            if (n != g.dependants.size()) return false;
            if (g.state == GoalState::done && !g.result) return false;
            if (g.state != GoalState::running && !g.demanded() && !(g.state == GoalState::done && g.kept))
                return false;  // only kept results outlive their demand
        }
        return true;
    }

private:
    struct RankLess {
        bool operator()(const std::pair<Priority, std::string>& a, const std::pair<Priority, std::string>& b) const {
            if (a.first != b.first) return a.first > b.first;  // higher priority first
            return a.second < b.second;
        }
    };

    struct Goal {
        GoalState state = GoalState::pending;
        bool depsKnown = false;
        std::vector<std::string> deps;
        std::uint32_t unmet = 0;  // deps not done, while pending
        std::uint32_t direct = 0;
        std::set<std::string, std::less<>> dependants;  // demanded goals listing this key; implicit demand
        Priority priority = INT_MIN;
        std::optional<ActorId> keeper;
        bool kept = false;
        bool given = false;
        bool parked = false;
        std::optional<ResultSlot> result;
        bool demanded() const { return direct > 0 || !dependants.empty(); }
    };

    Goal& ensure(const std::string& key) TS_REQUIRES(m_) { return goals_.try_emplace(key).first->second; }

    // Does `from` reach `to` through known deps? (debug cycle detection)
    bool reaches(const std::string& from, const std::string& to) const TS_REQUIRES(m_) {
        std::vector<const std::string*> stack{&from};
        std::set<std::string_view> seen;
        while (!stack.empty()) {
            const std::string* k = stack.back();
            stack.pop_back();
            if (*k == to) return true;
            if (!seen.insert(*k).second) continue;
            auto it = goals_.find(*k);
            if (it == goals_.end() || !it->second.depsKnown) continue;
            for (const std::string& d : it->second.deps) stack.push_back(&d);
        }
        return false;
    }

    void registerDeps(const std::string& key, Goal& g) TS_REQUIRES(m_) {
        for (const std::string& d : g.deps) {
            Goal& dg = ensure(d);
            const bool was = dg.demanded();
            dg.dependants.insert(key);
            if (!was) gainDemand(d);
            raise(d, g.priority);
        }
    }

    void gainDemand(const std::string& key) TS_REQUIRES(m_) {
        Goal& g = goals_.find(key)->second;
        if (g.depsKnown) registerDeps(key, g);
    }

    void loseDemand(const std::string& key) TS_REQUIRES(m_) {
        auto it = goals_.find(key);
        Goal& g = it->second;
        if (g.depsKnown) {
            for (const std::string& d : g.deps) {
                auto dit = goals_.find(d);
                PROTO_ASSERT(dit != goals_.end(), "a demanded goal's dep is missing");
                dit->second.dependants.erase(key);
                if (!dit->second.demanded()) loseDemand(d);
            }
        }
        switch (g.state) {
            case GoalState::runnable: runnable_.erase({g.priority, key}); [[fallthrough]];
            case GoalState::pending: goals_.erase(it); break;
            case GoalState::running: break;  // completion decides
            case GoalState::done:
                if (!g.kept) {
                    retired_.push_back(*g.result);
                    goals_.erase(it);
                }
                break;
        }
    }

    // Preconditions inherit their demander's priority; it never falls (open).
    void raise(const std::string& key, Priority p) TS_REQUIRES(m_) {
        Goal& g = goals_.find(key)->second;
        if (g.priority >= p) return;
        if (g.state == GoalState::runnable) {
            runnable_.erase({g.priority, key});
            runnable_.insert({p, key});
        }
        g.priority = p;
        if (g.depsKnown)
            for (const std::string& d : g.deps) raise(d, p);
    }

    void recountUnmet(Goal& g) TS_REQUIRES(m_) {
        g.unmet = 0;
        for (const std::string& d : g.deps) {
            auto it = goals_.find(d);
            if (it == goals_.end() || it->second.state != GoalState::done) ++g.unmet;
        }
    }

    void refreshRunnable(const std::string& key) TS_REQUIRES(m_) {
        Goal& g = goals_.find(key)->second;
        if (g.state == GoalState::pending && g.depsKnown && !g.given && !g.parked && g.unmet == 0 &&
            kinds_.find(key)) {
            g.state = GoalState::runnable;
            runnable_.insert({g.priority, key});
        }
    }

    void complete(const std::string& key, ResultSlot r) TS_REQUIRES(m_) {
        auto it = goals_.find(key);
        Goal& g = it->second;
        g.state = GoalState::done;
        g.result = r;
        for (const std::string& dk : g.dependants) {
            Goal& d = goals_.find(dk)->second;
            if (d.state == GoalState::pending && d.unmet > 0 && --d.unmet == 0) refreshRunnable(dk);
        }
        if (!g.demanded() && !g.kept) {  // abandoned while running: forgotten at once
            retired_.push_back(r);
            goals_.erase(it);
        }
    }

    const KindRegistry& kinds_;
    mutable Mutex m_;
    std::unordered_map<std::string, Goal, StringHash, std::equal_to<>> goals_ TS_GUARDED_BY(m_);
    std::set<std::pair<Priority, std::string>, RankLess> runnable_ TS_GUARDED_BY(m_);
    std::vector<ResultSlot> retired_ TS_GUARDED_BY(m_);
    std::uint64_t forgetsWhileDemanded_ TS_GUARDED_BY(m_) = 0;
};

}  // namespace proto::goal
