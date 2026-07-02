# Agent Summary

## What was done so far

### Project structure
A particle life simulation with GL compute shaders, CMake build, and Vulkan rendering.

### Changes made

**Traits & Genetics**
- Added mutable `GenomeData` with `age`, `lifespan`, `self_mod`, `cross_mod`, `generation`
- Genome buffer uploaded to GPU; used to modulate attraction forces between same-type (`self_mod`) and cross-type (`cross_mod`) particles
- Mitosis reproduces particles with inherited+trait genome, genome buffer written from shader

**Behavior flags (12 bits)**
- Added to compute shader with behavioral effects:
  - `REPEL`, `POLAR`, `HEAVY`, `CATALYST`, `VIRAL`, `LEECH`, `SHIELD`, `POSITIVE`, `NEGATIVE`, `FOOD`, `PREDATOR`, `SIGNALER`
- Shader reads behavior flags; some writable from simulation for mutation

**Terrain & Signals**
- Added `terrain_grid` (320x180) with procedural obstacle generation (circles, walls, noise)
- Added `signal_grid` (same resolution) for chemical trail emission
- Particles avoid obstacles and follow signal gradients

**Energy & Evolution**
- Energy-based reproduction: particles below 30% energy have reduced reproduction chance
- Age/lifespan: particles die when `age > lifespan`
- Mutation during mitosis: trait values (self_mod, cross_mod) ± 15%, behavior flags ±2%/1%

**Rendering**
- Age-based sizing (younger = larger)
- Multiple color palettes (viridis, plasma, magma, inferno)
- Generation number display; FPS/particle count HUD toggle (F1)
- Trait display mode (T): shows self_mod values as dot color
- Palettes selectable with P/B/N/M keys

**New file: rng.hpp** — Seedable RNG utilities for deterministic generation

### Performance optimizations applied

1. **Merged compute into render command buffer** (`renderer.cpp`, `simulation.cpp`)
   - Previously: compute submitted in a separate one-shot cmd buffer with `vkQueueWaitIdle` after each frame → GPU sat idle between compute and render
   - Now: compute dispatches recorded directly into the render frame's command buffer → GPU pipelines compute and render across frames
   - `vkQueueWaitIdle` only called on rare readback frames (every 2-10 seconds)

2. **Parallelized signal diffusion** (`compute.comp:722`)
   - Previously: single thread iterated all 57,600 chem grid cells sequentially
   - Now: full workgroup (256 threads) processes cells in parallel

3. **Buffer pre-allocation** (`compute_pipeline.cpp:148`)
   - Buffers allocated with power-of-2 capacity (min 32768) → small particle additions avoid full buffer recreate
   - Added `upload_particle_range()` to write new particles to existing GPU buffers
   - `resize_buffers` only triggered when capacity exceeded

4. **Terrain obstacle early-out** (`compute.comp:379`)
   - Obstacle count tracked in push constants; entire sampling block skipped when `count == 0`
   - Redundant CPU mortality/weather loops removed (they modified CPU arrays that never reached GPU)

5. **Hard-sphere collision detection** (`compute.comp:371`)
   - New: position correction + velocity bounce when `dist < particle_diameter`
   - Prevents particles from phasing through each other
   - Integrated into the existing neighbor loop (no extra dispatches)

### Current bugs being worked on
- Fixed: terrain/signal grid coordinate mapping used wrong world size (`1000000` instead of `params.region_size`)
- Fixed: FOV code was too aggressive in reducing lateral interactions, which let the spatial hash grid (60-unit cells) pull particles into alignment
- Fixed: behavior flag mutation range limited to bits 0-8 (was 0-10, could accidentally set FOOD flag)
