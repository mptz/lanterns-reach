#pragma once
// The bitmask mesher (world/chunk-meshing, versions.md v0.2.3): the palette
// representation's way of meshing a chunk. With cell index = x + 64·y + 64²·z
// (world/brick), a row of 64 cells along x is one uint64. One pass over the
// cells builds an opaque mask per row and a mask per translucent group;
// faces come from word operations (adjacent row words along y and z, shifts
// along x), cells outside the chunk read as air, and coplanar opaque faces of
// equal appearance id are merged greedily into quads of at most 64×64.
// Translucent faces are culled the same way and merged too, per group, with
// each quad's sides uniformly framed (v0.2.5d); unmerged, they are emitted
// one quad per face in the per-cell rule's order. Quads are packed
// (packed_quad.h), chunk-local.
//
// The masks, planes and tables are a private, thread-local working set,
// allocated on a thread's first mesh and reused. StateId-specific, and
// part of chunk code: it reads the Brick it is handed directly.

#include <cstddef>
#include <cstdint>

#include "brick.h"
#include "chunk_mesh.h"

// Appends the chunk's faces to `out` (which is only appended to). `merge`
// false emits every face as its own 1×1 quad.
void bitmaskMeshPalette(const PaletteBrick<StateId>& brick, const MeshContext& context, bool merge, WorldMesh& out);
// A uniform chunk: no masks for an opaque state (six sides, each one merged
// quad), and a translucent state through the translucent pass with its group
// mask set to all ones.
void bitmaskMeshUniform(StateId state, const MeshContext& context, bool merge,
                        WorldMesh& out);

// Transposes a 64×64 bit matrix in place: afterwards bit r of a[c] is what
// bit c of a[r] was. The recursive block swap (Hacker's Delight §7-3).
void transpose64(std::uint64_t a[64]);

// Where a mesh spends its time, in milliseconds summed over meshes (a
// measurement hook: null, and free, unless a test sets it for this thread).
struct MeshTimings {
    double masks = 0, culling = 0, transposes = 0, merging = 0, emission = 0;
};
void setMeshTimings(MeshTimings* timings);
// A measurement hook: with the side constraint off (this thread only),
// translucent faces merge by key alone, to count what the constraint costs;
// such meshes' side bits are not meaningful. On by default.
void setTranslucentSideConstraint(bool on);

// The thread's working state: how many times it has grown (a test checks a
// second mesh of a chunk no bigger allocates nothing), and its size.
std::size_t meshWorkingAllocations();
std::size_t meshWorkingBytes();
