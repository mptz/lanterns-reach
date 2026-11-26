#pragma once
// Block orientation (world/block-orientation): the 24 proper rotations of
// the cube, their precomputed tables, and the 11 self-symmetry types.
//
// Rotation indexing: rotation r = 4·facing + spin is F(facing) · R_z(spin·90°),
// where facing is where local +z (the attach direction) ends up, in the
// order +z, −z, −y, +y, +x, −x, and F is a fixed rotation taking +z there
// (I, R_x(180°), R_x(90°), R_x(−90°), R_y(90°), R_y(−90°)). So index 0 is the
// identity, and a type whose symmetry contains R_z (column, pointed) has
// canonical indices that are multiples of 4.
//
// Face texture frames: face n has texture "up" U = +z for the four side
// faces and +y for ±z, and texture "right" R = U × n. A rotation g carries
// face n's frame onto face g·n's frame turned by k quarter turns
// counter-clockwise (seen from outside the face) — faceTurns[g][n].
//
// Everything here is pure integer math; sandbox_tests checks it.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

namespace orient {

enum Face : std::uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ };
inline constexpr int kFaceCount = 6;
inline constexpr std::array<const char*, kFaceCount> kFaceNames = {"+x", "-x", "+y", "-y", "+z", "-z"};
// Orbit representatives are the first face of each orbit in this order:
// attach, bottom, front, back, right, left.
inline constexpr std::array<Face, kFaceCount> kRepOrder = {PosZ, NegZ, NegY, PosY, PosX, NegX};

glm::ivec3 faceNormal(Face f);
std::optional<Face> faceFromName(std::string_view name);

using Rot = std::uint8_t;
inline constexpr int kRotCount = 24;
inline constexpr Rot kIdentity = 0;

using Mat3i = std::array<std::array<int, 3>, 3>;  // m[row][col]
glm::ivec3 rotateVec(const Mat3i& m, const glm::ivec3& v);
Mat3i multiply(const Mat3i& a, const Mat3i& b);  // a·b: b first, then a

struct Group {
    std::array<Mat3i, kRotCount> matrix;
    std::array<std::array<Rot, kRotCount>, kRotCount> compose;  // compose[a][b] = a·b
    std::array<Rot, kRotCount> inverse;
    std::array<std::array<Face, kFaceCount>, kRotCount> faceMap;           // face n → g·n
    std::array<std::array<std::uint8_t, kFaceCount>, kRotCount> faceTurns;  // CCW quarter turns, 0–3
};
const Group& group();

// Index of an integer rotation matrix; nullopt if it is not one of the 24.
std::optional<Rot> indexOf(const Mat3i& m);
// R_axis(degrees) for the lattice-preserving cases (face axes: multiples of
// 90°; edge axes: 180°; body diagonals: multiples of 120°).
Rot rotationAbout(glm::ivec3 axis, int degrees);

enum class Symmetry : std::uint8_t {
    Uniform, Column, Pointed, Brick, TwistedColumn, PointedBrick, Edge, Corner, Diagonal, Tetrahedral, Oriented
};
inline constexpr int kSymmetryCount = 11;

struct Orbit {
    Face rep;
    std::vector<Face> faces;            // in kRepOrder order; faces[0] == rep
    std::vector<std::uint8_t> stabilizerTurns;  // sorted, unique in-plane turns H induces on rep
};

struct SymmetryInfo {
    const char* keyword;
    std::vector<Rot> subgroup;          // H in standard position, sorted
    std::array<Rot, kRotCount> canonical;  // lowest index in the coset gH
    std::vector<Orbit> orbits;          // in kRepOrder order of their reps
    // For each face f: its orbit's representative, and the lowest-index h in
    // H with h·rep = f, as the quarter turns that carry rep's texture onto f.
    std::array<Face, kFaceCount> repOf;
    std::array<std::uint8_t, kFaceCount> repTurns;
};
const SymmetryInfo& symmetry(Symmetry s);
std::optional<Symmetry> symmetryFromKeyword(std::string_view keyword);

inline Rot canonicalize(Symmetry s, Rot g) { return symmetry(s).canonical[g]; }

}  // namespace orient
