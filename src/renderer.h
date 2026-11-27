#pragma once
// Frame loop. Each frame builds the pass graph — G-buffer (+ emission into
// HDR), lighting (PBR: ambient + directional + 16 point lights, added onto
// HDR), the two moment-based OIT passes over the translucent mesh
// (absorbance, then shading), present (translucency composite + AgX), optional debug lines (F3 wireframe, F5 light
// gizmos, and whatever lines the caller passes: F4/F6/F10), then the dev UI —
// and lets the render-graph seam derive all synchronization and per-pass GPU
// timing.
//
// The world is drawn from the mesh it builds itself (WorldQuery::buildMesh):
// packed quads (packed_quad.h) which the vertex shaders pull through buffer
// device addresses, each quad's payload (texture layer, material, texture
// info) from the appearance table, uploaded from the state table. A new mesh
// replaces the old one; the old buffers live until no frame in flight can
// use them.

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "camera.h"
#include "dev_ui.h"
#include "gbuffer.h"
#include "gpu_buffer.h"
#include "scene.h"
#include "texture.h"
#include "vk_context.h"
#include "world_query.h"

// Full-screen buffer views (F2 cycles forward, Shift+F2 back).
// Variant: each opaque face's variant-set tile index as a colour (grey for
// faces without a set); the G-buffer pass writes it in place of albedo.
enum class DebugView : int { Lit = 1, Albedo = 2, Normal = 3, Depth = 4, RoughMetal = 5, Material = 6, Variant = 7 };
inline constexpr int kDebugViewCount = 7;
const char* debugViewName(DebugView v);

// Tunables exposed in the F5 lighting panel.
struct LightingSettings {
    float exposureEv = 0.0f;  // AgX input = HDR × 2^EV
    // Directional irradiance E (a white Lambert face facing the sun shows
    // exactly E) and the ambient level: chosen by eye with the AgX tonemap at
    // 0 EV (v0.1.4); placeholders until sky and shadows (render/shadows).
    float sun = 0.25f;
    float ambient = 0.04f;
    float pointScale = 1.0f;  // multiplies every point light's intensity
    bool heatmap = true;      // F5: per-pixel light-count tint
    bool gizmos = true;       // F5: light positions + radius spheres
};

// Moment-based OIT (render/transparency), tunable in the F9 panel.
struct OitSettings {
    // Bias toward a safe moment vector. 5e-4, not the paper's single-precision
    // value: boundary-integral moments are sums of large ±σ·F_k(r) terms that
    // cancel, so they carry more rounding than the paper's direct sums, and a
    // smaller bias lets summation order flip the reconstruction's root-vs-depth
    // tests (measured: 5e-7 → up to 4/255 between face orders; 5e-4 → 0).
    float momentBias = 5e-4f;
    float overestimation = 0.25f;  // β: interpolation toward the upper bound (the paper's value)
    int view = 0;                  // F9: 0 off, 1 total absorbance, 2 moments b1 b2, 3 moments b3 b4
    bool glassFrames = true;       // connected-glass frames on absorbing translucent faces
};

struct FrameOptions {
    DebugView view = DebugView::Lit;
    OitSettings oit;
    bool wireframe = false;  // F3: outlines of every face of the world mesh
    bool lighting = false;   // F5: heatmap + gizmos (as enabled in `light`) + panel
    LightingSettings light;
    bool variants = true;  // false: random/matched faces show tile 0, skipping the per-fragment hash (timing A/B)
};

// A preview ghost (input/preview-mode): a cell the main hand's planned edit
// would fill or change, drawn on the translucency path with the block's own
// face textures as faint emission (no glass frames), so its type and
// orientation read exactly as they'll be placed.
struct GhostCell {
    CellPos cell;
    StateId state;
};

// A caller-supplied overlay line in authoritative f64 coordinates (rebased
// here like everything else), depth-tested against the scene.
struct DebugLine {
    WorldPos a, b;
    glm::vec3 color{1.0f};
};

struct PassTime {
    std::string name;
    double ms;
};

class Renderer {
public:
    // Capacity of the per-frame light buffer; the lighting pass loops over
    // all of them per pixel (REVIEW D1). Arbitrary; the test scene has 4.
    static constexpr std::uint32_t kMaxPointLights = 16;

    // Uploads the registry's textures and materials; the world mesh comes
    // later through setWorldMesh.
    Renderer(VkContext& ctx, const BlockRegistry& registry);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Renders and presents one frame, ending the dev-UI frame begun with
    // devUi().beginFrame(). Recreates swapchain-sized resources when the
    // swapchain is out of date or `resized` was signalled.
    void drawFrame(const Camera& camera, const FrameOptions& options, const std::vector<PointLight>& lights,
                   const std::vector<DebugLine>& lines, const std::vector<GhostCell>& ghosts);
    void notifyResized() { resized_ = true; }
    // Replaces the world mesh (after an edit, or a translucent face reorder).
    // Cheap to call; the GPU upload is a host-visible copy.
    void setWorldMesh(const WorldMesh& mesh);

    // Readbacks (headless checks). A request applies to the next frame that
    // is recorded; its result can be taken once that frame has completed
    // (within kFramesInFlight further drawFrame calls).
    struct Capture {
        std::uint32_t width = 0, height = 0;
        std::vector<std::uint8_t> rgb;  // the final image, rows top to bottom
    };
    void requestCapture() { wantCapture_ = true; }
    std::optional<Capture> takeCapture() { return std::exchange(capture_, std::nullopt); }
    void requestAbsorbanceProbe(glm::ivec2 pixel) { wantProbe_ = pixel; }
    std::optional<glm::vec4> takeAbsorbanceProbe() { return std::exchange(probe_, std::nullopt); }  // OIT B0 texel
    vk::Extent2D extent() const { return ctx_.swapchain.extent; }

    DevUi& devUi() { return *devUi_; }
    // A block texture layer as a dev-UI image (hotbar icons; mip 0, drawn
    // with the nearest sampler).
    ImTextureID blockIcon(std::uint32_t layer);
    // Per-pass GPU times of the most recently completed frame; empty if the
    // queue has no timestamp support.
    const std::vector<PassTime>& gpuTimes() const { return gpuTimes_; }
    // Camera-relative view-projection at the current swapchain aspect.
    glm::mat4 viewProjection(const Camera& camera) const;

private:
    static constexpr std::size_t kFramesInFlight = 2;  // CPU records frame N+1 while the GPU runs N
    // Capacities (arbitrary, with room to spare): timestamp pairs per frame
    // (the graph has at most 9 passes), per-frame debug-line vertices
    // (overflow is cut), ghost cells (a 9×9 patch or a 5×5 shovel fits).
    static constexpr std::uint32_t kMaxTimedPasses = 10;
    static constexpr std::size_t kMaxLineVertices = 16384;
    static constexpr std::size_t kMaxGhostCells = 1024;

    struct GpuMesh {
        WorldPos origin;
        std::unique_ptr<HostBuffer> quads, tQuads;
        std::uint32_t quadCount = 0, tQuadCount = 0;
        // What the mesh's draws read besides its quads, kept alive with it
        // (both are replaced, never rewritten, when they grow).
        std::shared_ptr<HostBuffer> quadIndices, payloads;
    };

    struct Frame {
        vk::raii::CommandPool pool = nullptr;
        vk::raii::CommandBuffer cmd = nullptr;
        vk::raii::Semaphore imageAvailable = nullptr;
        vk::raii::Fence inFlight = nullptr;
        vk::raii::QueryPool timestamps = nullptr;  // 2 per pass; null without GPU timing
        std::vector<std::string> timedPasses;      // names of the passes last recorded here
        std::unique_ptr<HostBuffer> lines;         // debug-line vertices, rewritten each frame
        std::unique_ptr<HostBuffer> lights;        // SSBO: point lights (camera-relative), rewritten each frame
        std::unique_ptr<HostBuffer> translucentFrame;  // UBO: the translucency passes' frame constants
        vk::raii::DescriptorSet lightSet = nullptr;
        // A readback recorded into this frame: copied out after its fence.
        std::unique_ptr<HostBuffer> captureBuffer, probeBuffer;
        std::unique_ptr<HostBuffer> ghostVertices, ghostIndices;  // rebuilt each frame from the preview
        vk::Extent2D captureExtent{};
        vk::Format captureFormat = vk::Format::eUndefined;
    };

    void recreateSwapchainResources();
    void writeTargetDescriptors();
    void createPresentPipeline();
    void record(Frame& frame, std::uint32_t imageIndex, const Camera& camera, const FrameOptions& options,
                const std::vector<PointLight>& lights, const std::vector<DebugLine>& debugLines,
                const std::vector<GhostCell>& ghosts);
    std::uint32_t ghostMaterialId(BlockTypeId t) const { return static_cast<std::uint32_t>(1 + registry_.defs().size() + t); }
    void readTimestamps(Frame& frame);

    VkContext& ctx_;
    const BlockRegistry& registry_;
    HostBuffer materials_;  // SSBO: per material id (then one ghost material per type): PBR, emission, translucency
    TextureArray textures_;
    GpuMesh mesh_;
    // The static quad index buffer (0 1 2 0 2 3 per quad, offset 4 per quad),
    // grown to the largest mesh, and the appearance table, grown with the
    // state table (append-only, so old meshes read the new one correctly).
    std::shared_ptr<HostBuffer> quadIndices_, payloads_;
    std::uint32_t quadIndexCapacity_ = 0;  // quads
    std::size_t payloadCount_ = 0;
    std::vector<std::pair<std::uint64_t, GpuMesh>> retiredMeshes_;  // (frame it was replaced in, mesh)
    std::uint64_t frameNumber_ = 0;
    std::unique_ptr<GBuffer> gbuffer_;

    vk::raii::Sampler targetSampler_ = nullptr;  // nearest/clamp for G-buffer reads
    vk::raii::DescriptorSetLayout gbufferLayout_ = nullptr;
    vk::raii::DescriptorSetLayout lightingLayout_ = nullptr;
    vk::raii::DescriptorSetLayout presentLayout_ = nullptr;
    vk::raii::DescriptorSetLayout linesLayout_ = nullptr;
    vk::raii::DescriptorSetLayout lightsLayout_ = nullptr;  // set 1 of the lighting + translucent passes, per frame
    vk::raii::DescriptorSetLayout absorbanceLayout_ = nullptr;
    vk::raii::DescriptorSetLayout translucentLayout_ = nullptr;
    vk::raii::DescriptorPool descriptorPool_ = nullptr;
    vk::raii::DescriptorSet gbufferSet_ = nullptr;
    vk::raii::DescriptorSet lightingSet_ = nullptr;
    vk::raii::DescriptorSet presentSet_ = nullptr;
    vk::raii::DescriptorSet linesSet_ = nullptr;
    vk::raii::DescriptorSet absorbanceSet_ = nullptr;
    vk::raii::DescriptorSet translucentSet_ = nullptr;

    vk::raii::PipelineLayout gbufferPipelineLayout_ = nullptr;
    vk::raii::PipelineLayout lightingPipelineLayout_ = nullptr;
    vk::raii::PipelineLayout presentPipelineLayout_ = nullptr;
    vk::raii::PipelineLayout linesPipelineLayout_ = nullptr;
    vk::raii::Pipeline gbufferPipeline_ = nullptr;
    vk::raii::Pipeline lightingPipeline_ = nullptr;
    vk::raii::Pipeline presentPipeline_ = nullptr;
    vk::raii::Pipeline linesPipeline_ = nullptr;  // draws onto the swapchain too
    vk::raii::PipelineLayout absorbancePipelineLayout_ = nullptr;
    vk::raii::PipelineLayout translucentPipelineLayout_ = nullptr;
    vk::raii::Pipeline absorbancePipeline_ = nullptr;
    vk::raii::Pipeline translucentPipeline_ = nullptr;
    vk::raii::Pipeline ghostAbsorbancePipeline_ = nullptr;   // ghost.vert: MeshVertex input
    vk::raii::Pipeline ghostTranslucentPipeline_ = nullptr;
    vk::raii::PipelineLayout quadLinesPipelineLayout_ = nullptr;
    vk::raii::Pipeline quadLinesPipeline_ = nullptr;  // F3 outlines from the quads; onto the swapchain
    vk::Format presentPipelineFormat_ = vk::Format::eUndefined;

    std::array<Frame, kFramesInFlight> frames_;
    std::vector<vk::raii::Semaphore> renderFinished_;  // per swapchain image
    std::vector<ImageState> swapchainStates_;          // per swapchain image
    std::size_t frameIndex_ = 0;
    bool resized_ = false;
    std::vector<PassTime> gpuTimes_;
    bool wantCapture_ = false;
    std::optional<glm::ivec2> wantProbe_;
    std::optional<Capture> capture_;
    std::optional<glm::vec4> probe_;

    // Last member: shut down (after ~Renderer's waitIdle) before any Vulkan
    // object above is destroyed.
    std::unique_ptr<DevUi> devUi_;
};
