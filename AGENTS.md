# Agent Summary

## What was done so far

### Project structure
A particle life simulation with GL compute shaders, CMake build, and Vulkan rendering.

### Changes made

**Biochemical Behavior Flags (14 bits)**
- Added to types.h: `SOLUBLE`, `CHARGE`, `MEMBRANE`, `RECEPTOR`, `ENZYME`, `STRUCTURAL`, `SIGNALING`, `METABOLIC`, `TOXIC`, `STICKY`, `NUTRIENT`, `CELL`, `DECOMPOSER`, `VIRION`
- Replaced old trophic flags (REPEL, POLAR, HEAVY, CATALYST, VIRAL, LEECH, SHIELD, POSITIVE, NEGATIVE, FOOD, PREDATOR, SIGNALER)
- Shader reads behavior flags; some affect mobility, energy, and interactions

**14 Particle Types with Biochemical Roles**
- TYPE_WATER (0): Universal solvent, highly mobile
- TYPE_IONS (1): Charged particles, hydrated in solution
- TYPE_SIMPLE (2): Small soluble molecules, nutrients
- TYPE_LIPIDS (3): Hydrophobic, form membranes
- TYPE_PROTEINS (4): Enzymes, structural, catalytic
- TYPE_NUCLEIC (5): DNA/RNA, templating, information
- TYPE_CELL_MEM (6): Cell membrane components
- TYPE_ORGANELLE (7): Active cellular machinery
- TYPE_ELECTRON (8): Reactive radicals
- TYPE_NUTRIENT (9): Energy source
- TYPE_PROTON (10): Acidic, reactive
- TYPE_CELL (11): Living cells
- TYPE_DEAD_CELL (12): Decomposing matter
- TYPE_VIRUS (13): Infectious particles

**Genome Data (GPU buffer)**
- Added mutable Genomic data with `age`, `lifespan`, `self_mod`, `cross_mod`, `generation`, `adhesion`
- Genome buffer uploaded to GPU; used to modulate attraction forces
- Particles reproduce with inherited+trait genome

**Terrain & Signals**
- Added `terrain_grid` (320x180) with procedural obstacle generation
- Added `signal_grid` for chemical trail emission
- Particles avoid obstacles and follow signal gradients

**Energy & Evolution System**
- Per-type energy depletion rates in simulation.cpp reset()
- Cells have high metabolic cost, water/lipids/nutrients low/no cost
- Spontaneous generation triggers only on actual death (energy ≤ 0), very rarely
- Age/lifespan: particles die when `age > lifespan`, become TYPE_DEAD_CELL

**Rendering Updates**
- Biochemical color palette for all 14 types
- NUTRIENT particles pulse in size for visual effect
- Trait display mode shows self_mod values

**Ecosystem Logging**
- Fixed CSV header to include all 14 population columns

## Current Status
- Build compiles successfully
- Simulation runs with biochemical behavior
- Spontaneous generation reduced to prevent teleporting artifacts