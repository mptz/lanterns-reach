#pragma once
// Initial orientation of a placed block (world/block-orientation): ONE rule,
// three inputs, a per-type mapping — then canonicalized.
//   n  the clicked support face's outward normal;
//   f  the direction toward the player, snapped to the lattice direction
//      perpendicular to n;
//   hit point, as coordinates on the clicked face.
// Per type (local frame: +z = attach, −y = front):
//   uniform                                  nothing to choose
//   column, pointed                          +z along n
//   brick, pointed-brick, twisted-column,    +z along n, −y along f
//   oriented                                 (up along n, front toward the player)
//   corner, diagonal, tetrahedral            the feature vertex (+x,−y,+z) at the
//                                            clicked face's vertex nearest the hit
//   edge                                     the feature edge (+z/−y) at the clicked
//                                            face's edge nearest the hit
// Nearest vertex/edge is plain nearest (no dead zones; region tuning open).
// Exact ties resolve deterministically (see placement.cpp).

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "orientation.h"

namespace placement {

// The clicked face's frame (the same frames as face textures): up U = +z
// for the four side faces, +y for ±z; right R = U × n.
glm::ivec3 faceUp(glm::ivec3 n);
glm::ivec3 faceRight(glm::ivec3 n);

// f: `toPlayer` with its n component removed, snapped to the dominant axis.
// If the player is (almost) exactly along n there is no perpendicular
// direction; then the first of +x, +y, +z perpendicular to n is used.
glm::ivec3 snapPerpendicular(glm::dvec3 toPlayer, glm::ivec3 n);

// The hit point on the clicked face as (u, v) ∈ [0, 1]²: u along R, v along
// U, from the face's (−R, −U) corner.
glm::dvec2 faceCoords(glm::dvec3 hitFromFaceCenter, glm::ivec3 n);

// The placement rotation before canonicalization, and after.
orient::Rot placementRotation(orient::Symmetry s, glm::ivec3 n, glm::ivec3 f, glm::dvec2 faceUv);
inline orient::Rot placeOrientation(orient::Symmetry s, glm::ivec3 n, glm::ivec3 f, glm::dvec2 faceUv) {
    return orient::canonicalize(s, placementRotation(s, n, f, faceUv));
}

}  // namespace placement
