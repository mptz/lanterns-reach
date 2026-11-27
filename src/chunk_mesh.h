#pragma once
// Meshing a chunk (world/chunk-meshing). Three meshers share one face rule: a
// cell emits a face wherever its neighbour doesn't cover it, with everything
// outside the chunk treated as air (isolated meshing).
//   - the per-cell rule, `meshPerCell`: the reference, reading cells through
//     the chunk's interface, so it works over any representation;
//   - the bitmask mesher (bitmask_mesh.h), run by each representation, which
//     culls with word operations and merges coplanar faces of equal
//     appearance.
// `ChunkData::meshInto` picks one.

#include "appearance.h"
#include "blocks.h"
#include "chunk.h"
#include "world_query.h"

// What meshing needs besides cells: the block definitions and the faces'
// appearance ids.
struct MeshContext {
    const BlockRegistry& registry;
    const AppearanceTable& appearance;
};

// Appends the per-cell rule's faces for the chunk to `out`, in cell order.
void meshPerCell(const MeshContext& context, const ChunkData& chunk, const ChunkId& id, WorldMesh& out);
