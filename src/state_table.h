#pragma once
// The state table (world/brick, "Palette entries: block states"): one row per
// interned block state, indexed by a 32-bit StateId. A state's identity is its
// definition's ULID and its named property values; its state hash is BLAKE3
// (derive_key, deps/hashing) of the canonical text
//     <definition ULID>[name=value,...]
// with names sorted, properties at their default left out, and the brackets
// left out when nothing is written. Orientation is a property whose value
// names its coset without a rotation index (world/block-orientation).
//
// Interning hashes; everything else reads by id. Code resolves the ids it
// needs ahead of time (world/brick), and moving between states of one
// definition is a lookup in that definition's dense index, not a hash.
//
// Single-threaded here: rows are appended, never moved or changed, so the
// concurrent version changes the locking, not the layout.

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "appearance.h"
#include "id128.h"
#include "orientation.h"

struct BlockDef;
using BlockTypeId = std::uint16_t;
inline constexpr BlockTypeId kAirType = 0xFFFF;  // air's block type: no registry entry

// A block state's number in this process (world/brick): never persisted.
struct StateId {
    std::uint32_t value = 0;
    constexpr auto operator<=>(const StateId&) const = default;
};
inline constexpr StateId kAirState{0};  // air is always state 0

// The Brick's reverse lookup hashes this (in-process only, world/brick).
constexpr std::uint64_t lookupKey(StateId s) { return s.value; }

// A declared variant property (orientation is not one: it comes from the
// definition's symmetry). No value is a designated default (world/brick):
// an optional property is absent or one of its values (a flag is optional
// with the one value "true"), and a mandatory one always has a value. In
// the table a value is an index: for an optional property 0 is absent and
// i is values[i − 1]; for a mandatory one i is values[i]. `initial` is a
// mandatory property's starting value (behaviour, never identity).
struct PropertyDecl {
    std::string name;
    std::vector<std::string> values;
    bool mandatory = false;
    std::uint32_t initial = 0;  // index into values (mandatory only)
    bool operator==(const PropertyDecl&) const = default;

    std::uint32_t domainSize() const { return static_cast<std::uint32_t>(values.size()) + (mandatory ? 0u : 1u); }
    // The value's name; "" for an absent optional property.
    std::string_view valueName(std::uint32_t index) const {
        return mandatory ? std::string_view(values[index]) : index == 0 ? std::string_view() : std::string_view(values[index - 1]);
    }
    // The index of a named value, or nothing.
    std::optional<std::uint32_t> indexOf(std::string_view value) const {
        for (std::size_t i = 0; i < values.size(); ++i)
            if (values[i] == value) return static_cast<std::uint32_t>(i + (mandatory ? 0 : 1));
        return std::nullopt;
    }
    // Where a new block starts: absent, or the initial value.
    std::uint32_t startIndex() const { return mandatory ? initial : 0u; }
};

// A cell's visibility class (world/chunk-meshing, layer 1).
enum class Visibility : std::uint8_t { Air, Opaque, Translucent };

class StateTable {
public:
    static constexpr std::string_view kHashContext = "lanterns-reach 2026-11-27 block state v1";
    static constexpr std::uint32_t kMaxStatesPerDefinition = 1024;  // 2¹⁰ (world/brick)
    static constexpr Ulid airUlid();

    // A definition as the table knows it. `def` is the registry's (textures,
    // material, translucency); air and synthetic test definitions have none.
    struct Definition {
        Ulid ulid;
        std::string name;
        BlockTypeId type = kAirType;
        const BlockDef* def = nullptr;
        orient::Symmetry symmetry = orient::Symmetry::Uniform;
        std::vector<PropertyDecl> properties;  // sorted by name
        std::vector<orient::Rot> orientations;  // canonical rotations, ascending; [0] is the identity
        std::vector<std::string> orientationText;  // per orientation: its canonical value ("" for the default)
        std::uint32_t stateCount = 1;  // orientations × every property's domain (absence counted)
        std::vector<StateId> dense;    // by dense index; kNoState where not yet interned
    };
    static constexpr StateId kNoState{0xFFFFFFFFu};

    // Air only. Definitions are added with addDefinition.
    StateTable();

    // Adds a definition and returns its index. Fatal if its ULID is already
    // a definition's, or its states exceed kMaxStatesPerDefinition (the
    // loader checks both first, to report them). With internAll, every state
    // is interned now, in dense order.
    std::uint32_t addDefinition(Ulid ulid, std::string name, BlockTypeId type, const BlockDef* def,
                                orient::Symmetry symmetry, std::vector<PropertyDecl> properties, bool internAll);

    // The state with these values (orientation canonicalised under the
    // definition's symmetry; one value index per property, in name order),
    // interned if it wasn't. Fatal past 2³² states, or if the hash is already
    // another state's (invariant/brick/unique-state-hash).
    StateId intern(std::uint32_t definition, orient::Rot orientation, std::span<const std::uint32_t> values);
    std::optional<StateId> find(const StateHash& hash) const;
    // The state a canonical text names (interned if need be), or nothing if it
    // isn't canonical text for a known definition.
    std::optional<StateId> parse(std::string_view text);
    std::string text(StateId s) const;

    // Moving between states of one definition: dense lookups, interning only
    // a state never seen. A definition's starting state is unrotated, with its
    // optional properties absent and its mandatory ones at their initial value.
    StateId defaultState(std::uint32_t definition);
    StateId withOrientation(StateId s, orient::Rot placed);
    StateId with(StateId s, std::string_view property, std::string_view value);
    StateId without(StateId s, std::string_view property);  // an optional property absent

    // Reads by id.
    const StateHash& hash(StateId s) const { return rows_[s.value].hash; }
    std::uint32_t definitionIndex(StateId s) const { return rows_[s.value].definition; }
    const Definition& definition(StateId s) const { return definitions_[rows_[s.value].definition]; }
    const Definition& definitionAt(std::uint32_t index) const { return definitions_[index]; }
    BlockTypeId type(StateId s) const { return definition(s).type; }
    orient::Rot orientation(StateId s) const { return rows_[s.value].orientation; }
    bool isAir(StateId s) const { return s == kAirState; }
    Visibility visibility(StateId s) const { return rows_[s.value].visibility; }
    bool translucent(StateId s) const { return visibility(s) == Visibility::Translucent; }
    // The property's value name ("" if it is optional and absent).
    std::string_view value(StateId s, std::string_view property) const;
    std::vector<std::uint32_t> values(StateId s) const;  // one index per property, in name order
    // The face's appearance id (render/block-textures) and what it shows.
    std::uint32_t appearance(StateId s, orient::Face face) const { return rows_[s.value].appearance[face]; }
    const FacePayload& payload(std::uint32_t appearanceId) const { return payloads_[appearanceId]; }
    // The content digest's per-state term H(s) (data/content-digest): the
    // state hash's first 8 bytes, little-endian, reduced mod 2⁶¹ − 1.
    std::uint64_t digestTerm(StateId s) const { return rows_[s.value].digestTerm; }

    std::size_t size() const { return rows_.size(); }
    std::size_t definitionCount() const { return definitions_.size(); }
    std::optional<std::uint32_t> definitionOf(const Ulid& ulid) const;
    std::uint64_t interned() const { return interned_; }  // states interned so far (a measurement)
    std::size_t bytes() const;  // the table's memory, roughly

private:
    struct Row {
        StateHash hash;
        std::uint64_t digestTerm = 0;
        std::uint32_t definition = 0;
        std::uint32_t dense = 0;
        orient::Rot orientation = orient::kIdentity;
        Visibility visibility = Visibility::Air;
        std::array<std::uint32_t, orient::kFaceCount> appearance{};
    };

    std::uint32_t denseIndex(const Definition& d, std::size_t orientationSlot, std::span<const std::uint32_t> values) const;
    std::string canonicalText(const Definition& d, std::size_t orientationSlot, std::span<const std::uint32_t> values) const;
    std::uint32_t internPayload(const FacePayload& p);
    StateId internDense(std::uint32_t definition, std::uint32_t dense);

    std::vector<Definition> definitions_;
    std::vector<Row> rows_;
    std::unordered_map<StateHash, StateId, Id128Hash> byHash_;
    std::unordered_map<Ulid, std::uint32_t, Id128Hash> byUlid_;
    std::vector<FacePayload> payloads_;
    std::map<FacePayload, std::uint32_t> payloadIds_;
    std::uint64_t interned_ = 0;
};

// Air's built-in definition ULID (minted once with tools/mint_ulid.py).
constexpr Ulid StateTable::airUlid() {
    // 01M8F3N35FMTBBXQ1NFX22HGFS
    return Ulid{{0x01, 0xA2, 0x1E, 0x3A, 0x8C, 0xAF, 0xA6, 0x96, 0xBE, 0xDC, 0x35, 0x7F, 0x44, 0x28, 0xC1, 0xF9}};
}

// The canonical name of orientation `canonical` under `symmetry`: the images
// of +x and +z of the coset member that sorts first by those images (axes
// ordered +x, −x, +y, −y, +z, −z), e.g. "+y+z"; "" for the identity's coset
// (world/block-orientation). And its inverse over the definition's cosets.
std::string orientationText(orient::Symmetry symmetry, orient::Rot canonical);
