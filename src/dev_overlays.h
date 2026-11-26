#pragma once
// The dev UI (render/dev-ui): the HUD (corner hotbars, status), the crosshair
// and the F1–F10 overlays and panels. Built each frame between
// DevUi::beginFrame and the renderer's draw; overlays add debug lines.

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "camera.h"
#include "frame_stats.h"
#include "game.h"
#include "chunk.h"
#include "intentions.h"
#include "renderer.h"
#include "translucency.h"
#include "vk_context.h"

constexpr glm::vec3 kRemovalColor{1.0f, 0.12f, 0.1f};      // linear; red: will be removed
constexpr glm::vec3 kUnaffordableColor{1.0f, 0.65f, 0.05f};  // linear; amber: the container is short

// F7's log of recent intentions.
struct FiredLog {
    std::deque<std::pair<double, std::string>> entries;  // (time, description)
    void add(double now, const std::string& s) {
        entries.emplace_front(now, s);
        if (entries.size() > 12) entries.pop_back();
    }
};

std::string itemName(const Item& item, const BlockRegistry& registry);
// `plans`: each hand's planned edit this frame (indexed by Hand), computed
// once and shared with the preview.
void hud(const Game& game, const std::optional<RayHit>& target, const std::array<PlannedEdit, kHandCount>& plans,
         const std::function<ImTextureID(std::uint32_t)>& icon);
void crosshair();
void statsOverlay(const FrameClock& clock, const GpuTimeStats& gpu, const VkContext& ctx, const Camera& camera,
                  const FrameOptions& frame, const MeshStats& mesh);
void lightingPanel(LightingSettings& ls, std::size_t lightCount);
void inputOverlay(const IntentionFrame& f, const FiredLog& log, double now);
void orientationOverlay(const RayHit& target, const BlockRegistry& registry, const Renderer& renderer,
                        const Camera& camera, std::vector<DebugLine>& lines);
void targetingOverlay(const Camera& camera, const std::optional<RayHit>& target, std::vector<DebugLine>& lines);
void boundsOverlay(const ChunkId& chunk, std::vector<DebugLine>& lines);
// The preview outline: a cell's 12 edges, just outside its faces.
void outlineCell(const CellPos& c, const glm::vec3& color, std::vector<DebugLine>& lines);
void editSetOverlay(Game& game, const PlannedEdit& plan);
void translucencyPanel(OitSettings& oit, FaceOrder& order, bool& reorder);
