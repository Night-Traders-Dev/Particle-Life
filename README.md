# Particle Life

A GPU-accelerated particle life simulation with biologically-inspired emergent behaviour,
evolution, and ecosystem dynamics. Thousands of particles interact through configurable
force matrices, self-organise into organisms, speciate when their genome diverges, and
exhibit specialised behaviour through typed archetypes — complete with live weather,
predator-prey dynamics, seasonal migration, niche construction, and real-time analytics.

Written in C++20 with Vulkan compute shaders and Dear ImGui.

---

## Features

### Ecological Simulation
- **Mitosis / Reproduction:** High-energy particles (> 1.5) can reproduce — dead particle slots are recycled by spawning near a thriving parent, inheriting its type and momentum
- **Energy Inheritance:** Offspring inherit ~40% of parent energy at birth — parents pay an energy cost (0.3 for non-food, 0.1 for food)
- **Seed Dispersal:** Offspring scatter at 4–34× radius in random direction, preventing overcrowding and encouraging territory spread
- **Corpse Decay:** Dead particles convert into static food corpses (type 9) where they die, returning organic matter to the ecosystem
- **Food Decomposition:** Uneaten food slowly decays over time, preventing infinite accumulation
- **Per-type Metabolism:** Each particle type drains energy at a different rate (type 0 = 0.5×, type 8 = 2.0×), creating energy-budget specialization
- **Natural Selection:** Types that feed efficiently and reproduce outcompete others organically
- **Cross-Species Reproduction (Two-Parent Hybridization):**
  - Two particles of different types can mate with probability `cross_repro_rate` (default 15%)
  - Offspring genome is a **blend** of both parents' `self_mod`, `cross_mod`, `lifespan`, `adhesion`, `division_rate`, and `defense` using `mix()`
  - Offspring type may come from either parent, enabling true inter-species gene flow
  - Higher energy cost (0.5 from parent1, 0.25 from parent2) compared to single-parent mitosis (0.3)
  - Hybrid offspring gain a slightly wider behavior-flag mutation range
- **Horizontal Gene Transfer (HGT):**
  - Within the same organism, nearby bonded particles exchange genome traits (`self_mod`, `cross_mod`, `adhesion`, `defense`) probabilistically
  - Creates organism-level genetic cohesion independent of reproduction
- **Multicellular Spring Forces:**
  - Particles sharing the same `organism_id` are pulled toward a rest length (~3× interaction radius) proportional to `adhesion`
  - Creates stretchy membranes and cohesive multicellular clusters
- **CPU-Side Organism ID Writeback:** `OrganismManager` computes clusters on CPU; detected organism IDs are written back to the particle array and re-uploaded to GPU each detection cycle

### Predator-Prey Dynamics
- **Predator Pursuit:** PREDATOR-flagged particles actively steer toward nearby prey in addition to force-matrix interactions
- **Prey Conversion:** Predators convert nearby prey on contact, gaining energy reward (0.3)
- **Food Web:** Energy flows from food (type 9) → herbivores → predators through feeding and conversion chains
- **Co-evolution:** Parasite–host arms race tracked via infectivity and resistance scores across generations

### Speciation & Evolution
- **Speciation:** When a type's genome (self_mod/cross_mod expression) diverges 40%+ from baseline, the population can split into a new type slot — true evolution in real time
- **Heritable Traits:** self_mod, cross_mod, and lifespan mutate during reproduction (±15% on trait values)
- **Behavior Flag Mutation:** Small chance (2%) for offspring to gain/lose a random behavior flag, enabling drift adaptation
- **Generation Tracking:** Each mitosis increments the generation counter; average generation displayed in HUD

### Physics Engine
- Up to ~50,000+ particles simulated in real time on the GPU
- **Spatial Hash Grid:** O(n) neighbour lookups via a GPU-side grid (60-unit cells) with prefix-sum sorting
- **Hard-Sphere Collision:** Position correction + velocity bounce when particles overlap (prevents phasing)
- **Flocking / Schooling:** FLOCKING-flagged particles align velocity with nearby same-type neighbors (boids alignment term)
- **Per-type Radius:** Each type has an independent base radius — "whales" and "plankton" coexist visually and physically
- **Brownian Motion:** Temperature-dependent thermal noise — particles jitter more during daytime, less at night
- **Fluid Viscosity:** Density-dependent Stokes drag — dense clusters experience higher resistance
- Configurable repulsion radius, interaction radius, dampening, density limiting, and per-type radii
- Double-buffered ping-pong position/velocity buffers for data-race-free updates

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
| **Flocker** | `FLOCKING` | Aligns velocity with nearby same-type neighbours (boids) |
| **Migrator** | `MIGRATOR` | Seeks preferred temperature band (seasonal latitude movement) |

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

| Metric | Value |
|--------|-------|
| **Status** | <span style="color:green">**PASSING**</span> |
| **Last Run** | 2026-07-03 20:19:56 UTC |
| **Passed** | 66 |
| **Failed** | 0 |
| **Score** | 100% |

> Tests are run automatically on every build. Run `bash tests/test_suite` manually to re-run.
