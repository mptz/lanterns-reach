#include "blocks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>
#include <stb_image.h>

namespace {

namespace fs = std::filesystem;
using orient::Face;

std::string faceList(const std::vector<Face>& faces) {
    std::string s;
    for (Face f : faces) s += (s.empty() ? "" : ", ") + std::string(orient::kFaceNames[f]);
    return "{" + s + "}";
}

}  // namespace

Image rotateCcw(const Image& img, int quarterTurns) {
    Image out = img;
    const std::uint32_t n = img.width;
    for (int t = 0; t < ((quarterTurns % 4) + 4) % 4; ++t) {
        const Image src = out;
        // Turning CCW carries the top-left corner to the bottom-left:
        // new(x, y) = old(n−1−y, x).
        for (std::uint32_t y = 0; y < n; ++y)
            for (std::uint32_t x = 0; x < n; ++x)
                for (int c = 0; c < 4; ++c)
                    out.rgba[(y * n + x) * 4 + static_cast<std::uint32_t>(c)] =
                        src.rgba[(x * n + (n - 1 - y)) * 4 + static_cast<std::uint32_t>(c)];
    }
    return out;
}

std::optional<Image> loadPng(const std::string& path, std::string& error) {
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        error = path + ": " + stbi_failure_reason();
        return std::nullopt;
    }
    Image img{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), {}};
    img.rgba.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(data);
    return img;
}

std::vector<Image> buildMipChain(const Image& level0) {
    auto toLinear = [](std::uint8_t v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto toSrgb = [](double l) {
        const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
        return static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0), 0L, 255L));
    };
    std::vector<Image> chain{level0};
    while (chain.back().width > 1) {
        const Image& src = chain.back();
        const std::uint32_t n = src.width / 2;
        Image dst{n, n, std::vector<std::uint8_t>(static_cast<std::size_t>(n) * n * 4)};
        for (std::uint32_t y = 0; y < n; ++y)
            for (std::uint32_t x = 0; x < n; ++x)
                for (std::uint32_t c = 0; c < 4; ++c) {
                    double sum = 0.0;
                    for (std::uint32_t dy = 0; dy < 2; ++dy)
                        for (std::uint32_t dx = 0; dx < 2; ++dx) {
                            const std::uint8_t v = src.rgba[((2 * y + dy) * src.width + 2 * x + dx) * 4 + c];
                            sum += c == 3 ? v / 255.0 : toLinear(v);
                        }
                    dst.rgba[(y * n + x) * 4 + c] =
                        c == 3 ? static_cast<std::uint8_t>(std::lround(sum / 4.0 * 255.0)) : toSrgb(sum / 4.0);
                }
        chain.push_back(std::move(dst));
    }
    return chain;
}

std::uint32_t setTileCount(VariantSet set, std::uint32_t randomTiles) {
    switch (set) {
        case VariantSet::None: return 1;
        case VariantSet::Random: return randomTiles;
        case VariantSet::MatchedNS: return 4;
        case VariantSet::MatchedNSEW:
        case VariantSet::Connected: return 16;
    }
    return 1;
}

std::vector<Image> splitSet(const Image& atlas, std::uint32_t count) {
    std::vector<Image> tiles;
    const std::uint32_t t = kTextureSize;
    for (std::uint32_t i = 0; i < count; ++i) {
        Image tile{t, t, std::vector<std::uint8_t>(static_cast<std::size_t>(t) * t * 4)};
        const std::uint32_t ox = (i % kSetColumns) * t, oy = (i / kSetColumns) * t;
        for (std::uint32_t y = 0; y < t; ++y)
            std::copy_n(&atlas.rgba[((oy + y) * atlas.width + ox) * 4], t * 4, &tile.rgba[y * t * 4]);
        tiles.push_back(std::move(tile));
    }
    return tiles;
}

std::optional<ParsedDefinition> parseDefinition(std::string_view text, std::string& error) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        error = "not a JSON object";
        return std::nullopt;
    }
    for (const auto& [key, value] : j.items())
        if (key != "ulid" && key != "name" && key != "symmetry" && key != "properties" && key != "initial" && key != "textures" &&
            key != "roughness" &&
            key != "metallic" && key != "reflectance" && key != "emissive" && key != "translucency" &&
            key != "cutout") {
            error = "unknown field \"" + key + "\"";
            return std::nullopt;
        }
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        error = "missing \"name\"";
        return std::nullopt;
    }
    ParsedDefinition def;
    def.name = j["name"].get<std::string>();
    // The definition's identity (data/stable-identity): a ULID, minted once
    // (tools/mint_ulid.py) and kept for good.
    if (!j.contains("ulid") || !j["ulid"].is_string()) {
        error = "missing \"ulid\" (mint one with tools/mint_ulid.py --add)";
        return std::nullopt;
    }
    {
        const std::string ulidText = j["ulid"].get<std::string>();
        const auto ulid = Ulid::parse(ulidText);
        if (!ulid) {
            error = "\"ulid\": \"" + ulidText + "\" is not 26 characters of Crockford base32";
            return std::nullopt;
        }
        def.ulid = *ulid;
    }
    if (!j.contains("symmetry") || !j["symmetry"].is_string()) {
        error = "missing \"symmetry\"";
        return std::nullopt;
    }
    const auto sym = orient::symmetryFromKeyword(j["symmetry"].get<std::string>());
    if (!sym) {
        error = "unknown symmetry \"" + j["symmetry"].get<std::string>() + "\"";
        return std::nullopt;
    }
    def.symmetry = *sym;
    // Variant properties (world/block-states); no value is a designated default:
    //   "flag"                                 optional, the one value "true"
    //   ["a", "b", ...]                        optional: absent, or one of its values
    //   {"values": [...], "mandatory": true}   always one of its values
    // and "initial" names each mandatory property's starting value. Names and
    // values are [a-z0-9_]+; orientation is reserved (it comes from the symmetry).
    auto plainName = [](const std::string& n) {
        return !n.empty() && std::all_of(n.begin(), n.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
    };
    if (j.contains("properties")) {
        if (!j["properties"].is_object()) {
            error = "\"properties\" must be an object of name: \"flag\", [values] or {\"values\": [...], \"mandatory\": true}";
            return std::nullopt;
        }
        for (const auto& [name, domain] : j["properties"].items()) {
            if (!plainName(name) || name == "orientation") {
                error = "property \"" + name + "\": names are [a-z0-9_]+, and \"orientation\" is reserved";
                return std::nullopt;
            }
            PropertyDecl p{name, {}, false, 0};
            const nlohmann::json* list = nullptr;
            if (domain.is_string() && domain.get<std::string>() == "flag") {
                p.values = {"true"};
            } else if (domain.is_array()) {
                list = &domain;
            } else if (domain.is_object() && domain.contains("values") &&
                       std::all_of(domain.items().begin(), domain.items().end(), [](const auto& kv) { return kv.key() == "values" || kv.key() == "mandatory"; })) {
                list = &domain["values"];
                if (domain.contains("mandatory")) {
                    if (!domain["mandatory"].is_boolean()) {
                        error = "property \"" + name + "\": \"mandatory\" is true or false";
                        return std::nullopt;
                    }
                    p.mandatory = domain["mandatory"].get<bool>();
                }
            } else {
                error = "property \"" + name + "\" must be \"flag\", a list of value names, or {\"values\": [...], \"mandatory\": true}";
                return std::nullopt;
            }
            if (list) {
                if (!list->is_array() || list->empty() || (p.mandatory && list->size() < 2)) {
                    error = "property \"" + name + "\": an optional property needs a value, a mandatory one at least two";
                    return std::nullopt;
                }
                for (const auto& v : *list) {
                    if (!v.is_string() || !plainName(v.get<std::string>()) ||
                        std::find(p.values.begin(), p.values.end(), v.get<std::string>()) != p.values.end()) {
                        error = "property \"" + name + "\": values are distinct [a-z0-9_]+ names";
                        return std::nullopt;
                    }
                    p.values.push_back(v.get<std::string>());
                }
            }
            def.properties.push_back(std::move(p));
        }
        std::sort(def.properties.begin(), def.properties.end(), [](const PropertyDecl& a, const PropertyDecl& b) { return a.name < b.name; });
    }
    if (j.contains("initial") && !j["initial"].is_object()) {
        error = "\"initial\" must be an object of mandatory property: value";
        return std::nullopt;
    }
    for (PropertyDecl& p : def.properties) {
        const bool given = j.contains("initial") && j["initial"].contains(p.name);
        if (p.mandatory != given) {
            error = "\"initial\" names each mandatory property's starting value, and only those (\"" + p.name + "\")";
            return std::nullopt;
        }
        if (!given) continue;
        const nlohmann::json& v = j["initial"][p.name];
        const auto it = v.is_string() ? std::find(p.values.begin(), p.values.end(), v.get<std::string>()) : p.values.end();
        if (it == p.values.end()) {
            error = "\"initial\": \"" + p.name + "\" must be one of its values";
            return std::nullopt;
        }
        p.initial = static_cast<std::uint32_t>(it - p.values.begin());
    }
    if (j.contains("initial"))
        for (const auto& [key, value] : j["initial"].items())
            if (std::none_of(def.properties.begin(), def.properties.end(), [&](const PropertyDecl& p) { return p.name == key; })) {
                error = "\"initial\": no property \"" + key + "\"";
                return std::nullopt;
            }
    // The per-definition cap (world/block-states): orientations times every domain.
    {
        std::uint64_t states = static_cast<std::uint64_t>(orient::kRotCount) / orient::symmetry(def.symmetry).subgroup.size();
        for (const PropertyDecl& p : def.properties) states *= p.domainSize();
        if (states > StateTable::kMaxStatesPerDefinition) {
            error = "has " + std::to_string(states) + " states (orientations times property values); at most " +
                    std::to_string(StateTable::kMaxStatesPerDefinition);
            return std::nullopt;
        }
    }
    if (!j.contains("textures") || !j["textures"].is_object()) {
        error = "missing \"textures\" object";
        return std::nullopt;
    }

    const orient::SymmetryInfo& info = orient::symmetry(def.symmetry);
    std::vector<Face> reps, keys;
    for (const orient::Orbit& o : info.orbits) reps.push_back(o.rep);
    const nlohmann::json& tex = j["textures"];
    std::map<Face, TextureRef> refs;
    for (const auto& [key, value] : tex.items()) {
        const auto face = orient::faceFromName(key);
        TextureRef ref{face.value_or(Face{}), "", VariantSet::None, 1};
        const char* bad = nullptr;
        if (!face) {
            bad = "keys are faces +x -x +y -y +z -z";
        } else if (value.is_string()) {
            ref.file = value.get<std::string>();
        } else if (!value.is_object()) {
            bad = "a value is a file name or a variant set object";
        } else if (value.contains("random")) {
            ref.set = VariantSet::Random;
            if (value.size() != 2 || !value["random"].is_string() || !value.contains("tiles") ||
                !value["tiles"].is_number_unsigned() || value["tiles"].get<std::uint32_t>() < 1 ||
                value["tiles"].get<std::uint32_t>() > kMaxRandomTiles)
                bad = "{\"random\": file, \"tiles\": n} with 1 ≤ n ≤ 32";
            else {
                ref.file = value["random"].get<std::string>();
                ref.tiles = value["tiles"].get<std::uint32_t>();
            }
        } else if (value.contains("matched")) {
            const bool ok = value.size() == 2 && value["matched"].is_string() && value.contains("edges") &&
                            value["edges"].is_string() &&
                            (value["edges"] == "ns" || value["edges"] == "nsew");
            if (!ok) bad = "{\"matched\": file, \"edges\": \"ns\" | \"nsew\"}";
            else {
                ref.set = value["edges"] == "ns" ? VariantSet::MatchedNS : VariantSet::MatchedNSEW;
                ref.file = value["matched"].get<std::string>();
            }
        } else if (value.contains("connected")) {
            if (value.size() != 1 || !value["connected"].is_string()) bad = "{\"connected\": file}";
            else {
                ref.set = VariantSet::Connected;
                ref.file = value["connected"].get<std::string>();
            }
        } else {
            bad = "a variant set is {\"random\": …}, {\"matched\": …} or {\"connected\": …}";
        }
        if (bad) {
            error = "bad texture entry \"" + key + "\" (" + bad + ")";
            return std::nullopt;
        }
        if (ref.set != VariantSet::Random) ref.tiles = setTileCount(ref.set, 1);
        keys.push_back(*face);
        refs[*face] = ref;
    }
    std::vector<Face> sortedReps = reps, sortedKeys = keys;
    std::sort(sortedReps.begin(), sortedReps.end());
    std::sort(sortedKeys.begin(), sortedKeys.end());
    if (sortedKeys != sortedReps) {
        std::string orbits;
        for (const orient::Orbit& o : info.orbits)
            orbits += std::string(" ") + orient::kFaceNames[o.rep] + "=" + faceList(o.faces);
        error = "texture keys " + faceList(keys) + " do not match the orbit representatives " + faceList(reps) +
                " of symmetry \"" + info.keyword + "\" (orbits:" + orbits + ")";
        return std::nullopt;
    }
    for (Face rep : reps) def.textures.push_back(refs.at(rep));

    auto unit = [&](const char* field, float& out) {
        if (!j.contains(field)) return true;
        if (!j[field].is_number() || j[field].get<double>() < 0.0 || j[field].get<double>() > 1.0) {
            error = std::string("\"") + field + "\" must be a number in [0, 1]";
            return false;
        }
        out = j[field].get<float>();
        return true;
    };
    if (!unit("roughness", def.material.roughness) || !unit("metallic", def.material.metallic) ||
        !unit("reflectance", def.material.reflectance))
        return std::nullopt;
    if (j.contains("emissive")) {
        const nlohmann::json& e = j["emissive"];
        bool ok = e.is_array() && e.size() == 3;
        for (std::size_t c = 0; ok && c < 3; ++c) ok = e[c].is_number() && e[c].get<double>() >= 0.0;
        if (!ok) {
            error = "\"emissive\" must be [r, g, b] with non-negative linear values";
            return std::nullopt;
        }
        def.material.emissive = {e[0].get<float>(), e[1].get<float>(), e[2].get<float>()};
    }
    if (j.contains("translucency")) {
        const nlohmann::json& t = j["translucency"];
        if (!t.is_object()) {
            error = "\"translucency\" must be an object { \"absorption\": [r,g,b], \"emission\": [r,g,b] }";
            return std::nullopt;
        }
        Translucency tr;
        for (const auto& [key, value] : t.items()) {
            bool ok = (key == "absorption" || key == "emission") && value.is_array() && value.size() == 3;
            for (std::size_t c = 0; ok && c < 3; ++c) ok = value[c].is_number() && value[c].get<double>() >= 0.0;
            if (!ok) {
                error = "\"translucency\": \"" + key + "\" must be absorption or emission, [r, g, b] with non-negative values";
                return std::nullopt;
            }
            const glm::vec3 v{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            (key == "absorption" ? tr.absorption : tr.emission) = v;
        }
        def.translucency = tr;
    }
    if (j.contains("cutout")) {
        if (!j["cutout"].is_boolean()) {
            error = "\"cutout\" must be true or false";
            return std::nullopt;
        }
        def.cutout = j["cutout"].get<bool>();
        if (def.cutout && def.translucency) {
            error = "\"cutout\" and \"translucency\" exclude each other";
            return std::nullopt;
        }
    }
    return def;
}

std::string checkTextures(const ParsedDefinition& def,
                          const std::function<const Image*(const std::string& file)>& image) {
    for (const TextureRef& ref : def.textures) {
        const Image* img = image(ref.file);
        if (!img) return "cannot load texture \"" + ref.file + "\"";
        const std::string size = std::to_string(img->width) + "×" + std::to_string(img->height);
        if (ref.set == VariantSet::None) {
            if (img->width != kTextureSize || img->height != kTextureSize)
                return "texture \"" + ref.file + "\" is " + size + "; block textures are 16×16";
            continue;
        }
        const std::uint32_t rows = (ref.tiles + kSetColumns - 1) / kSetColumns;
        const std::uint32_t w = kTextureSize * kSetColumns, h = kTextureSize * rows;
        if (img->width != w || img->height != h)
            return "variant set \"" + ref.file + "\" is " + size + "; " + std::to_string(ref.tiles) +
                   " tiles of 16×16, 4 per row, need " + std::to_string(w) + "×" + std::to_string(h);
    }
    return "";
}

std::optional<BlockTypeId> BlockRegistry::find(std::string_view name) const {
    for (std::size_t i = 0; i < defs_.size(); ++i)
        if (defs_[i].name == name) return static_cast<BlockTypeId>(i);
    return std::nullopt;
}

BlockRegistry BlockRegistry::load(const std::string& dir) {
    BlockRegistry reg;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    if (ec) {
        reg.errors_.push_back(dir + ": " + ec.message());
        return reg;
    }
    std::sort(files.begin(), files.end());

    std::map<std::string, Image> images;          // by path; loaded on demand
    std::map<std::string, std::uint32_t> layerOf;  // by path; accepted definitions only
    for (const fs::path& file : files) {
        auto reject = [&](const std::string& why) { reg.errors_.push_back(file.string() + ": " + why); };
        std::ifstream in(file);
        std::stringstream text;
        text << in.rdbuf();
        std::string error;
        const auto parsed = parseDefinition(text.str(), error);
        if (!parsed) {
            reject(error);
            continue;
        }
        if (reg.find(parsed->name)) {
            reject("duplicate block name \"" + parsed->name + "\"");
            continue;
        }
        if (parsed->ulid == StateTable::airUlid()) {
            reject("\"ulid\" " + parsed->ulid.text() + " is air's");
            continue;
        }
        if (const auto other = std::find_if(reg.defs_.begin(), reg.defs_.end(), [&](const BlockDef& d) { return d.ulid == parsed->ulid; });
            other != reg.defs_.end()) {
            reject("\"ulid\" " + parsed->ulid.text() + " is also \"" + other->name + "\"'s");
            continue;
        }
        auto pathOf = [&](const std::string& tex) { return (file.parent_path() / tex).string(); };
        error = checkTextures(*parsed, [&](const std::string& tex) -> const Image* {
            const std::string path = pathOf(tex);
            if (auto it = images.find(path); it != images.end()) return &it->second;
            std::string loadError;
            auto img = loadPng(path, loadError);
            return img ? &images.emplace(path, std::move(*img)).first->second : nullptr;
        });
        if (!error.empty()) {
            reject(error);
            continue;
        }

        BlockDef def{.ulid = parsed->ulid, .name = parsed->name, .symmetry = parsed->symmetry,
                     .properties = parsed->properties, .material = parsed->material, .translucency = parsed->translucency,
                     .cutout = parsed->cutout};
        const orient::SymmetryInfo& info = orient::symmetry(def.symmetry);
        std::map<Face, std::uint32_t> repLayer;
        std::map<Face, VariantSet> repSet;
        std::map<Face, std::uint32_t> repTiles;
        for (const TextureRef& ref : parsed->textures) {
            const std::string path = pathOf(ref.file);
            auto [it, inserted] = layerOf.try_emplace(path, static_cast<std::uint32_t>(reg.layers_.size()));
            if (inserted) {
                if (ref.set == VariantSet::None) {
                    reg.layers_.push_back(images.at(path));
                } else {
                    for (Image& tile : splitSet(images.at(path), ref.tiles)) reg.layers_.push_back(std::move(tile));
                }
            }
            repLayer[ref.face] = it->second;
            repSet[ref.face] = ref.set;
            repTiles[ref.face] = ref.tiles;
        }
        for (int f = 0; f < orient::kFaceCount; ++f) {
            const auto fi = static_cast<std::size_t>(f);
            def.faceLayer[fi] = repLayer.at(info.repOf[fi]);
            def.faceTurns[fi] = info.repTurns[fi];
            def.faceSet[fi] = repSet.at(info.repOf[fi]);
            def.faceTiles[fi] = repTiles.at(info.repOf[fi]);
        }
        reg.defs_.push_back(std::move(def));
    }
    // Every state of every definition, interned now (world/block-states); the
    // registry's type t is the table's definition t + 1 (air is 0).
    for (std::size_t t = 0; t < reg.defs_.size(); ++t) {
        const BlockDef& d = reg.defs_[t];
        reg.states_->addDefinition(d.ulid, d.name, static_cast<BlockTypeId>(t), &d, d.symmetry, d.properties, true);
    }
    return reg;
}
