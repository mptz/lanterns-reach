// Tests: block definitions' ULIDs, the canonical text, state hashes and the
// state table (world/block-states, data/stable-identity, world/block-orientation;
// versions.md v0.2.4 acceptance 4, 5 and 7). Registered through
// sandbox_tests.cpp.

#include <map>
#include <set>

#include "test_support.h"

namespace {

using namespace orient;

// A fixture definition's ULID (not minted: only these tests use it).
const Ulid kFixtureUlid = *Ulid::parse("01M8F3N35FMTBBXQ1NFX22HGF0");

PropertyDecl flag(const char* name) { return {name, {"true"}, false, 0}; }
PropertyDecl optionalEnum(const char* name, std::vector<std::string> values) { return {name, std::move(values), false, 0}; }
PropertyDecl mandatoryEnum(const char* name, std::vector<std::string> values, std::uint32_t initial) {
    return {name, std::move(values), true, initial};
}

// The fixture: column symmetry (3 orientations), a flag, an optional enum and
// a mandatory one: 3 × 2 × 4 × 2 = 48 states. In name order the properties
// are color, lit, stage.
std::vector<PropertyDecl> fixtureProperties() {
    return {flag("lit"), optionalEnum("stage", {"a", "b", "c"}), mandatoryEnum("color", {"red", "blue"}, 1)};
}

std::uint32_t addFixture(StateTable& t, std::vector<PropertyDecl> properties, bool internAll = true) {
    return t.addDefinition(kFixtureUlid, "fixture", 0, nullptr, Symmetry::Column, std::move(properties), internAll);
}

// The fixture's state: value indices as the table holds them (color 0 red, 1
// blue; lit 0 absent, 1 true; stage 0 absent, 1 a, 2 b, 3 c).
StateId fixtureState(StateTable& t, std::uint32_t def, Rot r, std::uint32_t color, std::uint32_t lit, std::uint32_t stage) {
    const std::uint32_t values[3] = {color, lit, stage};
    return t.intern(def, r, values);
}

// Every state of a definition, by its dense index.
std::vector<StateId> allStates(StateTable& t, std::uint32_t def) {
    std::vector<StateId> out;
    for (StateId s : t.definitionAt(def).dense) out.push_back(s);
    return out;
}

void testAir() {
    CHECK(StateTable::airUlid().text() == "01M8F3N35FMTBBXQ1NFX22HGFS");
    StateTable t;
    CHECK(t.size() == 1 && t.isAir(kAirState) && t.text(kAirState) == "01M8F3N35FMTBBXQ1NFX22HGFS");
    CHECK(t.visibility(kAirState) == Visibility::Air && t.type(kAirState) == kAirType);
    CHECK(registry().states().text(kAirState) == t.text(kAirState) && registry().states().hash(kAirState) == t.hash(kAirState));
}

// The canonical text, exactly: names sorted; an absent optional property not
// written, a mandatory one always; values by name; orientation by the images
// of +x and +z.
void testCanonicalText() {
    StateTable t;
    const std::uint32_t def = addFixture(t, fixtureProperties());
    CHECK(t.definitionAt(def).stateCount == 48 && t.size() == 1 + 48);
    const std::string u = kFixtureUlid.text();
    CHECK(t.text(fixtureState(t, def, kIdentity, 0, 0, 0)) == u + "[color=red]");
    CHECK(t.text(fixtureState(t, def, kIdentity, 1, 1, 0)) == u + "[color=blue,lit=true]");
    CHECK(t.text(fixtureState(t, def, kIdentity, 0, 1, 3)) == u + "[color=red,lit=true,stage=c]");
    // Turned so its axis lies along x: its +z goes to +x (the coset's first member sends +x to +y).
    CHECK(t.text(fixtureState(t, def, rotationAbout({0, 1, 0}, 90), 0, 0, 2)) == u + "[color=red,orientation=+y+x,stage=b]");
    // The starting state: unrotated, optional properties absent, mandatory at their initial value.
    CHECK(t.defaultState(def) == fixtureState(t, def, kIdentity, 1, 0, 0));
    // Pinned: a change to the text, the context string or BLAKE3 is a visible decision.
    CHECK(t.hash(t.defaultState(def)).text() == "0N91W2PYNTR888D7AGPQRXXD2S");
    CHECK(t.hash(fixtureState(t, def, kIdentity, 0, 0, 0)).text() == "2T4JJ3A3JH1V4FF6MFD1WNBEYY");
}


// How identity evolves: adding an optional property leaves every hash; adding
// a mandatory one changes every hash; reordering a domain's values, or
// changing a mandatory property's initial value, changes none.
void testEvolution() {
    StateTable a;
    const std::uint32_t da = addFixture(a, fixtureProperties());
    std::map<std::string, StateHash> base;  // by a readable key: orientation and named values
    auto key = [](StateTable& t, StateId s) {
        return std::to_string(t.orientation(s)) + "/" + std::string(t.value(s, "color")) + "/" + std::string(t.value(s, "lit")) +
               "/" + std::string(t.value(s, "stage"));
    };
    for (StateId s : allStates(a, da)) base[key(a, s)] = a.hash(s);
    CHECK(base.size() == 48);

    StateTable b;  // + an optional flag: every existing state is the one with it absent
    std::vector<PropertyDecl> withWet = fixtureProperties();
    withWet.push_back(flag("wet"));
    const std::uint32_t db = addFixture(b, withWet);
    std::size_t same = 0;
    for (StateId s : allStates(b, db))
        if (b.value(s, "wet").empty()) same += base.at(key(b, s)) == b.hash(s);
    CHECK(same == 48);

    StateTable c;  // + a mandatory property: no state keeps its identity
    std::vector<PropertyDecl> withSize = fixtureProperties();
    withSize.push_back(mandatoryEnum("size", {"small", "large"}, 0));
    const std::uint32_t dc = addFixture(c, withSize);
    std::set<StateHash> old;
    for (const auto& [k, h] : base) old.insert(h);
    std::size_t kept = 0;
    for (StateId s : allStates(c, dc)) kept += old.contains(c.hash(s));
    CHECK(kept == 0);

    StateTable d;  // domains reordered, and another initial colour
    const std::uint32_t dd = addFixture(d, {flag("lit"), optionalEnum("stage", {"c", "a", "b"}), mandatoryEnum("color", {"blue", "red"}, 1)});
    std::size_t reordered = 0;
    for (StateId s : allStates(d, dd)) reordered += base.at(key(d, s)) == d.hash(s);
    CHECK(reordered == 48);
    CHECK(d.value(d.defaultState(dd), "color") == "red" && a.value(a.defaultState(da), "color") == "blue");
}

// Orientation's canonical value names its coset without a rotation index:
// for every symmetry, two rotations share a text exactly when they share a
// canonical rotation, the identity's coset writes nothing, and every text
// parses back to its state.
void testOrientationTexts() {
    for (int sym = 0; sym < kSymmetryCount; ++sym) {
        const auto symmetry = static_cast<Symmetry>(sym);
        bool consistent = true;
        for (Rot g = 0; g < kRotCount; ++g)
            for (Rot h = 0; h < kRotCount; ++h) {
                const bool sameCoset = canonicalize(symmetry, g) == canonicalize(symmetry, h);
                consistent = consistent && sameCoset == (orientationText(symmetry, canonicalize(symmetry, g)) ==
                                                        orientationText(symmetry, canonicalize(symmetry, h)));
            }
        CHECK(consistent);
        CHECK(orientationText(symmetry, kIdentity).empty());
        StateTable t;
        const std::uint32_t def = t.addDefinition(kFixtureUlid, "o", 0, nullptr, symmetry, {}, true);
        std::set<std::string> texts;
        bool parses = true, wellFormed = true;
        for (StateId s : allStates(t, def)) {
            const std::string text = t.text(s);
            texts.insert(text);
            parses = parses && t.parse(text) == s;
            if (t.orientation(s) != kIdentity) {
                const std::string v = text.substr(26);
                wellFormed = wellFormed && v.size() == 18 && v.rfind("[orientation=", 0) == 0 && v.back() == ']';  // [orientation=±a±b]
            }
        }
        CHECK(texts.size() == t.definitionAt(def).stateCount && parses && wellFormed);
    }
}

// Every state of every shipped definition and of the fixture round-trips:
// through its definition ULID and named values, its state hash, its text, and
// its definition's dense index.
void testRoundTrips() {
    StateTable fixture;
    addFixture(fixture, fixtureProperties());
    for (StateTable* t : {&registry().states(), &fixture}) {
        bool ok = true;
        std::set<StateHash> hashes;
        for (std::uint32_t i = 0; i < t->size(); ++i) {
            const StateId s{i};
            const StateTable::Definition& d = t->definition(s);
            const auto def = t->definitionOf(d.ulid);
            ok = ok && def && t->intern(*def, t->orientation(s), t->values(s)) == s;
            ok = ok && t->find(t->hash(s)) == s && t->parse(t->text(s)) == s;
            ok = ok && std::find(d.dense.begin(), d.dense.end(), s) != d.dense.end();
            // Named values: rebuild the state from the names alone.
            StateId named = t->withOrientation(t->defaultState(*def), t->orientation(s));
            for (const PropertyDecl& p : d.properties) {
                const std::string_view v = t->value(s, p.name);
                named = v.empty() ? t->without(named, p.name) : t->with(named, p.name, v);
            }
            ok = ok && named == s;
            hashes.insert(t->hash(s));
        }
        CHECK(ok);
        CHECK(hashes.size() == t->size());  // no two states share a hash
    }
    CHECK(registry().states().size() == validStateCount() || registry().states().size() > validStateCount());
}

void testParseRejects() {
    StateTable t;
    const std::uint32_t def = addFixture(t, fixtureProperties());
    const std::string u = kFixtureUlid.text();
    std::string lower = u;
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(t.parse(u + "[color=red,lit=true]").has_value());
    for (const std::string& bad : {u, lower + "[color=red]", std::string("7ZZZZZZZZZZZZZZZZZZZZZZZZZ[color=red]"), u + "[lit=true,color=red]",
                                   u + "[color=red,color=red]", u + "[color=red,wet=true]", u + "[color=green]", u + "[color=red,lit=false]",
                                   u + "[]", u + "[color=red,]", u + "[color=red", u + "[color]", u + "[orientation=,color=red]"})
        CHECK(!t.parse(bad).has_value());
    (void)def;
}

void testLazyInterning() {
    StateTable t;
    const std::uint32_t def = addFixture(t, fixtureProperties(), /*internAll=*/false);
    CHECK(t.size() == 1);
    const StateId lit = fixtureState(t, def, kIdentity, 0, 1, 0);
    CHECK(t.size() == 2 && fixtureState(t, def, kIdentity, 0, 1, 0) == lit && t.size() == 2);
    const StateId start = t.defaultState(def);  // blue, nothing else
    CHECK(t.size() == 3 && t.with(start, "color", "red") == t.without(lit, "lit") && t.size() == 4);
    const StateId turned = t.withOrientation(lit, rotationAbout({0, 1, 0}, 90));
    CHECK(t.parse(t.text(turned)) == turned && t.orientation(turned) != kIdentity);
    CHECK(t.value(start, "stage").empty() && t.value(t.with(start, "stage", "b"), "stage") == "b");
    CHECK(t.interned() == t.size());
}

void testFatalChecks() {
    const FatalHandler saved = fatalHandler();
    fatalHandler() = throwingFatalHandler;
    auto fires = [](auto&& f) {
        try {
            f();
        } catch (const FatalError&) {
            return true;
        }
        return false;
    };
    StateTable t;
    const std::uint32_t def = addFixture(t, fixtureProperties());
    CHECK(fires([&] { addFixture(t, {}); }));  // the same ULID twice
    std::vector<std::string> many;
    for (int i = 0; i < 42; ++i) many.push_back("v" + std::to_string(i));
    CHECK(fires([&] { t.addDefinition(Uid::fromParts(1, 2), "big", 0, nullptr, Symmetry::Oriented, {optionalEnum("v", many)}, false); }));  // 24 × 43 > 2¹⁰
    const std::uint32_t outOfDomain[3] = {2, 0, 0};
    CHECK(fires([&] { t.intern(def, kIdentity, outOfDomain); }));
    CHECK(fires([&] { t.without(t.defaultState(def), "color"); }));  // mandatory
    fatalHandler() = saved;
}

// Digests are functions of content alone now: the same in every process and
// run, so they are pinned.
void testPinnedDigests() {
    std::string err;
    const auto scene = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
    CHECK(scene.has_value());
    if (!scene) return;
    ChunkWorld w(registry());
    std::vector<std::string> warnings;
    w.applyBulk(sceneEdits(*scene, registry(), &warnings));
    Chunk stone(testStates());
    fillChunk(stone, Cells(kBrickCells, solidState()), true);
    CHECK(ChunkWorldTestAccess::chunk(w).digest() == 1243621874014904966ull);
    CHECK(stone.digest() == 1879917277027827065ull);
    CHECK(stone.digest() == mulMod(testStates().digestTerm(solidState()), digestK()));
}

// Nothing per cell, per face or per frame interns: after load, applying the
// v0.1 scene, meshing it every way, raycasting and planning every tool leave
// the count of interned states where it was.
void testNoInterningAfterLoad() {
    StateTable& t = testStates();
    const std::uint64_t before = t.interned();
    std::string err;
    const auto scene = loadScenario(SANDBOX_SCENARIO_DIR "/v0.1-scene.json", err);
    ChunkWorld w(registry());
    std::vector<std::string> warnings;
    if (scene) w.applyBulk(sceneEdits(*scene, registry(), &warnings));
    for (Mesher m : {Mesher::Greedy, Mesher::Unmerged, Mesher::PerCell}) (void)meshWith(w, m);
    (void)w.emitters();
    const auto target = aim(w, {10.0, 10.0, 19.0}, {16.0, 16.0, 15.0});
    CHECK(target.has_value());
    ToolSettings settings;
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    for (Item item : {Item::pick(), Item::wrench(), Item::shovel(), Item::container(), Item{Item::Kind::Block, typeOf("furnace")}})
        for (bool secondary : {false, true}) (void)planEdit(&item, secondary, target, ctx, w);
    CHECK(t.interned() == before);
}
}  // namespace

void runStateTests() {
    testAir();
    testCanonicalText();
    testEvolution();
    testOrientationTexts();
    testRoundTrips();
    testParseRejects();
    testLazyInterning();
    testFatalChecks();
    testPinnedDigests();
    testNoInterningAfterLoad();
}
