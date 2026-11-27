// GoalSource prototype tests with a toy octree client (refinements and
// meshes), a diamond, and a given result.
#include "../common/check.hpp"
#include "goal_source.hpp"

#include <algorithm>
#include <map>
#include <random>

using namespace proto;
using namespace proto::goal;

namespace {

constexpr ActorId kWorld = 1;     // the keeper of everything it wants
constexpr ActorId kStranger = 2;

std::uint64_t mix(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// ---- results
struct SiblingSet {
    std::array<std::uint64_t, 8> child;
};
struct Mesh {
    std::uint64_t value;
};
struct Num {
    std::int64_t v;
};

struct Log {
    std::vector<std::string> order;
};

// ---- keys: each kind's encode; nobody builds keys by hand
std::string encodeRefine(std::string_view path) { return "ON:" + std::string(path); }  // makes path's children
std::string encodeMesh(std::string_view path) { return "OM:" + std::string(path); }
std::vector<std::string> refineDeps(std::string_view path) {
    if (path.empty()) return {};
    return {encodeRefine(path.substr(0, path.size() - 1))};
}
std::vector<std::string> meshDeps(std::string_view path) { return {encodeRefine(path.substr(0, path.size() - 1))}; }

constexpr std::uint64_t kRootValue = 0x5EED;

class OctreeRefine final : public GoalActor {
public:
    explicit OctreeRefine(std::string_view key, Log* log) : path_(key.substr(3)), log_(log) {}
    void runTurn(GoalTurn& t) override {
        log_->order.emplace_back(t.key());
        std::uint64_t node = kRootValue;
        if (!path_.empty()) {
            auto parent = t.deps().get<SiblingSet>(0);  // typed borrow, this turn only
            node = parent->child[static_cast<std::size_t>(path_.back() - '0')];
        }
        SiblingSet s;
        for (std::size_t o = 0; o < 8; ++o) s.child[o] = mix(node * 8 + o);
        t.publish(t.make<SiblingSet>(s));
    }

private:
    std::string path_;
    Log* log_;
};

class OctreeMesh final : public GoalActor {
public:
    explicit OctreeMesh(std::string_view key, Log* log) : path_(key.substr(3)), log_(log) {}
    void runTurn(GoalTurn& t) override {
        log_->order.emplace_back(t.key());
        auto made = t.deps().get<SiblingSet>(0);
        std::uint64_t node = made->child[static_cast<std::size_t>(path_.back() - '0')];
        t.publish(t.make<Mesh>(Mesh{mix(node ^ 0x4D455348)}));
    }

private:
    std::string path_;
    Log* log_;
};

// Diamond: d needs b and c, both need a. "DX:<name>".
std::vector<std::string> diamondDeps(char n) {
    switch (n) {
        case 'b':
        case 'c': return {"DX:a"};
        case 'd': return {"DX:b", "DX:c"};
        default: return {};
    }
}
class Diamond final : public GoalActor {
public:
    explicit Diamond(std::string_view key, Log* log) : name_(key[3]), log_(log) {}
    void runTurn(GoalTurn& t) override {
        log_->order.emplace_back(t.key());
        std::int64_t v = 1;
        if (name_ == 'b') v = t.deps().get<Num>(0)->v * 3 + 1;
        if (name_ == 'c') v = t.deps().get<Num>(0)->v * 5 + 2;
        if (name_ == 'd') v = t.deps().get<Num>(0)->v * 7 + t.deps().get<Num>(1)->v;
        if (name_ == 'z') {  // abandons on its first turn
            static int n = 0;
            if (n++ == 0) {
                t.abandon();
                return;
            }
        }
        t.publish(t.make<Num>(Num{v}));
    }

private:
    char name_;
    Log* log_;
};

// A commit reads a given capture ("cap@N", no kind).
class Commit final : public GoalActor {
public:
    explicit Commit(std::string_view, Log* log) : log_(log) {}
    void runTurn(GoalTurn& t) override {
        log_->order.emplace_back(t.key());
        t.publish(t.make<Num>(Num{t.deps().get<Num>(0)->v + 1000}));
    }

private:
    Log* log_;
};

template <class A>
std::unique_ptr<GoalActor> inflate(std::string_view key, const GoalInputs& in) {
    return std::make_unique<A>(key, static_cast<Log*>(in.context));
}

struct Fixture {
    Log log;
    KindRegistry kinds;
    GoalSource gs{kinds};
    Fixture() {
        kinds.registerKind("ON:", &inflate<OctreeRefine>, {&log});
        kinds.registerKind("OM:", &inflate<OctreeMesh>, {&log});
        kinds.registerKind("DX:", &inflate<Diamond>, {&log});
        kinds.registerKind("SV:", &inflate<Commit>, {&log});
    }
};

template <class F>
bool fires(F&& f) {
    try {
        f();
    } catch (const FatalError&) {
        return true;
    }
    return false;
}

std::vector<std::string> paths(int depth) {
    std::vector<std::string> out{""}, level{""};
    for (int d = 0; d < depth; ++d) {
        std::vector<std::string> next;
        for (auto& p : level)
            for (char o = '0'; o < '8'; ++o) next.push_back(p + o);
        out.insert(out.end(), next.begin(), next.end());
        level = next;
    }
    return out;
}

struct Want {
    std::string key;
    std::vector<std::string> deps;
};

// The client's published DAG: refinements of nodes to depth 2 (making depth
// 3), meshes of every non-root node to depth 3.
std::vector<Want> octreePlan() {
    std::vector<Want> w;
    for (auto& p : paths(2)) w.push_back({encodeRefine(p), refineDeps(p)});
    for (auto& p : paths(3))
        if (!p.empty()) w.push_back({encodeMesh(p), meshDeps(p)});
    return w;
}

std::map<std::string, std::uint64_t> collect(GoalSource& gs, const std::vector<Want>& plan) {
    std::map<std::string, std::uint64_t> out;
    OwnerTurn t(kWorld);
    for (auto& w : plan) {
        if (w.key[1] == 'N') {
            auto r = gs.find<SiblingSet>(w.key);
            if (r) {
                std::uint64_t h = 0;
                for (auto c : r->child) h = mix(h ^ c);
                out[w.key] = h;
            }
        } else if (auto r = gs.find<Mesh>(w.key)) {
            out[w.key] = r->value;
        }
    }
    return out;
}

void testRegistry() {
    KindRegistry k;
    k.registerKind("ON:", &inflate<OctreeRefine>);
    k.registerKind("OM:", &inflate<OctreeMesh>);
    CHECK(fires([&] { k.registerKind("O", &inflate<OctreeMesh>); }));       // prefix of existing
    CHECK(fires([&] { k.registerKind("ON:x", &inflate<OctreeMesh>); }));    // existing is a prefix
    CHECK(fires([&] { k.registerKind("ON:", &inflate<OctreeMesh>); }));     // duplicate
    CHECK(k.find("ON:123").has_value());
    CHECK(k.find("OM:").has_value());
    CHECK(!k.find("OX:1").has_value());
    CHECK(!k.find("O").has_value());
    CHECK(k.find("ON:")->inflate == &inflate<OctreeRefine>);
    CHECK(k.nodeCount() == 6);  // root, O, N, ':', M, ':'
}

void testOrderIndependence() {
    const auto plan = octreePlan();
    std::map<std::string, std::uint64_t> reference;
    {
        Fixture f;
        {
            OwnerTurn t(kWorld);
            for (auto& w : plan) f.gs.want(t, w.key, w.deps, 0);
        }
        std::size_t ran = f.gs.runAll();
        CHECK(ran == plan.size());
        reference = collect(f.gs, plan);
        CHECK(reference.size() == plan.size());
        CHECK(f.gs.audit());
    }
    for (unsigned seed = 1; seed <= 20; ++seed) {
        Fixture f;
        std::mt19937_64 rng(seed);
        auto shuffled = plan;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);  // meshes often wanted before their refinements
        {
            OwnerTurn t(kWorld);
            for (auto& w : shuffled) f.gs.want(t, w.key, w.deps, static_cast<Priority>(rng() % 3));
        }
        std::size_t ran = f.gs.runAll([&](std::size_t n) { return static_cast<std::size_t>(rng() % n); });
        CHECK(ran == plan.size());
        CHECK(collect(f.gs, plan) == reference);
        CHECK(f.gs.audit());
    }
}

void testOutsideIn() {
    Fixture f;
    const auto plan = octreePlan();
    {
        OwnerTurn t(kWorld);
        for (auto& w : plan) f.gs.want(t, w.key, w.deps, 0);
        // A deep mesh wanted urgently: its whole chain inherits the priority.
        f.gs.want(t, encodeMesh("777"), meshDeps("777"), 10);
    }
    f.gs.runAll();
    const auto& order = f.log.order;
    REQUIRE(order.size() == plan.size());
    CHECK(order[0] == encodeRefine(""));
    CHECK(order[1] == encodeRefine("7"));
    CHECK(order[2] == encodeRefine("77"));
    CHECK(order[3] == encodeMesh("777"));
    std::map<std::string, std::size_t> pos;
    for (std::size_t i = 0; i < order.size(); ++i) pos[order[i]] = i;
    for (auto& w : plan)
        for (auto& d : w.deps) CHECK(pos.at(d) < pos.at(w.key));  // every dep ran first
}

void testImplicitDepsUnknown() {
    // Only meshes are wanted: their refinements are demanded implicitly, but
    // nobody has published their deps, so they never become runnable.
    Fixture f;
    {
        OwnerTurn t(kWorld);
        f.gs.want(t, encodeMesh("12"), meshDeps("12"), 0);
    }
    CHECK(f.gs.runAll() == 0);
    CHECK(f.gs.state(encodeRefine("1")) == GoalState::pending);
    {   // publishing the refinement chain unblocks it
        OwnerTurn t(kWorld);
        f.gs.want(t, encodeRefine("1"), refineDeps("1"), 0);
        f.gs.want(t, encodeRefine(""), refineDeps(""), 0);
        f.gs.drop(t, encodeRefine("1"));  // still alive: implicit demand from the mesh
        f.gs.drop(t, encodeRefine(""));
    }
    CHECK(f.gs.runAll() == 3);
    CHECK(f.gs.state(encodeMesh("12")) == GoalState::done);
    CHECK(f.gs.audit());
}

void testDropLeavesOnlyKept() {
    Fixture f;
    const auto plan = octreePlan();
    {
        OwnerTurn t(kWorld);
        for (auto& w : plan) f.gs.want(t, w.key, w.deps, 0);
    }
    f.gs.runAll();
    std::vector<std::string> keptKeys{encodeRefine(""), encodeRefine("3"), encodeRefine("35")};
    std::vector<Kept<const SiblingSet>> kept;
    {
        OwnerTurn t(kWorld);
        for (auto& k : keptKeys) kept.push_back(f.gs.keep<SiblingSet>(t, k));
    }
    {
        OwnerTurn s(kStranger);
        CHECK(fires([&] { (void)f.gs.keep<SiblingSet>(s, encodeRefine("1")); }));
        CHECK(fires([&] { f.gs.forget(s, encodeRefine("1")); }));
    }
    std::uint64_t root0 = kept[0]->child[0];
    {
        OwnerTurn t(kWorld);
        for (auto& w : plan) f.gs.drop(t, w.key);
    }
    f.gs.advanceEpoch();
    std::vector<std::string> sorted = keptKeys;
    std::sort(sorted.begin(), sorted.end());
    CHECK(f.gs.keys() == sorted);
    CHECK(f.gs.audit());
    CHECK(kept[0]->child[0] == root0);  // R5: valid across turns until forgotten
    CHECK(Slab<Mesh>::instance().live() == 0);
    {
        OwnerTurn t(kWorld);
        for (auto& k : keptKeys) f.gs.forget(t, k);
    }
    CHECK(f.gs.keys().empty());
    CHECK(f.gs.forgetsWhileDemanded() == 0);
    f.gs.advanceEpoch();
    CHECK(fires([&] { (void)kept[1]->child[0]; }));  // generation check after forget + epoch
    CHECK(Slab<SiblingSet>::instance().live() == 0);
}

void testForgetStillDemanded() {
    Fixture f;
    {
        OwnerTurn t(kWorld);
        for (char n : {'d', 'b', 'c', 'a'}) f.gs.want(t, std::string("DX:") + n, diamondDeps(n), 0);
    }
    CHECK(f.gs.runAll() == 4);
    std::int64_t d0;
    {
        OwnerTurn t(kWorld);
        d0 = f.gs.find<Num>("DX:d")->v;
        CHECK(d0 == (1 * 3 + 1) * 7 + (1 * 5 + 2));
        f.gs.forget(t, "DX:a");
    }
    CHECK(f.gs.forgetsWhileDemanded() == 1);
    CHECK(f.gs.state("DX:a") == GoalState::runnable);
    CHECK(f.gs.state("DX:b") == GoalState::done);  // done dependants keep their results
    f.log.order.clear();
    CHECK(f.gs.runAll() == 1);
    CHECK(f.log.order == std::vector<std::string>{"DX:a"});
    // Dependants that are not yet done lose their readiness.
    Fixture g;
    {
        OwnerTurn t(kWorld);
        g.gs.want(t, "DX:a", {}, 0);
        g.gs.want(t, "DX:b", {"DX:a"}, 0);
    }
    CHECK(g.gs.runOne());  // a
    CHECK(g.gs.state("DX:b") == GoalState::runnable);
    {
        OwnerTurn t(kWorld);
        g.gs.forget(t, "DX:a");
    }
    CHECK(g.gs.state("DX:b") == GoalState::pending);
    CHECK(g.gs.forgetsWhileDemanded() == 1);
    CHECK(g.gs.runAll() == 2);
    CHECK(g.gs.state("DX:b") == GoalState::done);
    CHECK(g.gs.audit());
}

void testAsserts() {
    Fixture f;
    OwnerTurn t(kWorld);
    f.gs.want(t, "DX:b", {"DX:a"}, 0);
    CHECK(fires([&] { f.gs.want(t, "DX:b", {"DX:c"}, 0); }));  // deps are a function of the key
    Fixture g;
    g.gs.want(t, "DX:p", {"DX:q"}, 0);
    g.gs.want(t, "DX:q", {"DX:r"}, 0);
    CHECK(fires([&] { g.gs.want(t, "DX:r", {"DX:p"}, 0); }));  // cycle
}

void testTypedAccessAndBorrows() {
    Fixture f;
    {
        OwnerTurn t(kWorld);
        f.gs.want(t, encodeRefine(""), {}, 0);
    }
    f.gs.runAll();
    {
        OwnerTurn t(kWorld);
        CHECK(fires([&] { (void)f.gs.find<Mesh>(encodeRefine("")); }));  // type id mismatch, fatal
    }
    std::optional<Borrow<const SiblingSet>> escaped;
    {
        OwnerTurn t(kWorld);
        escaped.emplace(f.gs.find<SiblingSet>(encodeRefine("")));
        CHECK((*escaped)->child[0] != 0);
    }
    CHECK(fires([&] { (void)(*escaped)->child[0]; }));  // R4: outlived its turn
    {
        OwnerTurn t(kWorld);
        CHECK(fires([&] { (void)(*escaped)->child[0]; }));  // a later turn has another serial
        CHECK(!f.gs.find<SiblingSet>("ON:9"));             // absent: an empty borrow
    }
}

void testGivenAndRunningDrop() {
    Fixture f;
    {
        OwnerTurn t(kWorld);
        f.gs.want(t, "SV:commit@1", {"cap@1"}, 0);
    }
    CHECK(f.gs.runAll() == 0);  // waiting on the given capture
    {
        OwnerTurn t(kWorld);
        CHECK(fires([&] { f.gs.give(t, "DX:a", makeSlab<Num>(Num{1})); }));  // a computed kind's key
    }
    Fixture g;  // fresh: the fatal above left f's bookkeeping in an unknown state
    {
        OwnerTurn t(kWorld);
        g.gs.want(t, "SV:commit@1", {"cap@1"}, 0);
        g.gs.give(t, "cap@1", makeSlab<Num>(Num{7}));
    }
    CHECK(g.gs.runAll() == 1);
    {
        OwnerTurn t(kWorld);
        CHECK(g.gs.find<Num>("SV:commit@1")->v == 1007);
        g.gs.drop(t, "SV:commit@1");
    }
    CHECK(g.gs.keys() == std::vector<std::string>{"cap@1"});  // the given result is kept by its giver
    // A goal dropped while running: its result is forgotten at once.
    {
        OwnerTurn t(kWorld);
        g.gs.want(t, "DX:a", {}, 0);
    }
    auto b = g.gs.take();
    REQUIRE(b.has_value());
    {
        OwnerTurn t(kWorld);
        g.gs.drop(t, "DX:a");
    }
    CHECK(g.gs.state("DX:a") == GoalState::running);
    g.gs.run(std::move(*b));
    CHECK(!g.gs.state("DX:a").has_value());
    // Abandonment: back to pending (parked) while wanted; the next want retries.
    {
        OwnerTurn t(kWorld);
        g.gs.want(t, "DX:z", {}, 0);
    }
    CHECK(g.gs.runAll() == 1);
    CHECK(g.gs.state("DX:z") == GoalState::pending);
    CHECK(g.gs.runAll() == 0);
    {
        OwnerTurn t(kWorld);
        g.gs.want(t, "DX:z", {}, 0);
    }
    CHECK(g.gs.runAll() == 1);
    CHECK(g.gs.state("DX:z") == GoalState::done);
    g.gs.advanceEpoch();
    CHECK(g.gs.audit());
}

}  // namespace

int main() {
    fatalHandler() = &throwingFatalHandler;
    testRegistry();
    testOrderIndependence();
    testOutsideIn();
    testImplicitDepsUnknown();
    testDropLeavesOnlyKept();
    testForgetStillDemanded();
    testAsserts();
    testTypedAccessAndBorrows();
    testGivenAndRunningDrop();
    return check::result("test_goal_source");
}
