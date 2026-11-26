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
#include "chunk_world.h"
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

// A world with the given cells set to stone.
ChunkWorld worldWith(std::initializer_list<CellPos> solid);

void runCoordsTests();
void runChunkTests();
void runOrientationTests();
void runBlocksTests();
void runShadingTests();
void runWorldTests();
void timeChunkMesh();  // sandbox_tests --time-mesh
void runPlacementTests();
void runInputTests();
void runTranslucencyTests();
void runToolsTests();
void runScenarioTests();
