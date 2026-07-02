# Particle Life

A GPU-accelerated particle life simulation with biologically-inspired emergent behaviour.
Thousands of particles interact through configurable force matrices, self-organise into
organisms, and exhibit specialised behaviour through typed archetypes — complete with
ecological dynamics, real-time analytics, and a day/night cycle.

Written in C++20 with Vulkan compute shaders and Dear ImGui.

---

## Features

### Ecological Simulation
- **Mitosis / Reproduction:** High-energy particles (> 1.5) can reproduce — dead particle slots are recycled by spawning near a thriving parent, inheriting its type and momentum
- **Corpse Decay:** Dead particles convert into static food corpses (type 9) where they die, returning organic matter to the ecosystem
- **Food Decomposition:** Uneaten food slowly decays over time, preventing infinite accumulation
- **Energy Metabolism:** All particles consume energy over time; feeding (proximity to attractive particles) replenishes it
- **Natural Selection:** Types that feed efficiently and reproduce outcompete others organically

### Physics Engine
- Up to ~22,500 particles simulated in real time on the GPU
- **Spatial Hash Grid:** O(n) neighbour lookups via a GPU-side grid (60-unit cells) with prefix-sum sorting
- **Brownian Motion:** Temperature-dependent thermal noise — particles jitter more during daytime, less at night
- **Fluid Viscosity:** Density-dependent Stokes drag — dense clusters experience higher resistance
- Configurable repulsion radius, interaction radius, dampening, and density limiting
- Double-buffered ping-pong position/velocity buffers for data-race-free updates

### Day/Night Cycle
- 20-minute real-time cycle with dawn, day, sunset, and night phases
- Temperature fluctuates sinusoidally (10°C–35°C), affecting Brownian motion intensity
- Sky colour transitions smoothly between day and night in the fragment shader
- Status bar shows current time, phase, and temperature (°C / °F)

### Force Matrix & Grid
- Up to 10 particle types, each pair with an independent attraction/repulsion scalar
- Interactive grid:
  - Hover + scroll: adjust force
  - Right-click: zero force
  - **Symmetry Toggle:** Enforce `Force[A,B] == Force[B,A]`
  - **Randomize:** Instantly randomize all force values
- Per-type colour pickers

### Particle Archetypes
Selectable behaviours per type:

| Archetype | Shader Flag | Effect |
|-----------|-------------|--------|
| **Default** | — | Force matrix only |
| **Repeller** | `REPEL` | Overrides force matrix — always repels all types |
| **Polar** | `POLAR` | Magnetic dipole: attraction modulated by relative dipole alignment |
| **Heavy** | `HEAVY` | Force response scaled to 0.25×; acts as a structural nucleus |
| **Catalyst** | `CATALYST` | Nearby particles lose less energy each step |
| **Membrane** | — | Force matrix preset: strong self-attraction, repels others |
| **Viral** | `VIRAL` | Converts adjacent non-viral particles to its own type |
| **Leech** | `LEECH` | Drains energy from neighbouring particles |
| **Shield** | `SHIELD` | Resistant to viral infection and energy drain |
| **Proton** | `HEAVY + POSITIVE` | Heavy particle with positive charge |
| **Electron** | `NEGATIVE` | Light particle with negative charge |
| **Food** | `FOOD` | Static energy source consumed by other particles |

### Visual Enhancements
- **Energy-based sizing:** High-energy particles grow larger, starving ones shrink
- **Food pulsing:** Food particles gently throb with a sinusoidal animation
- **Dying gray-out:** Particles below 0.3 energy desaturate toward gray
- **Velocity tails:** Moving particles show a fading directional trail revealing flow patterns
- **Organism halos:** Detected organism clusters are highlighted

### Real-Time Analytics
Click the status bar's particle/organism counter to open the **Metrics Explorer**:

- **Particles Tab:** Scrollable table of every particle (type, energy, age, organism membership)
- **Organisms Tab:** Table of detected organism clusters (size, speed, generation, kills, divisions)
- **Analytics Tab:** Rolling 300-frame graphs:
  - Population per species (colour-coded lines)
  - Total ecosystem energy
  - Average particle speed
  - Organism count over time
  - Birth/death rate estimates

### Interaction & Inspection
- **Interactive Hover:** Hover over any particle to see its type, conversion history, and age
- **Organism Inspection:** Hover popup shows organism composition and metrics
- **Simulation Control:**
  - **Time Scaling:** Dynamic speed adjustment (0.0x–10.0x)
  - **Persistence:** Save/Load configuration presets
  - **Force Grid Tools:** Symmetry toggle and instant randomization

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
bash ./build.sh
```

Or manually:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
cd .. && cp build/shaders/*.spv shaders/
```

Run from the project root:

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
| **Left click** | Spawn particle at cursor |
| **Status bar click** | Open Metrics Explorer |

**Force grid** (Particle Values section):
- Hover a cell + scroll → adjust attraction/repulsion
- Right-click a cell → zero the force

---

## Architecture

```
src/
├── main.cpp              # Window creation, main loop
├── simulation.cpp/h      # Top-level tick: input → compute → render
├── vulkan_context.cpp/h  # Vulkan instance, device, swapchain
├── compute_pipeline.cpp/h # GPU buffers, descriptor sets, dispatch
├── renderer.cpp/h        # Render pass, fullscreen quad, ImGui
├── particles.cpp/h       # CPU-side particle data & force generation
├── interface.cpp/h       # ImGui panels, analytics, hover popups
├── organism.cpp/h        # Cluster detection & organism tracking
├── types.h               # Shared constants, enums, push constants
└── serialization.h       # Config save/load

shaders/
├── compute.comp          # Physics, rendering, grid sorting (6 steps)
├── fullscreen.vert       # Fullscreen triangle vertex shader
└── fullscreen.frag       # Sky gradient + particle texture compositing
```

---

## License

MIT
