#pragma once
// The WorldQuery seam (versions.md, v0.1.x shared rules;
// core/opaque-chunk-queries). Everything that reads or edits the world —
// targeting, tools, preview, rendering, lighting — goes through this small
// semantic interface. There is deliberately NO bulk cell access: the world
// answers rays itself, meshes itself and reports its own light emitters.
// `probe` is a single-cell read for bounded, edit-local use only (pick-block,
// placement checks, a tool's small footprint) — never looped over a region.
//
// v0.2.1 implements it with one 64³ chunk (chunk_world.h).

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "blocks.h"
#include "chunk_coord.h"
#include "coords.h"

struct RayHit {
    CellPos cell;          // the solid cell hit
    glm::ivec3 normal{0};  // outward normal of the face entered; (0,0,0) if the ray started inside `cell`
    WorldPos point;        // where the ray meets that face (the origin, if it started inside)
    BlockState state;
    double distance = 0.0;
};

struct Edit {
    CellPos cell;
    BlockState state;  // BlockState::air() removes
    bool operator==(const Edit&) const = default;
};
struct EditSet {
    std::vector<Edit> edits;
    bool operator==(const EditSet&) const = default;
};

struct ApplyResult {
    std::size_t changed = 0;          // cells whose state actually changed
    std::vector<CellPos> rejected;    // edits outside the world (ignored)
};

// Mesh vertex as produced by the world (positions relative to the mesh
// origin, which the renderer rebases through coords.h like everything else;
// mesh origins lie on the integer lattice). Opaque and translucent faces use
// one layout: `layer` is the face's texture-array layer (a variant set's tile
// 0, or for a connected set the tile the mesher chose) and `texInfo` its
// turn and set kind (shaders/block_texture.glsl). Texel positions come from
// the fragment's position, so there is no UV.
struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    std::uint32_t layer;
    std::uint32_t material;
    std::uint32_t texInfo;
};

// Opaque faces go to the G-buffer; translucent faces (render/transparency)
// to a separate mesh drawn by the OIT passes. Each translucent cell emits
// its own faces, wound outward, wherever its neighbour isn't the same group
// (block type): air, opaque, another tint, or outside the world. Faces are
// quads: 4 vertices, 6 indices each, in both meshes.
struct WorldMesh {
    WorldPos origin;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshVertex> translucentVertices;
    std::vector<std::uint32_t> translucentIndices;
    std::vector<glm::vec3> edgeLines;  // pairs of endpoints: outlines of every emitted face, both meshes (F3)
};

struct Emitter {
    CellPos cell;
    glm::vec3 emissive{0.0f};  // linear: an opaque block's emissive, or a translucent block's emission
};

class WorldQuery {
public:
    virtual ~WorldQuery() = default;

    // First solid cell along the ray within maxDistance. `dir` need not be
    // normalized (distances are in units of |dir|·t, i.e. world units after
    // normalization inside).
    virtual std::optional<RayHit> raycast(const WorldPos& origin, const glm::dvec3& dir, double maxDistance) const = 0;
    // Single-cell semantic probe. Cells outside the world read as air.
    virtual BlockState probe(const CellPos& cell) const = 0;
    // Whether an edit of `cell` would be accepted (apply rejects the rest).
    // Tools use it so their planned edit sets — what preview shows — never
    // contain an edit that apply would drop. Always true for an unbounded
    // world.
    virtual bool editable(const CellPos& cell) const = 0;
    // Applies every edit that lands inside the world and rejects (reports,
    // ignores) the rest. Rejected edits change nothing, including the
    // generation.
    virtual ApplyResult apply(const EditSet& edits) = 0;

    // Meshing is chunk-granular: the chunks edited since they were last
    // meshed, and the mesh of one chunk (positions relative to its minimum
    // corner, which is the mesh origin). Building a chunk's mesh clears its
    // dirtiness. Everything outside a chunk is treated as air.
    virtual std::vector<ChunkId> dirtyChunks() const = 0;
    virtual WorldMesh buildMesh(const ChunkId& chunk) = 0;
    virtual std::vector<Emitter> emitters() const = 0;

    // Monotonic edit counter: changes whenever apply() changes a cell, so
    // derived views (mesh, lights) know when to rebuild.
    virtual std::uint64_t generation() const = 0;
};
