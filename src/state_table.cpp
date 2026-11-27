#include "state_table.h"

#include <algorithm>
#include <utility>

#include "block_mesh.h"
#include "blocks.h"
#include "digest.h"
#include "fatal.h"
#include "packed_quad.h"

std::string orientationText(orient::Symmetry symmetry, orient::Rot canonical) {
    if (canonical == orient::kIdentity) return "";
    const orient::Group& g = orient::group();
    const orient::SymmetryInfo& info = orient::symmetry(symmetry);
    std::pair<int, int> best{99, 99};
    for (orient::Rot h : info.subgroup) {
        const orient::Rot m = g.compose[canonical][h];
        best = std::min(best, std::pair<int, int>{g.faceMap[m][orient::PosX], g.faceMap[m][orient::PosZ]});
    }
    return std::string(orient::kFaceNames[static_cast<std::size_t>(best.first)]) +
           orient::kFaceNames[static_cast<std::size_t>(best.second)];
}

StateTable::StateTable() {
    addDefinition(airUlid(), "air", kAirType, nullptr, orient::Symmetry::Uniform, {}, true);
}

std::uint32_t StateTable::addDefinition(Ulid ulid, std::string name, BlockTypeId type, const BlockDef* def,
                                        orient::Symmetry symmetry, std::vector<PropertyDecl> properties, bool internAll) {
    FATAL_UNLESS(!byUlid_.contains(ulid), "two block definitions share a ULID");
    Definition d;
    d.ulid = ulid;
    d.name = std::move(name);
    d.type = type;
    d.def = def;
    d.symmetry = symmetry;
    std::sort(properties.begin(), properties.end(), [](const PropertyDecl& a, const PropertyDecl& b) { return a.name < b.name; });
    d.properties = std::move(properties);
    const orient::SymmetryInfo& info = orient::symmetry(symmetry);
    for (orient::Rot r = 0; r < orient::kRotCount; ++r)
        if (info.canonical[r] == r) {
            d.orientations.push_back(r);
            d.orientationText.push_back(orientationText(symmetry, r));
        }
    std::uint64_t count = d.orientations.size();
    for (const PropertyDecl& p : d.properties) {
        FATAL_UNLESS(!p.values.empty() && (!p.mandatory || p.values.size() >= 2), "a property with too few values");
        FATAL_UNLESS(!p.mandatory || p.initial < p.values.size(), "a mandatory property's initial value is outside its values");
        count *= p.domainSize();
    }
    FATAL_UNLESS(count <= kMaxStatesPerDefinition, "a block definition with more than 2^10 states");
    d.stateCount = static_cast<std::uint32_t>(count);
    d.dense.assign(d.stateCount, kNoState);
    const auto index = static_cast<std::uint32_t>(definitions_.size());
    byUlid_.emplace(ulid, index);
    definitions_.push_back(std::move(d));
    if (internAll)
        for (std::uint32_t i = 0; i < definitions_[index].stateCount; ++i) internDense(index, i);
    return index;
}

std::uint32_t StateTable::denseIndex(const Definition& d, std::size_t orientationSlot, std::span<const std::uint32_t> values) const {
    std::uint64_t index = orientationSlot;
    for (std::size_t i = 0; i < d.properties.size(); ++i) index = index * d.properties[i].domainSize() + values[i];
    return static_cast<std::uint32_t>(index);
}

std::vector<std::uint32_t> StateTable::values(StateId s) const {
    const Definition& d = definition(s);
    std::uint32_t dense = rows_[s.value].dense;
    std::vector<std::uint32_t> out(d.properties.size());
    for (std::size_t i = d.properties.size(); i-- > 0;) {
        const std::uint32_t n = d.properties[i].domainSize();
        out[i] = dense % n;
        dense /= n;
    }
    return out;
}

std::string StateTable::canonicalText(const Definition& d, std::size_t orientationSlot, std::span<const std::uint32_t> values) const {
    std::vector<std::pair<std::string_view, std::string_view>> written;
    if (!d.orientationText[orientationSlot].empty()) written.emplace_back("orientation", d.orientationText[orientationSlot]);
    for (std::size_t i = 0; i < d.properties.size(); ++i)
        if (d.properties[i].mandatory || values[i] != 0) written.emplace_back(d.properties[i].name, d.properties[i].valueName(values[i]));
    std::sort(written.begin(), written.end());
    std::string text = d.ulid.text();
    if (written.empty()) return text;
    text += '[';
    for (std::size_t i = 0; i < written.size(); ++i) {
        if (i) text += ',';
        text += written[i].first;
        text += '=';
        text += written[i].second;
    }
    text += ']';
    return text;
}

std::uint32_t StateTable::internPayload(const FacePayload& p) {
    // A packed quad carries 23 bits of appearance id (packed_quad.h).
    FATAL_UNLESS(payloads_.size() <= quad::kMaxAppearance || payloadIds_.contains(p), "appearance ids exhausted (2^23)");
    const auto [it, fresh] = payloadIds_.emplace(p, static_cast<std::uint32_t>(payloads_.size()));
    if (fresh) payloads_.push_back(p);
    return it->second;
}

StateId StateTable::internDense(std::uint32_t definition, std::uint32_t dense) {
    Definition& d = definitions_[definition];
    FATAL_UNLESS(dense < d.stateCount, "a state outside its definition's domains");
    if (d.dense[dense] != kNoState) return d.dense[dense];
    FATAL_UNLESS(rows_.size() < 0xFFFFFFFFull, "more than 2^32 block states (invariant/brick/state-ids-never-overflow)");
    // Decode the dense index into the orientation slot and property values.
    std::vector<std::uint32_t> vals(d.properties.size());
    std::uint32_t rest = dense;
    for (std::size_t i = d.properties.size(); i-- > 0;) {
        const std::uint32_t n = d.properties[i].domainSize();
        vals[i] = rest % n;
        rest /= n;
    }
    const std::size_t slot = rest;
    Row row;
    row.hash = blake3DeriveKey128(kHashContext, canonicalText(d, slot, vals));
    std::uint64_t first8 = 0;  // little-endian
    for (int i = 7; i >= 0; --i) first8 = (first8 << 8) | row.hash.bytes[static_cast<std::size_t>(i)];
    row.digestTerm = reduceMod(first8);
    row.definition = definition;
    row.dense = dense;
    row.orientation = d.orientations[slot];
    row.visibility = d.type == kAirType ? Visibility::Air
                     : d.def && d.def->translucent() ? Visibility::Translucent
                                                     : Visibility::Opaque;
    if (d.def)
        for (int f = 0; f < orient::kFaceCount; ++f) {
            const FaceTexture tex = faceTexture(*d.def, row.orientation, static_cast<orient::Face>(f));
            row.appearance[static_cast<std::size_t>(f)] =
                internPayload({tex.layer, BlockRegistry::materialId(d.type), packTexInfo(tex)});
        }
    const StateId id{static_cast<std::uint32_t>(rows_.size())};
    FATAL_UNLESS(byHash_.emplace(row.hash, id).second, "two block states share a state hash (invariant/brick/unique-state-hash)");
    rows_.push_back(row);
    d.dense[dense] = id;
    ++interned_;
    return id;
}

StateId StateTable::intern(std::uint32_t definition, orient::Rot orientation, std::span<const std::uint32_t> values) {
    FATAL_UNLESS(definition < definitions_.size(), "intern: no such definition");
    const Definition& d = definitions_[definition];
    FATAL_UNLESS(values.size() == d.properties.size(), "intern: one value per property");
    for (std::size_t i = 0; i < values.size(); ++i) FATAL_UNLESS(values[i] < d.properties[i].domainSize(), "intern: a value outside its domain");
    const orient::Rot canonical = orient::canonicalize(d.symmetry, orientation);
    const auto slot = static_cast<std::size_t>(std::find(d.orientations.begin(), d.orientations.end(), canonical) - d.orientations.begin());
    return internDense(definition, denseIndex(d, slot, values));
}

std::optional<StateId> StateTable::find(const StateHash& hash) const {
    const auto it = byHash_.find(hash);
    if (it == byHash_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::uint32_t> StateTable::definitionOf(const Ulid& ulid) const {
    const auto it = byUlid_.find(ulid);
    if (it == byUlid_.end()) return std::nullopt;
    return it->second;
}

std::optional<StateId> StateTable::parse(std::string_view text) {
    if (text.size() < 26) return std::nullopt;
    const auto ulid = Ulid::parse(text.substr(0, 26));
    if (!ulid || ulid->text() != text.substr(0, 26)) return std::nullopt;  // canonical: upper case
    const auto def = definitionOf(*ulid);
    if (!def) return std::nullopt;
    const Definition& d = definitions_[*def];
    std::size_t slot = 0;
    std::vector<std::uint32_t> vals(d.properties.size(), 0);
    std::vector<bool> seen(d.properties.size(), false);
    std::string_view rest = text.substr(26);
    if (!rest.empty()) {
        if (rest.size() < 3 || rest.front() != '[' || rest.back() != ']') return std::nullopt;
        rest = rest.substr(1, rest.size() - 2);
        std::string_view previous;
        while (true) {
            const std::size_t comma = rest.find(',');
            const std::string_view entry = rest.substr(0, comma);
            const std::size_t eq = entry.find('=');
            if (eq == std::string_view::npos || eq == 0 || eq + 1 == entry.size()) return std::nullopt;
            const std::string_view name = entry.substr(0, eq), value = entry.substr(eq + 1);
            if (!previous.empty() && name <= previous) return std::nullopt;  // sorted, each name once
            previous = name;
            if (name == "orientation") {
                const auto it = std::find(d.orientationText.begin(), d.orientationText.end(), value);
                if (it == d.orientationText.end() || it == d.orientationText.begin()) return std::nullopt;  // the default isn't written
                slot = static_cast<std::size_t>(it - d.orientationText.begin());
            } else {
                const auto p = std::find_if(d.properties.begin(), d.properties.end(), [&](const PropertyDecl& q) { return q.name == name; });
                if (p == d.properties.end()) return std::nullopt;
                const auto v = p->indexOf(value);
                if (!v) return std::nullopt;
                const auto i = static_cast<std::size_t>(p - d.properties.begin());
                vals[i] = *v;
                seen[i] = true;
            }
            if (comma == std::string_view::npos) break;
            rest = rest.substr(comma + 1);
        }
    }
    for (std::size_t i = 0; i < d.properties.size(); ++i)
        if (d.properties[i].mandatory && !seen[i]) return std::nullopt;  // a mandatory property is always written
    return internDense(*def, denseIndex(d, slot, vals));
}

std::string StateTable::text(StateId s) const {
    const Definition& d = definition(s);
    const std::vector<std::uint32_t> vals = values(s);
    const auto slot = static_cast<std::size_t>(std::find(d.orientations.begin(), d.orientations.end(), orientation(s)) - d.orientations.begin());
    return canonicalText(d, slot, vals);
}

StateId StateTable::defaultState(std::uint32_t definition) {
    FATAL_UNLESS(definition < definitions_.size(), "defaultState: no such definition");
    const Definition& d = definitions_[definition];
    std::vector<std::uint32_t> vals;
    for (const PropertyDecl& p : d.properties) vals.push_back(p.startIndex());
    return internDense(definition, denseIndex(d, 0, vals));
}

StateId StateTable::withOrientation(StateId s, orient::Rot placed) {
    return intern(definitionIndex(s), placed, values(s));
}

StateId StateTable::with(StateId s, std::string_view property, std::string_view value) {
    const Definition& d = definition(s);
    std::vector<std::uint32_t> vals = values(s);
    const auto p = std::find_if(d.properties.begin(), d.properties.end(), [&](const PropertyDecl& q) { return q.name == property; });
    FATAL_UNLESS(p != d.properties.end(), "with: no such property");
    const auto v = p->indexOf(value);
    FATAL_UNLESS(v.has_value(), "with: no such value");
    vals[static_cast<std::size_t>(p - d.properties.begin())] = *v;
    return intern(definitionIndex(s), orientation(s), vals);
}

StateId StateTable::without(StateId s, std::string_view property) {
    const Definition& d = definition(s);
    std::vector<std::uint32_t> vals = values(s);
    const auto p = std::find_if(d.properties.begin(), d.properties.end(), [&](const PropertyDecl& q) { return q.name == property; });
    FATAL_UNLESS(p != d.properties.end() && !p->mandatory, "without: no such optional property");
    vals[static_cast<std::size_t>(p - d.properties.begin())] = 0;
    return intern(definitionIndex(s), orientation(s), vals);
}

std::string_view StateTable::value(StateId s, std::string_view property) const {
    const Definition& d = definition(s);
    const std::vector<std::uint32_t> vals = values(s);
    for (std::size_t i = 0; i < d.properties.size(); ++i)
        if (d.properties[i].name == property) return d.properties[i].valueName(vals[i]);
    return {};
}

std::size_t StateTable::bytes() const {
    std::size_t b = rows_.capacity() * sizeof(Row) + byHash_.size() * (sizeof(StateHash) + sizeof(StateId) + 2 * sizeof(void*));
    for (const Definition& d : definitions_) b += d.dense.capacity() * sizeof(StateId);
    return b + payloads_.capacity() * sizeof(FacePayload);
}
