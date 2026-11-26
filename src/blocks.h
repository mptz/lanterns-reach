#pragma once
// Block definitions (v0.1.3+): JSON files under assets/blocks/, temporary
// until the grammar (data/grammar) replaces them. CPU only: no Vulkan here.
//
//   { "name": "log", "symmetry": "column",
//     "textures": { "+z": "log_end.png", "-y": { "matched": "bark.png", "edges": "ns" } },
//     "roughness": 0.8, "metallic": 0, "reflectance": 0.5, "emissive": [0, 0, 0] }
//
// A texture is a 16×16 PNG (render/block-textures; the loader rejects any
// other size), or a VARIANT SET: an atlas of 16×16 tiles, 4 per row (tile i
// at column i % 4, row i / 4; 64 wide, 16 × ⌈n / 4⌉ high), with a selection
// rule (render/block-textures, "three principles"):
//   { "random": file, "tiles": n }      one tile per face by a hash of its
//                                       cell and face; 1 ≤ n ≤ 32
//   { "matched": file, "edges": "ns" }  labels on the image's N and S edges,
//                                       tile = N | S << 1 (4 tiles)
//   { "matched": file, "edges": "nsew" } labels on all four, tile = edge bits
//                                       kEdgeN … kEdgeW (16 tiles)
//   { "connected": file }               glass frames: tile = framed edge bits,
//                                       chosen by the mesher (16 tiles)
// Matched labels are hashes of the shared edges, so neighbours agree; the
// generator makes same-label edges continuous. The loader checks structure
// only (sizes and tile counts), never how textures look.
//
// `textures` has exactly one entry per orbit of the symmetry group H on the
// faces, keyed by the orbit representative (orient::kRepOrder); keys that
// differ from the representatives are rejected. Other faces of an orbit take
// the representative's texture turned by the canonical transversal (the
// lowest-index h ∈ H carrying the representative onto them), so any texture
// is well-defined; asymmetric art is legal (world/block-orientation).
//
// `translucency` (optional): { "absorption": [r, g, b], "emission": [r, g, b] },
// linear, non-negative; absorption is σ per block of path length. A block
// with it is TRANSLUCENT: drawn by the moment-based OIT passes, not the
// G-buffer, and its group (for face culling) is its block type.
//
// Rotational invariance is not a texture property: it comes from canonical
// orientations (world/block-orientation). Material fields are optional and
// default as shown. Texture paths are relative to the definition's
// directory.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include "orientation.h"

struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;

    bool operator==(const Image&) const = default;
};

// The image as it appears turned k quarter turns counter-clockwise (square
// images only).
Image rotateCcw(const Image& img, int quarterTurns);
std::optional<Image> loadPng(const std::string& path, std::string& error);

using BlockTypeId = std::uint16_t;

// The whole block state in v0.1.x: type + canonical orientation (the lowest
// rotation index in the coset gH), so identical-looking states compare equal.
struct BlockState {
    static constexpr BlockTypeId kAirType = 0xFFFF;  // no block; never a registry index

    BlockTypeId type = kAirType;
    orient::Rot orientation = orient::kIdentity;

    static constexpr BlockState air() { return {}; }
    constexpr bool isAir() const { return type == kAirType; }
    bool operator==(const BlockState&) const = default;
};

inline constexpr std::uint32_t kTextureSize = 16;  // texels per block face edge (render/block-textures)
inline constexpr std::uint32_t kSetColumns = 4;    // tiles per atlas row
inline constexpr std::uint32_t kMaxRandomTiles = 32;  // MeshVertex::texInfo carries count − 1 in 5 bits

// Edge bits of a matched or connected tile, in the image's own frame:
// N = top row, E = right column, S = bottom row, W = left column.
inline constexpr std::uint32_t kEdgeN = 1, kEdgeE = 2, kEdgeS = 4, kEdgeW = 8;

// A face texture's selection rule (shaders/variants.glsl uses the same
// numbering in MeshVertex::texInfo).
enum class VariantSet : std::uint8_t { None = 0, Random = 1, MatchedNS = 2, MatchedNSEW = 3, Connected = 4 };
// Tiles a set holds: `random` its declared count; matched ns 4; matched nsew
// and connected 16; a single texture 1.
std::uint32_t setTileCount(VariantSet set, std::uint32_t randomTiles);

// The full mip chain of a square power-of-two sRGB texture, level 0 first
// (16 → 8 → 4 → 2 → 1): each level a 2×2 box filter of the one above,
// averaged in LINEAR light (sRGB decoded, averaged, re-encoded) with alpha
// averaged linearly, so the 1×1 level is the face's mean colour
// (render/block-textures; world/progressive-lod).
std::vector<Image> buildMipChain(const Image& level0);

// The first `count` 16×16 tiles of a set's atlas, by index.
std::vector<Image> splitSet(const Image& atlas, std::uint32_t count);

// Per-material constants (render/gbuffer-layout; glTF metallic-roughness).
struct MaterialParams {
    float roughness = 0.8f;
    float metallic = 0.0f;
    float reflectance = 0.5f;
    glm::vec3 emissive{0.0f};  // linear RGB radiance added in the G-buffer pass
};

// Translucent media (render/transparency). Absent = opaque.
struct Translucency {
    glm::vec3 absorption{0.0f};  // σ per block of path length, per channel
    glm::vec3 emission{0.0f};    // linear radiance of its front faces
};

struct TextureRef {
    orient::Face face;  // the orbit representative
    std::string file;
    VariantSet set = VariantSet::None;
    std::uint32_t tiles = 1;  // setTileCount(set, declared `tiles`)
};

struct ParsedDefinition {
    std::string name;
    orient::Symmetry symmetry = orient::Symmetry::Uniform;
    std::vector<TextureRef> textures;                            // one per orbit, in orbit order
    MaterialParams material;
    std::optional<Translucency> translucency;
};

// Structural checks: valid JSON, known fields, a symmetry keyword, texture
// keys exactly equal to the orbit representatives, set rules well formed,
// material values in range.
std::optional<ParsedDefinition> parseDefinition(std::string_view text, std::string& error);
// Image checks on the representative textures, structural only: a single
// texture is 16×16, a set's atlas is 64 wide and exactly tall enough for its
// tiles. Returns "" if OK.
std::string checkTextures(const ParsedDefinition& def,
                          const std::function<const Image*(const std::string& file)>& image);

struct BlockDef {
    std::string name;
    orient::Symmetry symmetry = orient::Symmetry::Uniform;
    MaterialParams material;
    std::optional<Translucency> translucency;
    bool translucent() const { return translucency.has_value(); }
    // Per local face (orient::Face order): texture-array layer (a set's
    // tile 0; tile i is layer + i), the CCW quarter turns that carry the
    // orbit representative's texture onto it, its selection rule and tiles.
    std::array<std::uint32_t, orient::kFaceCount> faceLayer{};
    std::array<std::uint8_t, orient::kFaceCount> faceTurns{};
    std::array<VariantSet, orient::kFaceCount> faceSet{};
    std::array<std::uint32_t, orient::kFaceCount> faceTiles{};
};

class BlockRegistry {
public:
    // Loads every *.json in dir, in filename order. Rejected definitions are
    // skipped and reported in errors().
    static BlockRegistry load(const std::string& dir);

    const std::vector<BlockDef>& defs() const { return defs_; }
    const std::vector<Image>& layers() const { return layers_; }  // all kTextureSize²
    const std::vector<std::string>& errors() const { return errors_; }

    std::optional<BlockTypeId> find(std::string_view name) const;
    const BlockDef& def(BlockTypeId t) const { return defs_[t]; }
    BlockState state(BlockTypeId t, orient::Rot placed) const {
        return {t, orient::canonicalize(defs_[t].symmetry, placed)};
    }
    // Material ids index the GPU material table; 0 means "no material".
    static std::uint16_t materialId(BlockTypeId t) { return static_cast<std::uint16_t>(t + 1); }

private:
    std::vector<BlockDef> defs_;
    std::vector<Image> layers_;
    std::vector<std::string> errors_;
};
