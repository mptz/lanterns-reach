#include "block_mesh.h"

#include "cube.h"

#include <cmath>

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
