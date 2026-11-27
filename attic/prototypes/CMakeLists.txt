# Standalone prototypes (Brick, GoalSource): independent of the repo's build.
#   cmake -S attic/prototypes -B build-proto -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-proto && ctest --test-dir build-proto
cmake_minimum_required(VERSION 3.22)
project(SandboxPrototypes CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Werror -ffp-contract=off)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wthread-safety)
endif()

enable_testing()

add_executable(test_brick brick/test_brick.cpp)
add_executable(test_equivalence brick/test_equivalence.cpp)
add_executable(bench_brick brick/bench_brick.cpp)
add_executable(test_goal_source goal/test_goal_source.cpp)
add_executable(test_reverse_lookup brick/test_reverse_lookup.cpp)
add_executable(bench_lookup brick/bench_lookup.cpp)
add_executable(bench_compaction brick/bench_compaction.cpp)

add_test(NAME brick_unit COMMAND test_brick)
add_test(NAME brick_equivalence COMMAND test_equivalence)
add_test(NAME brick_reverse_lookup COMMAND test_reverse_lookup)
add_test(NAME goal_source COMMAND test_goal_source)
