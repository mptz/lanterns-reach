#include "block_mesh.h"

#include "cube.h"

#include <cmath>
#include <optional>

namespace shader {
using namespace glm;
#include "variants.glsl"
}  // namespace shader

FaceTexture faceTexture(const BlockRegistry& registry, BlockState state, orient::Face face) {
    const orient::Group& g = orient::group();
    const BlockDef& def = registry.def(state.type);
    const orient::Face local = g.faceMap[g.inverse[state.orientation]][face];
    return {def.faceLayer[local], static_cast<std::uint32_t>((def.faceTurns[local] + g.faceTurns[state.orientation][local]) % 4),
            def.faceSet[local], def.faceTiles[local]};
}

std::uint32_t packTexInfo(const FaceTexture& tex) {
    const std::uint32_t count = tex.set == VariantSet::Random ? tex.tiles - 1u : 0u;
    return (tex.turn & 3u) | (static_cast<std::uint32_t>(tex.set) << 2u) | ((count & 31u) << 5u);
}

FaceTexture resolvePositional(FaceTexture tex, const CellPos& cell, orient::Face face) {
    if (tex.set != VariantSet::Random && tex.set != VariantSet::MatchedNS && tex.set != VariantSet::MatchedNSEW) return tex;
    // Wrapping to 32 bits matches the shader, which hashes the integer cell
    // of a mesh origin plus a small offset.
    const glm::ivec3 c(static_cast<int>(cell.x), static_cast<int>(cell.y), static_cast<int>(cell.z));
    tex.layer += shader::variantTile(static_cast<std::uint32_t>(tex.set), tex.tiles, c, orient::faceNormal(face), tex.turn);
    tex.set = VariantSet::None;
    return tex;
}

std::uint32_t connectedTile(std::uint32_t frameBits, std::uint32_t turn) {
    if ((frameBits & 16u) == 0u) return 0;
    // Face positions N (+U), E (+R), S (−U), W (−R) as bits 0–3.
    const std::uint32_t positions = ((frameBits & 8u) ? 1u : 0u) | ((frameBits & 2u) ? 2u : 0u) |
                                    ((frameBits & 4u) ? 4u : 0u) | ((frameBits & 1u) ? 8u : 0u);
    return shader::variantImageBits(positions, turn);
}

void appendFace(std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices, const FaceTexture& tex,
                orient::Face face, glm::vec3 corner, std::uint32_t materialId, float inflate) {
    const auto base = static_cast<std::uint32_t>(vertices.size());
    const float scale = 1.0f + 2.0f * inflate;
    const auto& cubeVerts = cube::vertices();
    const std::uint32_t info = packTexInfo(tex);
    for (int k = 0; k < 4; ++k) {
        const Vertex& v = cubeVerts[static_cast<std::size_t>(face * 4 + k)];
        vertices.push_back({corner + v.pos * scale + glm::vec3(0.5f), v.normal, tex.layer, materialId, info});
    }
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) indices.push_back(base + i);
}

bool mergeTopFaces(WorldMesh& mesh, const CellPos& minCell, int size) {
    // Mesh-relative corner of the region; the +z faces lie at z = minCell.z + 1.
    const glm::dvec3 lo = cellCorner(minCell) - mesh.origin;
    const glm::vec3 lo3(lo);
    std::vector<bool> drop(mesh.vertices.size() / 4, false);
    std::optional<MeshVertex> first;
    int found = 0;
    for (std::size_t q = 0; q < drop.size(); ++q) {
        const MeshVertex& v = mesh.vertices[q * 4];
        if (v.normal.z < 0.5f) continue;
        glm::vec3 c(0.0f);
        for (std::size_t k = 0; k < 4; ++k) c += mesh.vertices[q * 4 + k].pos * 0.25f;
        const glm::vec3 d = c - lo3;
        const float sz = static_cast<float>(size);
        if (std::abs(d.z - 1.0f) > 1e-4f || d.x < 0.0f || d.y < 0.0f || d.x > sz || d.y > sz) continue;
        if (first && (v.layer != first->layer || v.material != first->material || v.texInfo != first->texInfo)) return false;
        first = v;
        drop[q] = true;
        ++found;
    }
    if (found != size * size) return false;
    std::vector<MeshVertex> verts;
    std::vector<std::uint32_t> idx;
    for (std::size_t q = 0; q < drop.size(); ++q) {
        if (drop[q]) continue;
        const auto base = static_cast<std::uint32_t>(verts.size());
        for (std::size_t k = 0; k < 4; ++k) verts.push_back(mesh.vertices[q * 4 + k]);
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
    }
    // One quad, wound CCW about +z like the cube's own faces.
    const float s = static_cast<float>(size), z = lo3.z + 1.0f;
    const auto base = static_cast<std::uint32_t>(verts.size());
    for (const glm::vec2 corner : {glm::vec2(0, 0), glm::vec2(s, 0), glm::vec2(s, s), glm::vec2(0, s)})
        verts.push_back({glm::vec3(lo3.x + corner.x, lo3.y + corner.y, z), glm::vec3(0, 0, 1), first->layer,
                         first->material, first->texInfo});
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
    mesh.vertices = std::move(verts);
    mesh.indices = std::move(idx);
    return true;
}
