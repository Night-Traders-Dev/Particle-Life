# Particle Life

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Vulkan](https://img.shields.io/badge/Vulkan-1.2-green)
![License](https://img.shields.io/badge/License-MIT-yellow)

A GPU-accelerated particle life simulation with **biochemically-inspired emergent behaviour**,
evolution, and ecosystem dynamics. Thousands of particles interact through configurable
force matrices, self-organise into organisms, speciate when their genome diverges, and
exhibit specialized behaviour through typed archetypes — complete with day/night cycles,
temperature effects, and real-time analytics.

Written in C++20 with Vulkan compute shaders and Dear ImGui.

---

## Features

### Biochemical Ecosystem

Instead of a traditional predator-prey model, particles now simulate molecular and
cellular biology with 14 biochemical types:

| Type | Role | Behavior |
|------|------|----------|
| **Water (0)** | Universal solvent | Highly mobile, hydrates ions |
| **Ions (1)** | Charged particles | Form hydration shells, ionic bonds |
| **Simple (2)** | Small molecules | Dissolved nutrients/gases |
| **Lipids (3)** | Hydrophobic | Form lipid bilayers, hydrophobic exclusion |
| **Proteins (4)** | Enzymes/structure | Catalysis, binding, structural support |
| **Nucleic (5)** | DNA/RNA | Genetic information, templating |
| **Cell Mem (6)** | Membrane | Cell barriers, sticky adhesion |
| **Organelles (7)** | Cellular machinery | Active processes, signaling |
| **Electrons (8)** | Reactive radicals | Highly toxic, short-lived |
| **Nutrients (9)** | Energy source | Food for metabolic entities |
| **Protons (10)** | H+ ions | Acidic, highly reactive |
| **Cells (11)** | Living organisms | Autonomous, metabolic, signaling |
| **Dead Cells (12)** | Decomposing matter | Attracts decomposers |
| **Viruses (13)** | Infectious particles | Seek host cells |

### Cellular Life Cycle

- **Mitosis/Reproduction:** High-energy cells can reproduce, inheriting genome traits
- **Energy Budget:** Each type has different metabolic costs (water/nutrients free, organelles/cells high)
- **Corpse Decomposition:** Dead cells become decomposing matter, slowly converting to nutrients
- **Natural Selection:** Efficient metabolizers outcompete others; population dynamics emerge

### Genome & Evolution

- **Heritable Traits:** age, lifespan, self_mod, cross_mod, generation, adhesion
- **Mutation:** ±15% trait variation during reproduction
- **Speciation:** Genetic divergence creates new types organically

### Physics Engine

- Up to ~50,000+ particles simulated in real time on the GPU
- **Spatial Hash Grid:** O(n) neighbour lookups via GPU-side grid
- **Hard-Sphere Collision:** Prevents particle phasing
- **Per-type Radius:** Lipids (large), ions (small) coexist physically

### Day/Night Cycle
- **Real-time 24-hour cycle** — time of day matches your wall clock (6:00 sunrise, 8:00–18:00 day, 18:00 sunset, 20:00 night)
- Smooth sinusoidal light curve peaks at solar noon, troughs at midnight
- Temperature fluctuates sinusoidally (10°C–35°C), affecting Brownian motion intensity
- Cloud cover dims daylight by up to 35%
- Sky colour transitions smoothly between day and night in the fragment shader
- Status bar shows current time, phase, and temperature (°C / °F)

### Live Weather
- **Auto-detects your location** via IP geolocation on startup (ip-api.com, no API key), default ZIP 41101 (Ashland, KY)
- **Manual ZIP code entry** — type your ZIP in the settings panel and click "Set" to override location
- **Live conditions** fetched from Open-Meteo API every 60 seconds (free, no API key, non-blocking async):
  - Temperature affects particle metabolism and seasonal migration
  - Cloud cover dims the day/night lighting
  - Wind speed/direction applies a global directional force to all particles
  - Weather label shown in status bar (Clear / Cloudy / Foggy / Drizzle / Rain / Snow / Storm)

### Global Wind
- Real-time wind from weather data applied as a uniform directional force on all particles
- Wind strength and direction update with each weather fetch
- Particles drift downwind, adding dynamic environmental pressure to the ecosystem

### Metamorphosis
- Per-type metamorphosis age threshold: when a particle's age exceeds the threshold, it transforms into a target type
- Models life-cycle transitions: larva → adult, trophic mode switching, or developmental stages

### Kin Recognition & Altruism
- Per-type kin sharing parameter: particles share a fraction of their energy with nearby same-type neighbors
- Creates cooperative clusters and emergent multicellular-like behaviour through resource pooling

### Seasonal Migration
- MIGRATOR-flagged particles seek their preferred temperature band (latitude on the y-axis)
- Type 0 prefers cold (northern regions), type 8 prefers heat (southern regions)
- Nudging force intensifies during extreme temperatures, driving seasonal movement

### Memory Map & Chemotaxis
- Each particle carries a persistent memory of the chemical signal at its location
- Particles seek areas where signal strength is improving and explore randomly when trapped in low-signal zones
- Memory decays over time, balancing exploration vs exploitation

### Niche Construction
- Particles modify their environment over time: high-traffic corridors erode terrain, dense feeding grounds accumulate organic deposits
- Terrain changes every ~20 seconds based on particle density patterns
- Creates feedback loops: terrain shapes behaviour, behaviour shapes terrain

### Force Matrix & Grid
- 14 particle types, each pair with an independent attraction/repulsion scalar (biochemical compatibility)
- Interactive grid:
  - Hover + scroll: adjust force
  - Right-click: zero force
  - **Symmetry Toggle:** Enforce `Force[A,B] == Force[B,A]`
  - **Randomize:** Instantly randomize all force values
- Per-type colour pickers

### Particle Behaviors (Biochemical Archetypes)
Selectable behaviours per type via checkboxes:

| Behavior | Flag | Effect |
|----------|------|--------|
| **SOLUBLE** | 1 | Dissolves in aqueous environment; high mobility |
| **CHARGE** | 2 | Forms ionic bonds; charged interactions |
| **MEMBRANE** | 4 | Forms lipid bilayers; surface tension |
| **RECEPTOR** | 8 | Selective binding; lock-and-key recognition |
| **ENZYME** | 16 | Catalyzes reactions; reduces neighbor energy decay |
| **STRUCTURAL** | 32 | Forms scaffolds; low mobility |
| **SIGNALING** | 64 | Communicates chemically; emits signals |
| **METABOLIC** | 128 | Consumes nutrients; high energy drain |
| **TOXIC** | 256 | Damages other particles on contact |
| **STICKY** | 512 | Adheres to membranes/structures |
| **NUTRIENT** | 1024 | Energy source; attracts metabolic types |
| **CELL** | 2048 | Living cell; autonomous behavior |
| **DECOMPOSER** | 4096 | Breaks down dead cells |
| **VIRION** | 8192 | Infectious; seeks host cells |

Each type can have multiple behaviors combined (e.g., Proteins = RECEPTOR + ENZYME + STRUCTURAL).

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
  - Per-type generation trends
- **Ecosystem Tab:** Health metrics:
  - Simpson's Diversity Index
  - Energy flux (energy change per frame)
  - Trophic efficiency ratio
  - Per-type population percentages with generation averages

### Ecosystem Telemetry Log
- **Persistent CSV log** (`ecosystem_log.csv`) written every organism-detection frame
- Columns: time, frame, populations per type, energy, diversity, temperature, wind, births, deaths
- **Event log** (`ecosystem_events.log`) records collapses, recoveries, speciation events with timestamps
- Collapse detection: warns when any type drops below 33% of previous sample
- Press **F5** to dump a live summary to the terminal
- Logs auto-restart on simulation reset (F2) and close on exit

### Interaction & Inspection
- **Interactive Hover:** Hover over any particle to see its type, conversion history, and age
- **Organism Inspection:** Hover popup shows organism composition and metrics
- **Particle Selection:** Right-click selects nearest particle for detailed inspection (type, energy, age, organism)
- **Trait Display Mode (T):** Toggle to colour particles by self_mod value (blue=0.3, red=2.0)
- **Colour Palettes (P/B/N/M):** Cycle through viridis, plasma, magma, inferno colormaps
- **Simulation Control:**
- **Time Scaling:** Dynamic speed adjustment (0.0x–10.0x)
- **Autospawn Toggle:** Enable/disable periodic food spawning
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
| **F3** | Toggle FPS / particle HUD overlay |
| **F5** | Dump ecosystem log summary to console |
| **F11** | Toggle fullscreen |
| **F12** | Save screenshot (PPM) |
| **Space** | Pause / unpause |
| **Esc** | Quit |
| **Left drag** | Pan camera |
| **Scroll wheel** | Zoom |
| **Left click** | Spawn selected particle type at cursor |
| **Right click** | Select nearest particle for inspection |
| **T** | Toggle trait display mode (colour by self_mod) |
| **P** | Cycle colour palette forward |
| **B** | Cycle colour palette backward |
| **N** | Reset to default palette |
| **M** | Switch to magma palette |
| **Status bar click** | Open Metrics Explorer |

**Force grid** (Particle Values section):
- Hover a cell + scroll → adjust attraction/repulsion
- Right-click a cell → zero the force

---

## Architecture

```
src/
├── core/
│   ├── main.cpp          # Window creation, main loop
│   └── types.h           # Shared constants, enums, push constants
├── gpu/
│   ├── vulkan_context.cpp/h   # Vulkan instance, device, swapchain
│   ├── compute_pipeline.cpp/h # GPU buffers, descriptor sets, dispatch
│   └── renderer.cpp/h    # Render pass, fullscreen quad, ImGui
├── sim/
│   ├── simulation.cpp/h  # Top-level tick: input → compute → render
│   ├── particles.cpp/h   # CPU-side particle data & force generation
│   └── organism.cpp/h    # Cluster detection & organism tracking
└── ui/
    ├── interface.cpp/h   # ImGui panels, analytics, hover popups
    └── serialization.h   # Config save/load

shaders/
├── compute.comp          # Physics, rendering, grid sorting (6 steps)
├── fullscreen.vert       # Fullscreen triangle vertex shader
└── fullscreen.frag       # Sky gradient + particle texture compositing
```

---

## License

MIT

---

## 🧪 Test Suite

Run tests manually:

```bash
bash tests/test_suite
```
