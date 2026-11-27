# Building and running

How to build, test and run the sandbox. Per-release choices and results are in
`RELEASES.md`; what each release is for is in `versions.md`.

## Requirements

- **Clang** is the only supported compiler (`deps/toolchain`), with the
  platform's standard library (libstdc++ on Linux). `CMakeLists.txt` defaults
  to `clang`/`clang++` and fails configuration for any other compiler. The
  compiler comes from the OS package; it isn't pinned (Debian 13 has 19.1.7).
- **CMake** 3.21 or newer, **Ninja** (any generator works), **Python 3**
  (tool checks and `tools/`).
- **Vulkan 1.3** loader and headers, and `glslc` (shaders compile to SPIR-V at
  build time, so no display is needed to build).
- **Network access** on the first configure: GLFW, GLM, VMA, vk-bootstrap,
  stb, Dear ImGui and nlohmann/json are fetched by `FetchContent`, each pinned
  to a commit in `CMakeLists.txt`.
- Running needs a **GPU with a window system**, or Mesa's lavapipe under Xvfb
  for the headless checks.

### Debian 13 (trixie) packages

```
sudo apt install \
  clang cmake ninja-build git python3 \
  libvulkan-dev vulkan-validationlayers vulkan-tools glslc \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libx11-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libwayland-bin
```

For the headless checks (`tools/headless_check.sh`) add `xvfb` and
`mesa-vulkan-drivers` (lavapipe). A real GPU needs its own Vulkan driver
package as usual.

This is the set installed on the machine the project is developed on, and the
build works there. It hasn't been pruned to a minimum. The earlier Ubuntu 24.04
list is in `attic/build-notes-v0.1.md`. Debian 13's Vulkan headers are 1.4.x,
newer than the 1.3.275 that the vk-bootstrap pin was chosen for; it builds
regardless.

The Debug build requires the Khronos validation layer
(`vulkan-validationlayers`): without it the binary prints `validation: off` and
exits non-zero.

## Build and test

Out-of-source trees are `build-debug/` and `build-release/` (both gitignored):

```
cmake -S . -B build-debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-debug
ctest --test-dir build-debug            # 8 tests; Release has 7
```

The build uses `-Werror` with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
and Clang's `-Wthread-safety`, and must stay warning-free. `ctest` is
headless. A build directory configured with another compiler can't be reused:
make a fresh one (or `CC=clang CXX=clang++`).

## Run

```
build-debug/sandbox                                   # the v0.1 world, interactive
build-debug/sandbox --scenario scenarios/headless/lit.json --capture lit.png
build-release/sandbox --scenario scenarios/perf-1080p.json --gpu-report
build-debug/sandbox --scenario scenarios/v0.1-scene.json --check-absorbance
```

A run is a scenario file (`scenarios/README.md` has the schema, flags and
shipped scenarios). Esc quits; Tab toggles mouse capture.

## Headless checks

```
tools/headless_check.sh build-debug [out-dir]   # about 3 minutes
```

Runs every scenario under `scenarios/headless/` on Mesa lavapipe inside Xvfb,
screenshotting each, plus the order-invariance, determinism, merged-quad and
Beer–Lambert comparisons. Every run must exit 0 (no validation errors). It
sets `VK_DRIVER_FILES` to lavapipe's ICD and forces
`SANDBOX_GLFW_PLATFORM=x11`, since GLFW prefers Wayland when a compositor is
reachable. To compare captures between builds, use
`python3 tools/imgdiff.py A.png B.png [--max N]`.

## Measurements

```
build-release/sandbox_tests --time-mesh   # time to mesh a full 64³ chunk
```
