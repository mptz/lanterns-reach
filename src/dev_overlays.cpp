#include "dev_overlays.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "dev_ui.h"
#include "placement.h"

namespace shader {
using namespace glm;
#include "heat.glsl"
}  // namespace shader

std::string itemName(const Item& item, const BlockRegistry& registry) {
    switch (item.kind) {
        case Item::Kind::Pick: return "pick";
        case Item::Kind::Wrench: return "wrench";
        case Item::Kind::Shovel: return "shovel";
        case Item::Kind::Container: return "container";
        case Item::Kind::Block: break;
    }
    return registry.def(item.block).name;
}

// Hotbar icons (placeholder, v0.1.8): a block slot shows one 16×16 face —
// its front (local −y) — at a whole-number scale times the DPI scale; a
// connected set shows its fully framed tile over its tint; tools show a
// letter.
constexpr float kIconScale = 3.0f;  // texels → points before DPI scaling; chosen by eye

namespace {

char toolLetter(Item::Kind k) {
    switch (k) {
        case Item::Kind::Pick: return 'P';
        case Item::Kind::Wrench: return 'W';
        case Item::Kind::Shovel: return 'S';
        case Item::Kind::Container: return 'C';
        case Item::Kind::Block: break;
    }
    return '?';
}

// One hand's hotbar in its own lower corner (input/hotbars): the slots, a
// highlight on the selected one with its name centred above it (nudged only
// as far as needed to stay on screen), and the hand's planned action — the
// same planned edit a click applies.
void hotbarCorner(const Game& game, Hand h, const PlannedEdit& plan, const std::function<ImTextureID(std::uint32_t)>& icon) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const float iconSize = std::round(static_cast<float>(kTextureSize) * kIconScale * dpi);
    const float pad = std::round(4.0f * dpi), margin = std::round(10.0f * dpi);
    const Hotbar& bar = game.hands.bar(h);
    const bool left = h == Hand::Left;
    const auto count = static_cast<float>(std::max<std::size_t>(bar.slots().size(), 1));
    const float width = count * (iconSize + pad) + pad;
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 size(std::max(width, 0.0f), iconSize + 2.0f * pad + 2.0f * lineH + pad);
    const ImVec2 pos(left ? margin : display.x - margin - size.x, display.y - margin - size.y);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin(left ? "hotbar left" : "hotbar right", nullptr, kFlags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 frame = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
        const ImU32 gold = ImGui::GetColorU32(ImVec4(1.0f, 0.7f, 0.1f, 1.0f));
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        const float top = pos.y + lineH;  // the label line above the slots
        // The planned action, under the slots, aligned to the outer edge.
        const std::string action = std::string(left ? "LMB " : "RMB ") + (h == game.hands.main() ? "main" : "off") +
                                   " -> " + plan.action;
        const ImVec2 actionSize = ImGui::CalcTextSize(action.c_str());
        const float actionY = top + iconSize + 2.0f * pad + pad * 0.5f;
        const float actionX = left ? pos.x : std::max(0.0f, pos.x + size.x - actionSize.x);
        dl->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + width, top + iconSize + 2.0f * pad), frame, pad);
        DevUi::nearestSampler(dl);
        for (std::size_t i = 0; i < bar.slots().size(); ++i) {
            const Item& item = bar.slots()[i];
            const ImVec2 a(pos.x + pad + static_cast<float>(i) * (iconSize + pad), top + pad);
            const ImVec2 b(a.x + iconSize, a.y + iconSize);
            if (item.kind == Item::Kind::Block) {
                const BlockDef& def = game.registry.def(item.block);
                std::uint32_t layer = def.faceLayer[orient::NegY];
                ImU32 tint = IM_COL32_WHITE;
                if (def.faceSet[orient::NegY] == VariantSet::Connected) {
                    layer += kEdgeN | kEdgeE | kEdgeS | kEdgeW;  // the fully framed tile
                    const glm::vec3 t = glm::exp(-def.translucency->absorption);  // one block's transmittance
                    tint = ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 1.0f));
                    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.8f)));
                }
                if (const ImTextureID tex = icon(layer)) dl->AddImage(tex, a, b, ImVec2(0, 0), ImVec2(1, 1), tint);
            } else {
                dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(0.18f, 0.18f, 0.2f, 0.9f)));
                const char letter[2] = {toolLetter(item.kind), 0};
                const float fontSize = iconSize * 0.6f;
                const ImVec2 ls = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, letter);
                dl->AddText(ImGui::GetFont(), fontSize, ImVec2(a.x + (iconSize - ls.x) * 0.5f, a.y + (iconSize - ls.y) * 0.5f),
                            text, letter);
            }
        }
        DevUi::linearSampler(dl);
        // The selection: a gold border and the name above, or "bare" over
        // the whole bar.
        const int sel = bar.selected();
        float labelCentre = pos.x + width * 0.5f;
        std::string label = "bare";
        if (!bar.isBare() && sel >= 0) {
            const ImVec2 a(pos.x + pad + static_cast<float>(sel) * (iconSize + pad), top + pad);
            dl->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(a.x + iconSize + 2.0f, a.y + iconSize + 2.0f), gold, 0.0f,
                        std::max(2.0f, std::round(2.0f * dpi)));
            labelCentre = a.x + iconSize * 0.5f;
            label = itemName(bar.slots()[static_cast<std::size_t>(sel)], game.registry);
        }
        // Drawn on the foreground list so it can sit outside this window,
        // clamped to the screen.
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const float lx = std::clamp(labelCentre - ts.x * 0.5f, 0.0f, std::max(0.0f, display.x - ts.x));
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(ImVec2(lx - pad, top - lineH), ImVec2(lx + ts.x + pad, top - lineH + ts.y), frame, pad);
        fg->AddText(ImVec2(lx, top - lineH), gold, label.c_str());
        fg->AddRectFilled(ImVec2(actionX - pad, actionY), ImVec2(actionX + actionSize.x + pad, actionY + actionSize.y), frame, pad);
        fg->AddText(ImVec2(actionX, actionY), text, action.c_str());
    }
    ImGui::End();
}

// `plans`: each hand's planned edit this frame (indexed by Hand), computed
// once and shared with the preview.
}  // namespace

void hud(const Game& game, const std::optional<RayHit>& target, const std::array<PlannedEdit, kHandCount>& plans,
         const std::function<ImTextureID(std::uint32_t)>& icon) {
    hotbarCorner(game, Hand::Left, plans[static_cast<std::size_t>(Hand::Left)], icon);
    hotbarCorner(game, Hand::Right, plans[static_cast<std::size_t>(Hand::Right)], icon);
    // Status at the bottom centre: preview, the container's mode, the
    // orientation choice, the target.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y - 10.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("HUD", nullptr, kFlags)) {
        const Item* held = game.mainItem();
        ImGui::Text("preview %s (Q)", game.preview ? "on" : "off");
        if (held && held->kind == Item::Kind::Container)
            ImGui::Text("container mode: %s (M cycles)", placementModeName(game.settings.container.mode));
        if (held && held->kind == Item::Kind::Block && game.latch.count(held->block))
            ImGui::TextUnformatted("orientation chosen (X held)");
        // No coordinates here (input/no-player-coordinates): just the block's
        // name. Cell, face, canonical index and distance are in F6.
        if (target) ImGui::Text("target: %s", game.registry.def(target->state.type).name.c_str());
        else ImGui::TextDisabled("target: none in reach");
    }
    ImGui::End();
}


// Always on: a small crosshair at the screen center, where the targeting ray
// goes. Light with a dark outline so it reads on any background.
void crosshair() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 c(std::floor(display.x * 0.5f) + 0.5f, std::floor(display.y * 0.5f) + 0.5f);
    constexpr float kArm = 8.0f, kGap = 2.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    for (const auto& [color, width] : {std::pair{IM_COL32(0, 0, 0, 160), 3.0f}, std::pair{IM_COL32(255, 255, 255, 230), 1.0f}}) {
        draw->AddLine(ImVec2(c.x - kArm, c.y), ImVec2(c.x - kGap, c.y), color, width);
        draw->AddLine(ImVec2(c.x + kGap, c.y), ImVec2(c.x + kArm, c.y), color, width);
        draw->AddLine(ImVec2(c.x, c.y - kArm), ImVec2(c.x, c.y - kGap), color, width);
        draw->AddLine(ImVec2(c.x, c.y + kGap), ImVec2(c.x, c.y + kArm), color, width);
    }
}

void statsOverlay(const FrameClock& clock, const GpuTimeStats& gpu, const VkContext& ctx, const Camera& camera,
                  const FrameOptions& frame, const MeshStats& mesh) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.65f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("Stats (F1)", nullptr, kFlags)) {
        ImGui::Text("sandbox %s", SANDBOX_VERSION);
        ImGui::Text("%.1f fps  %.2f ms/frame", clock.fps, clock.frameMs);
        ImGui::Separator();
        if (gpu.names.empty()) {
            ImGui::TextUnformatted("GPU pass times: n/a (no timestamp support)");
        } else {
            double total = 0.0;
            for (std::size_t i = 0; i < gpu.names.size(); ++i) {
                ImGui::Text("GPU %-12s %7.3f ms", gpu.names[i].c_str(), gpu.shown[i]);
                total += gpu.shown[i];
            }
            ImGui::Text("GPU %-12s %7.3f ms  (means over %.1f s)", "total", total, GpuTimeStats::kWindowSeconds);
        }
        ImGui::Separator();
        const WorldPos& p = camera.position;
        ImGui::Text("camera (f64)  %.4f  %.4f  %.4f", p.x, p.y, p.z);
        ImGui::Text("view %s%s%s", debugViewName(frame.view), frame.wireframe ? " + wireframe" : "",
                    frame.lighting ? " + lighting" : "");
        ImGui::Text("world mesh: %zu faces, last rebuild %.2f ms (%llu rebuilds)", mesh.faces, mesh.rebuildMs,
                    static_cast<unsigned long long>(mesh.rebuilds));
        ImGui::Text("GPU  %s", ctx.gpuName.c_str());
        ImGui::Text("validation: %s", ctx.validation ? "on" : "off");
    }
    ImGui::End();
}

// F5: the lighting panel (release the mouse with Esc to use it).
void lightingPanel(LightingSettings& ls, std::size_t lightCount) {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 10.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    if (ImGui::Begin("Lighting (F5)", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove)) {
        ImGui::SliderFloat("exposure (EV)", &ls.exposureEv, -6.0f, 6.0f, "%+.2f");
        ImGui::SliderFloat("sun", &ls.sun, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("ambient", &ls.ambient, 0.0f, 0.5f, "%.3f");
        ImGui::SliderFloat("point lights ×", &ls.pointScale, 0.0f, 4.0f, "%.2f");
        ImGui::Checkbox("light gizmos", &ls.gizmos);
        ImGui::Checkbox("light-count heatmap", &ls.heatmap);
        ImGui::Text("%zu point lights from lamp blocks (max %u)", lightCount, Renderer::kMaxPointLights);
        if (ls.heatmap) {
            // The present pass's ramp (shaders/heat.glsl).
            ImGui::TextUnformatted("lights covering a pixel:");
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            constexpr int kMax = static_cast<int>(Renderer::kMaxPointLights);
            for (int i = 1; i <= kMax; ++i) {
                const glm::vec3 rgb = shader::heat(static_cast<float>(i) / kMax);
                const float r = rgb.r, g = rgb.g, b = rgb.b;
                const ImVec2 min(at.x + static_cast<float>(i - 1) * 14.0f, at.y);
                draw->AddRectFilled(min, ImVec2(min.x + 13.0f, min.y + 13.0f), ImGui::GetColorU32(ImVec4(r, g, b, 1.0f)));
            }
            ImGui::Dummy(ImVec2(14.0f * kMax, 14.0f));
            ImGui::Text("1%*s%d", 14 * kMax / 7 - 2, "", kMax);
        }
    }
    ImGui::End();
}

// F7: the mapping-context stack and the intentions fired recently.

void inputOverlay(const IntentionFrame& f, const FiredLog& log, double now) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, ImGui::GetIO().DisplaySize.y * 0.45f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("Input (F7)", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        std::string stack;
        for (const std::string& c : f.contexts) stack += (stack.empty() ? "" : " > ") + c;
        ImGui::Text("contexts: %s", stack.c_str());
        ImGui::Text("move fwd %+.0f right %+.0f up %+.0f%s", f.forward, f.right, f.up, f.fast ? "  fast" : "");
        ImGui::Text("use held: left %s  right %s", f.useHeld[0] ? "yes" : "no", f.useHeld[1] ? "yes" : "no");
        ImGui::Separator();
        ImGui::TextUnformatted("fired (newest first):");
        for (const auto& [t, s] : log.entries) ImGui::Text("  %5.2fs ago  %s", now - t, s.c_str());
    }
    ImGui::End();
}

// F4: the targeted block's local axes (from its stored, canonical rotation)
// and its symmetry keyword + canonical index at the block.
void orientationOverlay(const RayHit& target, const BlockRegistry& registry, const Renderer& renderer,
                        const Camera& camera, std::vector<DebugLine>& lines) {
    const orient::Mat3i& m = orient::group().matrix[target.state.orientation];
    const WorldPos c = cellCenter(target.cell);
    const glm::vec3 colors[3] = {{1.0f, 0.05f, 0.05f}, {0.05f, 1.0f, 0.05f}, {0.1f, 0.25f, 1.0f}};
    for (std::size_t axis = 0; axis < 3; ++axis) {  // column `axis` = where local axis `axis` points
        const glm::dvec3 dir(m[0][axis], m[1][axis], m[2][axis]);
        lines.push_back({c, c + dir * 1.0, colors[axis]});
    }
    const BlockDef& def = registry.def(target.state.type);
    const glm::vec4 clip = renderer.viewProjection(camera) *
                           glm::vec4(rebase(c + glm::dvec3(0.0, 0.0, 0.85), camera.position).v, 1.0f);
    if (clip.w <= Camera::kNear) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 at((clip.x / clip.w * 0.5f + 0.5f) * display.x, (clip.y / clip.w * 0.5f + 0.5f) * display.y);
    char text[96];
    std::snprintf(text, sizeof text, "%s\n%s #%d", def.name.c_str(), orient::symmetry(def.symmetry).keyword,
                  target.state.orientation);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 min(at.x - size.x * 0.5f - 3.0f, at.y - size.y - 3.0f), max(at.x + size.x * 0.5f + 3.0f, at.y);
    draw->AddRectFilled(min, max, IM_COL32(0, 0, 0, 170), 3.0f);
    draw->AddText(ImVec2(min.x + 3.0f, min.y + 1.5f), IM_COL32(255, 255, 255, 255), text);
}

// F6: the ray, the hit face's outline and normal, the hit point, and the
// hit-point regions the placement rule reads (quadrants → nearest vertex,
// diagonals → nearest edge), with the selected vertex and edge highlighted.
void targetingOverlay(const Camera& camera, const std::optional<RayHit>& target, std::vector<DebugLine>& lines) {
    const glm::dvec3 fwd = camera.forward();
    const WorldPos end = target ? target->point : camera.position + fwd * kReach;
    lines.push_back({camera.position + glm::dvec3(0.0, 0.0, -0.05), end, {1.0f, 1.0f, 1.0f}});

    // The target's coordinates live here, in the dev overlay, not the HUD.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("Targeting (F6)", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        if (target) {
            int face = -1;
            for (int f = 0; f < orient::kFaceCount; ++f)
                if (orient::faceNormal(static_cast<orient::Face>(f)) == target->normal) face = f;
            ImGui::Text("cell (%lld, %lld, %lld)", static_cast<long long>(target->cell.x),
                        static_cast<long long>(target->cell.y), static_cast<long long>(target->cell.z));
            ImGui::Text("face %s", face < 0 ? "inside" : orient::kFaceNames[static_cast<std::size_t>(face)]);
            ImGui::Text("canonical orientation #%d", target->state.orientation);
            ImGui::Text("distance %.2f blocks (reach %.0f)", target->distance, kReach);
        } else {
            ImGui::Text("no target within %.0f blocks", kReach);
        }
    }
    ImGui::End();
    if (!target || target->normal == glm::ivec3(0)) return;
    const glm::ivec3 n = target->normal;
    const glm::dvec3 nd(n), R(placement::faceRight(n)), U(placement::faceUp(n));
    const WorldPos fc = cellCenter(target->cell) + nd * 0.505;  // just off the face
    auto at = [&](double u, double v) { return fc + R * (u - 0.5) + U * (v - 0.5); };
    const glm::vec3 outline(0.2f, 1.0f, 1.0f), region(0.25f, 0.45f, 0.5f), pick(1.0f, 0.3f, 0.9f);
    lines.push_back({at(0, 0), at(1, 0), outline});
    lines.push_back({at(1, 0), at(1, 1), outline});
    lines.push_back({at(1, 1), at(0, 1), outline});
    lines.push_back({at(0, 1), at(0, 0), outline});
    lines.push_back({at(0.5, 0), at(0.5, 1), region});  // quadrants: nearest vertex
    lines.push_back({at(0, 0.5), at(1, 0.5), region});
    lines.push_back({at(0, 0), at(1, 1), region});      // diagonals: nearest edge
    lines.push_back({at(1, 0), at(0, 1), region});
    lines.push_back({fc, fc + nd * 0.5, {1.0f, 0.9f, 0.2f}});  // normal
    const glm::dvec2 uv = placement::faceCoords(target->point - (cellCenter(target->cell) + nd * 0.5), n);
    const WorldPos hp = at(uv.x, uv.y);
    lines.push_back({hp + R * -0.06, hp + R * 0.06, pick});
    lines.push_back({hp + U * -0.06, hp + U * 0.06, pick});
    // The vertex and edge the rule would pick (ties as in placement.cpp).
    const WorldPos vtx = at(uv.x >= 0.5 ? 1 : 0, uv.y >= 0.5 ? 1 : 0);
    lines.push_back({vtx, vtx + nd * 0.25, pick});
    const double d[4] = {uv.x, 1.0 - uv.x, uv.y, 1.0 - uv.y};
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (d[i] < d[best]) best = i;
    const WorldPos ea[4] = {at(0, 0), at(1, 0), at(0, 0), at(0, 1)}, eb[4] = {at(0, 1), at(1, 1), at(1, 0), at(1, 1)};
    lines.push_back({ea[best] + nd * 0.01, eb[best] + nd * 0.01, pick});
}

// The 12 edges of the axis-aligned cube [lo, lo + size]³.
namespace {

void boxEdges(const WorldPos& lo, double size, const glm::vec3& color, std::vector<DebugLine>& lines) {
    for (int axis = 0; axis < 3; ++axis)
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b) {
                glm::dvec3 p(0.0), q(0.0);
                p[(axis + 1) % 3] = q[(axis + 1) % 3] = a * size;
                p[(axis + 2) % 3] = q[(axis + 2) % 3] = b * size;
                q[axis] = size;
                lines.push_back({lo + p, lo + q, color});
            }
}

}  // namespace

// F10: the grid's bounds.
void boundsOverlay(const DenseGrid& grid, std::vector<DebugLine>& lines) {
    boxEdges(grid.origin(), grid.size(), {0.9f, 0.5f, 0.1f}, lines);
}


// The preview outline: a cell's 12 edges, inflated so they sit just outside
// its faces (chosen by eye: visible, not detached).
constexpr double kOutlineInflate = 0.01;  // blocks, on each side

void outlineCell(const CellPos& c, const glm::vec3& color, std::vector<DebugLine>& lines) {
    boxEdges(cellCorner(c) + glm::dvec3(-kOutlineInflate), 1.0 + 2.0 * kOutlineInflate, color, lines);
}

// F8: the raw edit set of the main hand's planned edit (cell, before →
// after), and the tool settings (shovel fill, the container, the latch).
void editSetOverlay(Game& game, const PlannedEdit& plan) {
    const BlockRegistry& reg = game.registry;
    auto stateText = [&](BlockState st) {
        if (st.isAir()) return std::string("air");
        return reg.def(st.type).name + " #" + std::to_string(st.orientation);
    };
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.8f);
    if (ImGui::Begin("Edit set & tools (F8)", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::Text("%s", plan.action.c_str());
        ImGui::Text("%zu edits (fill %zu, cut %zu, blocked %zu)", plan.edits.edits.size(), plan.fill, plan.cut, plan.blocked);
        std::size_t shown = 0;
        for (const Edit& e : plan.edits.edits) {
            if (++shown > 24) {
                ImGui::TextDisabled("… %zu more", plan.edits.edits.size() - 24);
                break;
            }
            ImGui::Text("  (%lld, %lld, %lld)  %s -> %s", static_cast<long long>(e.cell.x), static_cast<long long>(e.cell.y),
                        static_cast<long long>(e.cell.z), stateText(game.world.probe(e.cell)).c_str(),
                        stateText(e.state).c_str());
        }
        ImGui::Separator();
        // Tool settings (creative unless "consume counts" is on).
        auto typeCombo = [&](const char* label, BlockTypeId& t, bool softOnly) {
            const char* current = t == BlockState::kAirType ? "(empty)" : reg.def(t).name.c_str();
            if (ImGui::BeginCombo(label, current)) {
                if (!softOnly && ImGui::Selectable("(empty)", t == BlockState::kAirType)) t = BlockState::kAirType;
                for (std::size_t i = 0; i < reg.defs().size(); ++i) {
                    const auto id = static_cast<BlockTypeId>(i);
                    if (softOnly && !isSoft(reg, id)) continue;
                    if (ImGui::Selectable(reg.def(id).name.c_str(), t == id)) t = id;
                }
                ImGui::EndCombo();
            }
        };
        typeCombo("shovel fill", game.settings.shovelFill, true);
        ContainerState& box = game.settings.container;
        for (std::size_t i = 0; i < box.slots.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(140.0f);
            typeCombo("##type", box.slots[i].type, false);
            ImGui::SameLine();
            int count = static_cast<int>(box.slots[i].count);
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("count", &count)) box.slots[i].count = static_cast<std::uint32_t>(std::max(count, 0));
            ImGui::PopID();
        }
        if (ImGui::BeginCombo("container mode", placementModeName(box.mode))) {
            for (int m = 0; m < kPlacementModeCount; ++m)
                if (ImGui::Selectable(placementModeName(static_cast<PlacementMode>(m)), static_cast<int>(box.mode) == m))
                    box.mode = static_cast<PlacementMode>(m);
            ImGui::EndCombo();
        }
        ImGui::SliderInt("patch N", &box.patch, 1, 9);
        ImGui::Checkbox("consume counts (survival rule)", &box.consume);
        ImGui::Text("draws: %llu (the in-order cursor and RNG state; advances per block placed)",
                    static_cast<unsigned long long>(box.draws));
        ImGui::Separator();
        ImGui::TextUnformatted("orientation choice (while X is held):");
        for (const auto& [type, rot] : game.latch) ImGui::Text("  %s -> #%d", reg.def(type).name.c_str(), rot);
        if (game.latch.empty()) ImGui::TextDisabled("  none");
    }
    ImGui::End();
}

// F9: what the translucency view shows, and the MBOIT tunables.
void translucencyPanel(OitSettings& oit, FaceOrder& order, bool& reorder) {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, ImGui::GetIO().DisplaySize.y - 120.0f),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    if (ImGui::Begin("Translucency (F9)", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::RadioButton("absorbance", &oit.view, 1);
        ImGui::SameLine();
        ImGui::RadioButton("moments b1 b2", &oit.view, 2);
        ImGui::SameLine();
        ImGui::RadioButton("moments b3 b4", &oit.view, 3);
        ImGui::SameLine();
        ImGui::RadioButton("image", &oit.view, 0);
        ImGui::SliderFloat("moment bias", &oit.momentBias, 1e-9f, 1e-2f, "%.1e", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("overestimation", &oit.overestimation, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("glass frames", &oit.glassFrames);
        int kind = static_cast<int>(order.kind);
        const char* kinds[] = {"as built", "forward", "reverse", "shuffle"};
        ImGui::Combo("face order", &kind, kinds, 4);
        int seed = static_cast<int>(order.seed);
        if (kind == 3) ImGui::InputInt("seed", &seed);
        if (kind != static_cast<int>(order.kind) || static_cast<std::uint64_t>(seed) != order.seed) {
            order = {static_cast<FaceOrder::Kind>(kind), static_cast<std::uint64_t>(std::max(seed, 0))};
            reorder = true;
        }
        if (ImGui::Button("re-sort from here")) reorder = true;
    }
    ImGui::End();
}

// --check-absorbance: scripted views of the test scene. Each case looks at a
// known arrangement; the OIT absorbance at the center pixel is read back and
// compared with Beer–Lambert, σ × the analytic chord of that pixel's ray
