#include "scenario.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Include chains deeper than this are a cycle or a mistake.
constexpr int kMaxIncludeDepth = 8;

constexpr const char* kBufferViews[] = {"lit", "albedo", "normal", "depth", "roughmetal", "material", "variant"};
constexpr const char* kOverlayNames[kOverlayCount] = {"stats",     "wireframe", "orientation", "lighting",
                                                      "targeting", "input",     "bounds",      "editSet"};

// A parse error carrying the field path, e.g. "events[3].key".
struct Error {
    std::string message;
};

[[noreturn]] void fail(const std::string& path, const std::string& what) { throw Error{path + ": " + what}; }

void onlyFields(const json& j, const std::string& path, std::initializer_list<const char*> allowed) {
    if (!j.is_object()) fail(path, "must be an object");
    for (const auto& [key, value] : j.items())
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return key == a; }))
            fail(path.empty() ? key : path + "." + key, "unknown field");
}

std::string at(const std::string& path, const char* key) { return path.empty() ? key : path + "." + key; }

bool boolean(const json& j, const std::string& path) {
    if (!j.is_boolean()) fail(path, "must be true or false");
    return j.get<bool>();
}

long integer(const json& j, const std::string& path, long lo, long hi) {
    if (!j.is_number_integer()) fail(path, "must be an integer");
    const long v = j.get<long>();
    if (v < lo || v > hi) fail(path, "must be in " + std::to_string(lo) + "…" + std::to_string(hi));
    return v;
}

double number(const json& j, const std::string& path) {
    if (!j.is_number()) fail(path, "must be a number");
    return j.get<double>();
}

std::string string(const json& j, const std::string& path) {
    if (!j.is_string()) fail(path, "must be a string");
    return j.get<std::string>();
}

WorldPos vec3(const json& j, const std::string& path) {
    if (!j.is_array() || j.size() != 3) fail(path, "must be [x, y, z]");
    return {number(j[0], path + "[0]"), number(j[1], path + "[1]"), number(j[2], path + "[2]")};
}

CellPos cell(const json& j, const std::string& path) {
    if (!j.is_array() || j.size() != 3) fail(path, "must be [x, y, z] (integers)");
    constexpr long kBig = 1L << 40;  // far beyond any test world
    return {integer(j[0], path + "[0]", -kBig, kBig), integer(j[1], path + "[1]", -kBig, kBig),
            integer(j[2], path + "[2]", -kBig, kBig)};
}

std::pair<int, int> size2(const json& j, const std::string& path) {
    if (!j.is_array() || j.size() != 2) fail(path, "must be [width, height]");
    return {static_cast<int>(integer(j[0], path + "[0]", 64, 16384)),
            static_cast<int>(integer(j[1], path + "[1]", 64, 16384))};
}

void parseScene(const json& j, Scenario& s) {
    if (!j.is_array()) fail("scene", "must be a list of operations");
    for (std::size_t i = 0; i < j.size(); ++i) {
        const std::string path = "scene[" + std::to_string(i) + "]";
        const json& op = j[i];
        SceneOp o;
        if (op.is_object() && op.contains("fill")) {
            onlyFields(op, path, {"fill", "from", "to"});
            o.kind = SceneOp::Kind::Fill;
            o.type = string(op["fill"], path + ".fill");
            if (!op.contains("from") || !op.contains("to")) fail(path, "a fill needs \"from\" and \"to\"");
            o.from = cell(op["from"], path + ".from");
            o.to = cell(op["to"], path + ".to");
        } else if (op.is_object() && op.contains("place")) {
            onlyFields(op, path, {"place", "at", "rotation"});
            o.kind = SceneOp::Kind::Place;
            o.type = string(op["place"], path + ".place");
            if (!op.contains("at")) fail(path, "a place needs \"at\"");
            o.from = o.to = cell(op["at"], path + ".at");
            if (op.contains("rotation"))
                o.rotation = static_cast<orient::Rot>(integer(op["rotation"], path + ".rotation", 0, 23));
        } else {
            fail(path, "an operation is {\"fill\": type, \"from\", \"to\"} or {\"place\": type, \"at\", \"rotation\"}");
        }
        s.scene.push_back(std::move(o));
    }
}

void parseEvents(const json& j, Scenario& s) {
    if (!j.is_array()) fail("events", "must be a list");
    s.events.clear();
    for (std::size_t i = 0; i < j.size(); ++i) {
        const std::string path = "events[" + std::to_string(i) + "]";
        const json& e = j[i];
        if (!e.is_object() || !e.contains("frame")) fail(path, "an event is an object with a \"frame\"");
        const long frame = integer(e["frame"], path + ".frame", 0, 1L << 30);
        const bool shift = e.contains("shift") && boolean(e["shift"], path + ".shift");
        auto keyCode = [&](const char* field) {
            const std::string name = string(e[field], at(path, field));
            const auto code = keyFromName(name);
            if (!code) fail(at(path, field), "unknown key \"" + name + "\" (a–z, 0–9, F1–F12)");
            return *code;
        };
        if (e.contains("key")) {
            onlyFields(e, path, {"frame", "key", "shift"});
            const int code = keyCode("key");
            s.events.push_back({frame, {Device::Key, code, true, shift}});
            s.events.push_back({frame + 1, {Device::Key, code, false, shift}});
        } else if (e.contains("hold")) {
            onlyFields(e, path, {"frame", "hold", "until", "shift"});
            const int code = keyCode("hold");
            if (!e.contains("until")) fail(path, "a hold needs \"until\" (the release frame)");
            const long until = integer(e["until"], path + ".until", frame + 1, 1L << 30);
            s.events.push_back({frame, {Device::Key, code, true, shift}});
            s.events.push_back({until, {Device::Key, code, false, shift}});
        } else if (e.contains("click")) {
            onlyFields(e, path, {"frame", "click", "shift"});
            const std::string b = string(e["click"], path + ".click");
            const int code = b == "left" ? GLFW_MOUSE_BUTTON_LEFT
                             : b == "right" ? GLFW_MOUSE_BUTTON_RIGHT
                             : b == "middle" ? GLFW_MOUSE_BUTTON_MIDDLE
                                             : -1;
            if (code < 0) fail(path + ".click", "unknown button \"" + b + "\" (left | right | middle)");
            s.events.push_back({frame, {Device::MouseButton, code, true, shift}});
            s.events.push_back({frame + 1, {Device::MouseButton, code, false, shift}});
        } else if (e.contains("wheel")) {
            onlyFields(e, path, {"frame", "wheel", "shift"});
            const std::string d = string(e["wheel"], path + ".wheel");
            if (d != "up" && d != "down") fail(path + ".wheel", "must be \"up\" or \"down\"");
            s.events.push_back({frame, {Device::Wheel, d == "up" ? +1 : -1, true, shift}});
        } else {
            fail(path, "an event has one of \"key\", \"hold\", \"click\", \"wheel\"");
        }
    }
    std::stable_sort(s.events.begin(), s.events.end(),
                     [](const ScenarioEvent& a, const ScenarioEvent& b) { return a.frame < b.frame; });
}

void parseChecks(const json& j, Scenario& s) {
    onlyFields(j, "checks", {"absorbance"});
    s.absorbance.clear();
    if (!j.contains("absorbance")) return;
    const json& list = j["absorbance"];
    if (!list.is_array()) fail("checks.absorbance", "must be a list");
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string path = "checks.absorbance[" + std::to_string(i) + "]";
        const json& c = list[i];
        onlyFields(c, path, {"name", "eye", "look", "cubes"});
        AbsorbanceCase k;
        k.name = c.contains("name") ? string(c["name"], path + ".name") : "case " + std::to_string(i);
        if (!c.contains("eye") || !c.contains("look") || !c.contains("cubes")) fail(path, "needs eye, look and cubes");
        k.eye = vec3(c["eye"], path + ".eye");
        k.look = vec3(c["look"], path + ".look");
        if (!c["cubes"].is_array()) fail(path + ".cubes", "must be a list");
        for (std::size_t q = 0; q < c["cubes"].size(); ++q) {
            const std::string cp = path + ".cubes[" + std::to_string(q) + "]";
            const json& cube = c["cubes"][q];
            onlyFields(cube, cp, {"at", "type"});
            if (!cube.contains("at") || !cube.contains("type")) fail(cp, "needs at and type");
            k.cubes.emplace_back(cell(cube["at"], cp + ".at"), string(cube["type"], cp + ".type"));
        }
        s.absorbance.push_back(std::move(k));
    }
}

void parseInto(const json& j, Scenario& s, const std::string& directory, int depth) {
    onlyFields(j, "", {"include", "world", "scene", "camera", "window", "view", "render", "tools", "events",
                       "frames", "checks"});
    if (j.contains("include")) {
        if (depth >= kMaxIncludeDepth) fail("include", "includes nest too deeply (a cycle?)");
        const fs::path p = fs::path(directory) / string(j["include"], "include");
        std::ifstream in(p);
        if (!in) fail("include", "cannot read " + p.string());
        std::stringstream text;
        text << in.rdbuf();
        const json inc = json::parse(text.str(), nullptr, false);
        if (inc.is_discarded()) fail("include", p.string() + " is not valid JSON");
        try {
            parseInto(inc, s, p.parent_path().string(), depth + 1);
        } catch (const Error& e) {
            throw Error{p.filename().string() + ": " + e.message};
        }
    }
    if (j.contains("world")) {
        onlyFields(j["world"], "world", {"grid"});
        if (j["world"].contains("grid")) s.grid = static_cast<int>(integer(j["world"]["grid"], "world.grid", 1, 512));
    }
    if (j.contains("scene")) parseScene(j["scene"], s);
    if (j.contains("camera")) {
        const json& c = j["camera"];
        onlyFields(c, "camera", {"pos", "look"});
        if (c.contains("pos")) s.camera = vec3(c["pos"], "camera.pos");
        if (c.contains("look")) s.look = vec3(c["look"], "camera.look");
    }
    if (j.contains("window")) {
        const json& w = j["window"];
        onlyFields(w, "window", {"size", "resize"});
        if (w.contains("size")) std::tie(s.width, s.height) = size2(w["size"], "window.size");
        if (w.contains("resize")) {
            onlyFields(w["resize"], "window.resize", {"frame", "size"});
            if (!w["resize"].contains("frame") || !w["resize"].contains("size"))
                fail("window.resize", "needs frame and size");
            s.resizeFrame = integer(w["resize"]["frame"], "window.resize.frame", 0, 1L << 30);
            std::tie(s.resizeWidth, s.resizeHeight) = size2(w["resize"]["size"], "window.resize.size");
        }
    }
    if (j.contains("view")) {
        const json& v = j["view"];
        onlyFields(v, "view", {"buffer", "overlays", "preview"});
        if (v.contains("buffer")) {
            const std::string name = string(v["buffer"], "view.buffer");
            const auto it = std::find_if(std::begin(kBufferViews), std::end(kBufferViews),
                                         [&](const char* b) { return name == b; });
            if (it == std::end(kBufferViews))
                fail("view.buffer", "unknown view \"" + name + "\" (lit, albedo, normal, depth, roughmetal, material, variant)");
            s.bufferView = static_cast<int>(it - std::begin(kBufferViews)) + 1;
        }
        if (v.contains("overlays")) {
            if (!v["overlays"].is_array()) fail("view.overlays", "must be a list of names");
            std::fill(std::begin(s.overlays), std::end(s.overlays), false);
            for (std::size_t i = 0; i < v["overlays"].size(); ++i) {
                const std::string path = "view.overlays[" + std::to_string(i) + "]";
                const std::string name = string(v["overlays"][i], path);
                const auto it = std::find_if(std::begin(kOverlayNames), std::end(kOverlayNames),
                                             [&](const char* o) { return name == o; });
                if (it == std::end(kOverlayNames))
                    fail(path, "unknown overlay \"" + name +
                                   "\" (stats, wireframe, orientation, lighting, targeting, input, bounds, editSet)");
                s.overlays[it - std::begin(kOverlayNames)] = true;
            }
        }
        if (v.contains("preview")) s.preview = boolean(v["preview"], "view.preview");
    }
    if (j.contains("render")) {
        const json& r = j["render"];
        onlyFields(r, "render", {"anisotropy", "variants", "glassFrames", "momentBias", "faceOrder", "oitView",
                                 "mergedQuad"});
        if (r.contains("anisotropy")) s.anisotropy = static_cast<float>(number(r["anisotropy"], "render.anisotropy"));
        if (r.contains("variants")) s.variants = boolean(r["variants"], "render.variants");
        if (r.contains("glassFrames")) s.glassFrames = boolean(r["glassFrames"], "render.glassFrames");
        if (r.contains("momentBias")) s.momentBias = static_cast<float>(number(r["momentBias"], "render.momentBias"));
        if (r.contains("faceOrder")) {
            const std::string o = string(r["faceOrder"], "render.faceOrder");
            const auto order = FaceOrder::parse(o);
            if (!order) fail("render.faceOrder", "\"" + o + "\" is not forward | reverse | shuffle:<seed>");
            s.faceOrder = *order;
        }
        if (r.contains("oitView")) s.oitView = static_cast<int>(integer(r["oitView"], "render.oitView", 0, 3));
        if (r.contains("mergedQuad")) s.mergedQuad = boolean(r["mergedQuad"], "render.mergedQuad");
    }
    if (j.contains("tools")) {
        const json& t = j["tools"];
        onlyFields(t, "tools", {"patch", "consume"});
        if (t.contains("patch")) s.patch = static_cast<int>(integer(t["patch"], "tools.patch", 1, 9));
        if (t.contains("consume")) s.consume = boolean(t["consume"], "tools.consume");
    }
    if (j.contains("events")) parseEvents(j["events"], s);
    if (j.contains("frames")) s.frames = integer(j["frames"], "frames", 1, 1L << 30);
    if (j.contains("checks")) parseChecks(j["checks"], s);
}

}  // namespace

std::optional<long> Scenario::runFrames(bool capturing) const {
    if (frames) return frames;
    if (!capturing) return std::nullopt;
    // v0.1.9's rule (tools/headless_check.sh), now the engine's.
    constexpr long kMinFrames = 60, kSettle = 20, kResizeSettle = 60;
    long last = kMinFrames;
    for (const ScenarioEvent& e : events)
        if (e.event.press) last = std::max(last, e.frame + kSettle);
    if (resizeFrame) last = std::max(last, *resizeFrame + kResizeSettle);
    return last;
}

std::optional<Scenario> parseScenario(std::string_view text, const std::string& directory, std::string& error) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        error = "not valid JSON";
        return std::nullopt;
    }
    Scenario s;
    try {
        parseInto(j, s, directory, 0);
    } catch (const Error& e) {
        error = e.message;
        return std::nullopt;
    }
    return s;
}

std::optional<Scenario> loadScenario(const std::string& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path;
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();
    auto s = parseScenario(text.str(), fs::path(path).parent_path().string(), error);
    if (!s) error = path + ": " + error;
    return s;
}

EditSet sceneEdits(const Scenario& s, const BlockRegistry& registry, std::vector<std::string>* warnings) {
    EditSet out;
    std::set<std::string> missing;
    for (const SceneOp& op : s.scene) {
        BlockState state = BlockState::air();
        if (op.type != "air") {
            const auto t = registry.find(op.type);
            if (!t) {
                if (warnings && missing.insert(op.type).second)
                    warnings->push_back("no block type \"" + op.type + "\"; its scene operations are skipped");
                continue;
            }
            state = registry.state(*t, op.rotation);
        }
        for (auto z = op.from.z; z <= op.to.z; ++z)
            for (auto y = op.from.y; y <= op.to.y; ++y)
                for (auto x = op.from.x; x <= op.to.x; ++x) out.edits.push_back({{x, y, z}, state});
    }
    return out;
}

std::optional<int> keyFromName(std::string_view k) {
    if (k.size() == 1 && k[0] >= 'a' && k[0] <= 'z') return GLFW_KEY_A + (k[0] - 'a');
    if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z') return GLFW_KEY_A + (k[0] - 'A');
    if (k.size() == 1 && k[0] >= '0' && k[0] <= '9') return GLFW_KEY_0 + (k[0] - '0');
    if (k.size() >= 2 && k.size() <= 3 && (k[0] == 'F' || k[0] == 'f')) {
        int n = 0;
        for (char c : k.substr(1)) {
            if (c < '0' || c > '9') return std::nullopt;
            n = n * 10 + (c - '0');
        }
        if (n >= 1 && n <= 12) return GLFW_KEY_F1 + n - 1;
    }
    return std::nullopt;
}
