cmake_minimum_required(VERSION 3.21)
# deps/toolchain: Clang is the only supported compiler. It is the default
# here; a caller's CC/CXX or -DCMAKE_CXX_COMPILER must also be Clang.
if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
endif()
# The one version label: the window title and Vulkan app name come from
# PROJECT_VERSION (SANDBOX_VERSION below).
project(sandbox VERSION 0.2.3 LANGUAGES C CXX)

# The sandbox (versions.md): the engine, its CPU-only core and the tests.
# Compiling needs no display; running needs a GPU + window system.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "Clang is required (deps/toolchain); found ${CMAKE_CXX_COMPILER_ID}. "
                      "Use a fresh build directory with CC=clang CXX=clang++.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

find_package(Vulkan 1.3 REQUIRED)
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)

# ---------------------------------------------------------------------------
# Third-party pins (FetchContent). Bump deliberately; record breakage in
# RELEASES.md.
#   GLFW          3.5.1
#   GLM           1.0.3
#   VMA           v3.5.0
#   vk-bootstrap  v1.3.275  (must match system Vulkan headers 1.3.275; see attic/build-notes-v0.1.md)
#   stb           003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5 (stb_image only)
#   Dear ImGui    v1.93.0  (core + imgui_impl_glfw + imgui_impl_vulkan)
#   nlohmann/json v3.12.0  (v0.1.3 block definitions; temporary until the grammar)
#   BLAKE3        1.8.8    (state hashes, deps/hashing; portable C, no assembly)
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG d9d6f0f1f967807ffade6598ea9a631ebaf37a56  # 3.5.1
  SYSTEM)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 8d1fd52e5ab5590e2c81768ace50c72bae28f2ed  # 1.0.3
  SYSTEM)

FetchContent_Declare(vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG 0f1a757da579831283413295cc7f3b47eb521c77  # v3.5.0
  SYSTEM)

set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vk_bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
  GIT_TAG 382259e32b23e332c23829db82493bcdf1cbbe03  # v1.3.275
  SYSTEM)

FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5  # master snapshot
  SYSTEM)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG c1aaac65e896a45f81f47a4d68f9959e0f86c859  # v1.93.0
  SYSTEM)

set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 55f93686c01528224f448c19128836e7df245f72  # v3.12.0
  SYSTEM)

FetchContent_Declare(blake3
  GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
  GIT_TAG 282d06646f0f47d90091e3413d0be2ba5bee3b1e  # 1.8.8
  SOURCE_SUBDIR no-cmake  # populate only: we build its portable C sources below
  SYSTEM)

FetchContent_MakeAvailable(glfw glm vma vk_bootstrap stb imgui json blake3)  # stb, imgui: no CMakeLists, populate only

# ---------------------------------------------------------------------------
# Shaders: GLSL -> SPIR-V offline, emitted as C initializer lists
# (glslc -mfmt=num) and #included, so the binary carries its own SPIR-V.
# ---------------------------------------------------------------------------
set(SHADER_SOURCES
  shaders/gbuffer.vert
  shaders/gbuffer.frag
  shaders/fullscreen.vert
  shaders/lighting.frag
  shaders/present.frag
  shaders/lines.vert
  shaders/lines.frag
  shaders/translucent.vert
  shaders/absorbance.frag
  shaders/translucent.frag)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
set(SHADER_OUTPUTS)
foreach(src ${SHADER_SOURCES})
  get_filename_component(name ${src} NAME)
  set(out ${SHADER_OUT_DIR}/${name}.spv.inc)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${GLSLC} --target-env=vulkan1.3 -Werror -O -mfmt=num
            -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.glsl ${CMAKE_CURRENT_SOURCE_DIR}/shaders/oit.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/translucent_common.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/material.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/variants.glsl ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block_texture.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/translucent_surface.glsl ${CMAKE_CURRENT_SOURCE_DIR}/shaders/heat.glsl
    COMMENT "glslc ${name}"
    VERBATIM)
  list(APPEND SHADER_OUTPUTS ${out})
endforeach()
add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})

# ---------------------------------------------------------------------------
# Third-party implementation TU (warnings off; not our code).
# ---------------------------------------------------------------------------
add_library(thirdparty_impl STATIC src/thirdparty_impl.cpp)
target_link_libraries(thirdparty_impl PUBLIC Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
add_library(stb_impl STATIC src/stb_impl.cpp)
target_include_directories(stb_impl SYSTEM PUBLIC ${stb_SOURCE_DIR})

# BLAKE3 (deps/hashing): the portable C implementation only. Hashing is not on
# a hot path, so the SIMD variants and their assembly are compiled out.
add_library(blake3 STATIC
  ${blake3_SOURCE_DIR}/c/blake3.c
  ${blake3_SOURCE_DIR}/c/blake3_dispatch.c
  ${blake3_SOURCE_DIR}/c/blake3_portable.c)
target_include_directories(blake3 SYSTEM PUBLIC ${blake3_SOURCE_DIR}/c)
target_compile_definitions(blake3 PRIVATE BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2 BLAKE3_NO_AVX512 BLAKE3_USE_NEON=0)

# Dear ImGui (developer UI, render/dev-ui): core + GLFW/Vulkan backends.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS: compiles out ImGui's default
# "open in shell" handler (fork + execvp("xdg-open")), the only
# process-spawning path in the third-party code we build (flagged in the
# v0.1.5 dependency review, attic/build-notes-v0.1.md). We have no link widgets.
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
                                        GLFW_INCLUDE_NONE)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

# ---------------------------------------------------------------------------
# The v0.1.x harness.
# ---------------------------------------------------------------------------
# deps/toolchain: no fused multiply-add contraction, so float results don't depend on the target.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)
# Frame pointers in every build, so `perf record --call-graph fp` gives call
# graphs with no system setting (versions.md v0.2.4).
add_compile_options(-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer)
set(SANDBOX_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-missing-field-initializers -Werror)
# deps/toolchain: Clang, libstdc++, thread-safety analysis on (the annotations arrive with the concurrency work).
list(APPEND SANDBOX_WARNINGS -Wthread-safety)

# CPU-only core (no Vulkan): orientation group, block definitions, the
# WorldQuery chunk, placement rule, hands/hotbars, the intention layer and
# the free-fly camera.
# Shared by the engine and sandbox_tests.
add_library(sandbox_core STATIC
  src/orientation.cpp
  src/blocks.cpp
  src/cube.cpp
  src/chunk.cpp
  src/appearance.cpp
  src/id128.cpp
  src/bitmask_mesh.cpp
  src/chunk_mesh.cpp
  src/chunk_world.cpp
  src/placement.cpp
  src/hands.cpp
  src/intentions.cpp
  src/bindings.cpp
  src/translucency.cpp
  src/png_write.cpp
  src/block_mesh.cpp
  src/camera.cpp
  src/scenario.cpp
  src/tools.cpp)
target_include_directories(sandbox_core PUBLIC src)
# shaders/variants.glsl is also C++ (the mesher and ghosts pick variant-set tiles with it).
target_include_directories(sandbox_core PRIVATE shaders)
# Key codes come from GLFW's header (bindings.cpp, tests); no GLFW linking.
target_include_directories(sandbox_core SYSTEM PUBLIC $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(sandbox_core PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLFW_INCLUDE_NONE)
target_compile_options(sandbox_core PRIVATE ${SANDBOX_WARNINGS})
target_link_libraries(sandbox_core PUBLIC glm::glm nlohmann_json::nlohmann_json stb_impl blake3)

add_executable(sandbox
  src/main.cpp
  src/app.cpp
  src/app_input.cpp
  src/dev_overlays.cpp
  src/game.cpp
  src/harness_checks.cpp
  src/scene.cpp
  src/dev_ui.cpp
  src/vk_context.cpp
  src/renderer.cpp
  src/renderer_setup.cpp
  src/render_graph.cpp
  src/gbuffer.cpp
  src/texture.cpp)
add_dependencies(sandbox shaders)
target_include_directories(sandbox PRIVATE src shaders ${SHADER_OUT_DIR})  # shaders: heat.glsl is also C++
target_compile_definitions(sandbox PRIVATE
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_FORCE_RADIANS
  GLFW_INCLUDE_NONE
  VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
  SANDBOX_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
  SANDBOX_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenarios"
  SANDBOX_VERSION="v${PROJECT_VERSION}"
  SANDBOX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  SANDBOX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  SANDBOX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
  $<$<CONFIG:Debug>:SANDBOX_VALIDATION=1>)
target_compile_options(sandbox PRIVATE ${SANDBOX_WARNINGS})
target_link_libraries(sandbox PRIVATE
  Vulkan::Vulkan glfw glm::glm GPUOpen::VulkanMemoryAllocator vk-bootstrap::vk-bootstrap
  thirdparty_impl imgui sandbox_core)

# ---------------------------------------------------------------------------
# Tests (deps/testing): one executable with an in-house CHECK macro, plus
# Python tool checks. Pure logic only; `ctest --test-dir build` runs
# headlessly.
# ---------------------------------------------------------------------------
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(sandbox_tests
  tests/sandbox_tests.cpp
  tests/test_support.cpp
  tests/tests_coords.cpp
  tests/tests_identity.cpp
  tests/tests_chunk.cpp
  tests/tests_digest.cpp
  tests/tests_representation.cpp
  tests/tests_equivalence.cpp
  tests/tests_mesh_equivalence.cpp
  tests/tests_orientation.cpp
  tests/tests_blocks.cpp
  tests/tests_shading.cpp
  tests/tests_world.cpp
  tests/tests_meshing.cpp
  tests/tests_placement.cpp
  tests/tests_input.cpp
  tests/tests_translucency.cpp
  tests/tests_tools.cpp
  tests/tests_scenario.cpp
  tests/measurements.cpp)
target_include_directories(sandbox_tests PRIVATE tests shaders)  # tests include shaders/pbr.glsl as C++
target_compile_options(sandbox_tests PRIVATE ${SANDBOX_WARNINGS})
find_package(Threads REQUIRED)
target_link_libraries(sandbox_tests PRIVATE sandbox_core Threads::Threads)
target_compile_definitions(sandbox_tests PRIVATE SANDBOX_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
                                               SANDBOX_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenarios")
# The randomized equivalence test is the long pole: its own ctest entry, so it
# runs beside the rest under `ctest -j`.
add_test(NAME sandbox_tests COMMAND sandbox_tests --skip-equivalence)
add_test(NAME sandbox_equivalence COMMAND sandbox_tests --only-equivalence)
# The mesh equivalence cases (versions.md v0.2.3 acceptance 3 and 4) one per
# entry, so `ctest -j` spreads them. A timeout on each: a hang fails, not waits.
set(MESH_EQUIVALENCE_CASES 1 2 3 4 5 6 7 8)
foreach(n ${MESH_EQUIVALENCE_CASES})
  add_test(NAME mesh_equivalence_${n} COMMAND sandbox_tests --mesh-case=${n})
  set_tests_properties(mesh_equivalence_${n} PROPERTIES TIMEOUT 600)
endforeach()
set_tests_properties(sandbox_tests sandbox_equivalence PROPERTIES TIMEOUT 600)

# Block definitions: the engine's --check-blocks is the one validator
# (v0.1.8), checking structure only. It accepts every shipped definition and
# rejects each negative fixture (tests/blocks_bad/) for the expected reason.
set(BLOCKS ${CMAKE_CURRENT_SOURCE_DIR}/assets/blocks)
set(BAD_BLOCKS ${CMAKE_CURRENT_SOURCE_DIR}/tests/blocks_bad)
set(EXPECT_FAIL ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_failure.py)
add_test(NAME engine_loads_blocks COMMAND sandbox --check-blocks --blocks ${BLOCKS})
# Negative fixtures: (name, reason).
set(BAD_FIXTURES
  "wrong_keys|do not match the orbit representatives"
  "not16|block textures are 16×16"
  "wrong_tiles|4 tiles of 16×16")
foreach(entry ${BAD_FIXTURES})
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 fixture)
  list(GET parts 1 why)
  add_test(NAME engine_rejects_${fixture}
    COMMAND ${EXPECT_FAIL} "${why}" $<TARGET_FILE:sandbox> --check-blocks --blocks ${BAD_BLOCKS}/${fixture})
endforeach()
# The texture generator creates its output directory and reproduces the
# committed assets byte for byte.
add_test(NAME gen_block_texture
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_gen_block_texture.py
          ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_block_texture.py ${BLOCKS}
          ${CMAKE_CURRENT_BINARY_DIR}/test_out/gen_block_texture)

# Debug builds request validation; with every layer disabled the binary must
# report `validation: off` and exit non-zero before it needs a display.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_test(NAME validation_missing_fails
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_validation_missing.py
            $<TARGET_FILE:sandbox> ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/one-frame.json)
endif()
