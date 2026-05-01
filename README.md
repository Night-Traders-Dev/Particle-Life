# Particle Life

A GPU-accelerated particle life simulation with biologically-inspired emergent behaviour.
Thousands of particles interact through configurable force matrices, self-organise into
organisms, and exhibit specialised behaviour through typed archetypes.

Written in C++20 with Vulkan compute shaders and Dear ImGui.

---

## Features

### Particle Physics
- Up to ~22,500 particles simulated in real time on the GPU
- O(n²) pairwise force calculation in a GLSL compute shader
- Toroidal world wrapping
- Configurable repulsion radius, interaction radius, dampening, and density limiting
- Double-buffered ping-pong position/velocity buffers for data-race-free updates

### Force Matrix
- Up to 10 particle types, each pair with an independent attraction/repulsion scalar
- Interactive grid: hover + scroll to adjust force, right-click to zero
- Per-type colour pickers

### Particle Archetypes
Six behaviours selectable per type:

| Archetype | Shader flag | Effect |
|-----------|-------------|--------|
| **Default** | — | Force matrix only |
| **Repeller** | `REPEL` | Overrides force matrix — always repels all types |
| **Polar** | `POLAR` | Magnetic dipole: attraction modulated by relative dipole alignment |
| **Heavy** | `HEAVY` | Force response scaled to 0.25×; acts as a structural nucleus |
| **Catalyst** | `CATALYST` | Nearby particles lose less energy each step |
| **Membrane** | — | Force matrix preset only: strong self-attraction, repels others |

---

## Requirements

### Linux (Ubuntu / Debian)

```bash
# Vulkan SDK (includes glslc)
sudo apt install libvulkan-dev vulkan-tools glslang-tools

# GLFW + GLM
sudo apt install libglfw3-dev libglm-dev

# CMake 3.20+ and a C++20 compiler
sudo apt install cmake g++
```

---

## Build

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run the binary from the build directory:

```bash
./build/particle_life
```

---

## Controls

| Input | Action |
|-------|--------|
| **F1** | Toggle settings panel |
| **F2** | Reset simulation |
| **Space** | Pause / unpause |
| **F11** | Toggle fullscreen |
| **Esc** | Quit |
| **Left drag** | Pan camera |
| **Scroll wheel** | Zoom |

**Force grid** (Particle Values section):
- Hover a cell + scroll → adjust attraction/repulsion
- Right-click a cell → zero the force

---

## License

MIT
