#pragma once
// Per-cell meshing of one chunk (world/chunk-meshing): the v0.1 rule over the
// chunk's cells, with everything outside the chunk treated as air. It reads
// the chunk through its interface only, so it gives the same mesh for the same
// content in any representation (rep-swap invariance). Greedy and bitmask
// meshing arrive in v0.2.3.

#include "blocks.h"
#include "chunk.h"
#include "world_query.h"

// Positions are relative to the chunk's minimum corner, which is the mesh
// origin. Uniform air meshes to nothing without visiting a cell.
WorldMesh meshChunkPerCell(const BlockRegistry& registry, const ChunkData& chunk, const ChunkId& id);
