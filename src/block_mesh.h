#pragma once
// Textured block faces, shared by the world mesh (chunk_mesh.cpp, bitmask_mesh.cpp) and the
// preview ghosts (renderer.cpp), so a ghost shows exactly the faces the
// placed block will: same texture layers, same turns, same orientation.

#include <cstdint>
#include <vector>

#include "blocks.h"
#include "world_query.h"

// The preview ghosts' vertex (renderer.cpp): world meshes are packed quads
// (packed_quad.h); ghosts are a few faces rebuilt per frame and keep this
// layout. `layer` is the face's texture-array layer and `texInfo` its turn
// and set kind (shaders/block_texture.glsl). Texel positions come from the
// fragment's position, so there is no UV.
struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    std::uint32_t layer;
    std::uint32_t material;
    std::uint32_t texInfo;
};

// The texture a block in `state` shows on world face `face`: the world face
// w of a block stored with rotation r shows local face f = r⁻¹·w; its
// texture is the definition's layer for f (a set's tile 0), turned by the
// definition's frame mapping plus the rotation's own turn.
struct FaceTexture {
    std::uint32_t layer = 0;
    std::uint32_t turn = 0;  // CCW quarter turns of the image on the face
    VariantSet set = VariantSet::None;
    std::uint32_t tiles = 1;  // a random set's tile count
};
FaceTexture faceTexture(const BlockDef& def, orient::Rot orientation, orient::Face face);
FaceTexture faceTexture(const StateTable& states, StateId state, orient::Face face);

// Texture info, in appearance payloads and ghost vertices
// (shaders/block_texture.glsl): bits 0–1 turn, 2–4 set
// kind, 5–9 random tile count − 1.
std::uint32_t packTexInfo(const FaceTexture& tex);

// For faces resolved on the CPU (the preview ghosts): the
// tile a random or matched set shows on world face `face` of `cell`, by the
// same code the fragment shader runs (shaders/variants.glsl), folded into
// the layer, the set then marked plain. Other sets are returned unchanged.
FaceTexture resolvePositional(FaceTexture tex, const CellPos& cell, orient::Face face);
// The connected-set tile from face-frame frame bits (bits 0–3 = framed −R,
// +R, −U, +U, a quad's side bits at one cell; bit 4 = surface layer),
// carried into the image's frame, as the fragment shader picks it. No
// surface layer, or no framed edge: tile 0.
std::uint32_t connectedTile(std::uint32_t frameBits, std::uint32_t turn);

// Appends world face `face` of a cell whose minimum corner is at `corner`
// (mesh-relative): 4 vertices, 6 indices. `inflate` grows the quad outward
// by that many blocks (ghosts enclose the block they replace).
void appendFace(std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices, const FaceTexture& tex,
                orient::Face face, glm::vec3 corner, std::uint32_t materialId, float inflate = 0.0f);
