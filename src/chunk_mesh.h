#pragma once
// Meshing a chunk (world/chunk-meshing). Three meshers share one face rule: a
// cell emits a face wherever its neighbour doesn't cover it, with everything
// outside the chunk treated as air (isolated meshing).
//   - the per-cell rule, `meshPerCell`: the reference, reading cells through
//     the chunk's interface, so it works over any representation;
//   - the bitmask mesher (bitmask_mesh.h), run by each representation, which
//     culls with word operations and merges coplanar faces of equal
//     appearance.
// `Chunk::meshInto` picks one.

#include "state_table.h"
#include "blocks.h"
#include "chunk.h"
#include "world_query.h"

// What meshing needs besides cells: the block definitions and the state
// table (visibility classes, appearance ids).
struct MeshContext {
    const BlockRegistry& registry;
    const StateTable& states;
};

// Whether a face carries connected-glass frames (render/block-textures): a
// translucent block that absorbs, showing a connected set on that face.
// Only such faces have side bits, and only their merging is constrained.
bool framedFace(const StateTable& states, StateId state, orient::Face face);

// Appends the per-cell rule's faces for the chunk to `out`, in cell order.
void meshPerCell(const MeshContext& context, const Chunk& chunk, const ChunkId& id, WorldMesh& out);
