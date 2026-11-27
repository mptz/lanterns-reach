#pragma once
// Shared by the test files (one per module; sandbox_tests.cpp runs them).

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <GLFW/glfw3.h>

#include "block_mesh.h"
#include "blocks.h"
#include "brick.h"
#include "camera.h"
#include "check.h"
#include "chunk.h"
#include "chunk_mesh.h"
#include "chunk_world.h"
#include "digest.h"
#include "fatal.h"
#include "coords.h"
#include "hands.h"
#include "intentions.h"
#include "orientation.h"
#include "placement.h"
#include "scenario.h"
#include "tools.h"
#include "translucency.h"

// The shipped block definitions, loaded once.
const BlockRegistry& registry();
BlockState solidState();               // stone, identity orientation
BlockTypeId typeOf(const char* name);  // a shipped block type by name
bool near(double a, double b);         // |a − b| < 1e-9

inline const glm::ivec3 kNormals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

// A tool target: the first hit of the ray from `eye` toward `at`.
std::optional<ToolTarget> aim(const WorldQuery& w, WorldPos eye, WorldPos at);

// Test states: 0 is air, 1…validStateCount()−1 the registry's distinct
// canonical states (every type times its canonical orientations), and beyond
// that synthetic states (types past the registry), which are fine for
// storage, probes, raycasts and digests but can't be meshed.
BlockState testState(std::uint32_t k);
std::uint32_t validStateCount();

using Cells = std::vector<BlockState>;  // one per cell, in brickCellIndex order
glm::ivec3 localOfIndex(std::uint32_t cell);
std::uint64_t oracleDigest(const Cells& cells);
// Content with exactly n distinct states (n ≤ 2¹⁸), spatially structured (z
// bands of a few states plus scattered others, air above a ground line when
// air is among them). `pool` receives the test-state numbers used.
Cells makeContent(std::uint32_t n, std::mt19937_64& rng, std::vector<std::uint32_t>& pool);
// n distinct states, each at least twice: cell c holds testState(base + c % n).
Cells distinctContent(std::uint32_t n, std::uint32_t base);
// Sets every cell of `data` that differs from air by edits (promotion and
// growth on the way); finish=true then ends it like a bulk fill (compaction).
void fillChunk(ChunkData& data, const Cells& cells, bool finish);

// A world with the given cells set to stone.
ChunkWorld worldWith(std::initializer_list<CellPos> solid);

void runCoordsTests();
void runChunkTests();
void runOrientationTests();
void runBlocksTests();
void runShadingTests();
void runWorldTests();
void runDigestTests();
void runRepresentationTests();
void runEquivalenceTests(bool longRun);
void timeChunkMesh();             // sandbox_tests --time-mesh
void timeChunkRepresentations();  // sandbox_tests --time-chunk
void runPlacementTests();
void runInputTests();
void runTranslucencyTests();
void runToolsTests();
void runScenarioTests();
