#include "chunk_mesh.h"

#include <glm/vector_relational.hpp>

#include "bitmask_mesh.h"
#include "block_mesh.h"

namespace {

// Cells of one chunk; everything outside it is air.
struct Probe {
    const ChunkData& chunk;
    ChunkCoord coord;
    BlockState operator()(const CellPos& c) const {
        return chunkOf(c) == coord ? chunk.get(localOf(c)) : BlockState::air();
    }
};

// Connected-glass frame bits of translucent cell c's face toward world
// normal n (block_mesh.h connectedTile turns them into a set tile). Bit 4:
// the face carries the surface layer
// — against air, or against a different translucent type when its own type
// id is lower (so an interface pair is framed once); never against opaque.
// Bits 0–3 (−R, +R, −U, +U): the edge is a boundary unless the in-plane
// neighbour is the same type with an identical face (emitted, same surface
// state) toward n — i.e. the region's surface continues flat across it.
std::uint32_t frameBits(const BlockRegistry& registry, const Probe& probe, const CellPos& c, BlockTypeId type,
                        glm::ivec3 n) {
    auto surfaceOn = [&](const CellPos& cell) -> int {  // −1: no face; 0: face, no surface layer; 1: framed face
        const BlockState across = probe(cell + n);
        if (across.type == type) return -1;
        if (across.isAir()) return 1;
        if (!registry.def(across.type).translucent()) return 0;
        return type < across.type ? 1 : 0;
    };
    const int mine = surfaceOn(c);
    if (mine != 1) return 0;
    const glm::ivec3 up = n.z != 0 ? glm::ivec3(0, 1, 0) : glm::ivec3(0, 0, 1);
    const glm::ivec3 right(up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x);
    const glm::ivec3 sides[4] = {-right, right, -up, up};
    std::uint32_t bits = 16u;
    for (int i = 0; i < 4; ++i) {
        const CellPos e = c + sides[i];
        const bool continues = probe(e).type == type && surfaceOn(e) == mine;
        if (!continues) bits |= 1u << i;
    }
    return bits;
}


// Naive culled mesh of one chunk. An opaque cell emits a face wherever its
// neighbour isn't opaque (air, translucent, or outside the chunk: everything
// outside the world is air). A translucent cell emits a face (into the
// translucent mesh) wherever its neighbour isn't the same group — block type
// — so a run of one tint has no internal faces, an interface between two
// tints is two coplanar faces, and glass against stone keeps its face (it
// closes the volume for absorbance). Face textures come from block_mesh.h
// (faceTexture); a connected set's tile is chosen here from the frame bits;
// random and matched tiles by the fragment shader (opaque) or here
// (translucent). Vertex positions are relative to the chunk's minimum corner.
} // namespace

void meshPerCell(const MeshContext& context, const ChunkData& chunk, const ChunkId& id, WorldMesh& mesh) {
    const BlockRegistry& registry = context.registry;
    mesh.origin = cellCorner(chunkOrigin(id.coord));
    if (const auto u = chunk.uniformState(); u && u->isAir()) return;  // uniform air meshes to nothing
    const Probe probe{chunk, id.coord};
    for (int z = 0; z < kChunkSize; ++z)
        for (int y = 0; y < kChunkSize; ++y)
            for (int x = 0; x < kChunkSize; ++x) {
                const glm::ivec3 local(x, y, z);
                const BlockState st = chunk.get(local);
                if (st.isAir()) continue;
                const CellPos c = cellAt(id.coord, local);
                const BlockDef& def = registry.def(st.type);
                const bool translucent = def.translucent();
                auto& verts = translucent ? mesh.translucentVertices : mesh.vertices;
                auto& idx = translucent ? mesh.translucentIndices : mesh.indices;
                const glm::vec3 corner(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                for (int w = 0; w < orient::kFaceCount; ++w) {
                    const auto face = static_cast<orient::Face>(w);
                    const BlockState n = probe(c + orient::faceNormal(face));
                    const bool hidden = translucent ? n.type == st.type
                                                    : !n.isAir() && !registry.def(n.type).translucent();
                    if (hidden) continue;
                    const auto base = static_cast<std::uint32_t>(verts.size());
                    // Translucent faces are drawn per cell, so a random or
                    // matched tile is chosen here; opaque faces choose theirs
                    // per fragment (merged quads, v0.2).
                    FaceTexture tex = faceTexture(registry, st, face);
                    if (translucent) tex = resolvePositional(tex, c, face);
                    // A connected set's tile comes from the frame rule
                    // (absorbing translucent faces only).
                    if (tex.set == VariantSet::Connected)
                        tex.layer += translucent && glm::any(glm::greaterThan(def.translucency->absorption, glm::vec3(0.0f)))
                                         ? connectedTile(frameBits(registry, probe, c, st.type, orient::faceNormal(face)), tex.turn)
                                         : 0u;
                    appendFace(verts, idx, tex, face, corner, BlockRegistry::materialId(st.type));
                    for (int k = 0; k < 4; ++k) {
                        mesh.edgeLines.push_back(verts[base + static_cast<std::uint32_t>(k)].pos);
                        mesh.edgeLines.push_back(verts[base + static_cast<std::uint32_t>((k + 1) % 4)].pos);
                    }
                }
            }
}

void ChunkData::meshInto(const MeshContext& context, const ChunkId& id, Mesher mesher, WorldMesh& out) const {
    out.origin = cellCorner(chunkOrigin(id.coord));
    if (mesher == Mesher::PerCell) {
        meshPerCell(context, *this, id, out);
        return;
    }
    const bool merge = mesher == Mesher::Greedy;
    if (const auto uniform = uniformState()) {
        bitmaskMeshUniform(*uniform, context, id, merge, out);  // air emits nothing and touches no working state
        return;
    }
    bitmaskMeshPalette(palette(), context, id, merge, out);
}
