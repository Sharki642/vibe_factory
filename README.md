# Factory (raylib + C++20)

2D factory game: small ore veins, miners, belts, smelters and chests. Click any building to see live what it is doing.

## Requirements (Ubuntu / Zorin)

```bash
sudo apt install build-essential cmake ninja-build git \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
```

## Build & run

```bash
cmake -S . -B build -G Ninja      # fetches raylib 6.2 from GitHub the first time
cmake --build build
./build/factory
ctest --test-dir build            # tests (world logic, no window)
```

## How it works

- **Veins**: small patches of iron, copper, coal and stone. Every tile holds a limited amount of ore (more rocks drawn = more ore left).
- **Miner**: place on ore. Digs 1 ore every 2 s and pushes it out the arrow side. Stops when blocked or the tile is mined out.
- **Belt**: carries items in the arrow direction. Drag to draw a line; it orients itself.
- **Smelter**: accepts ore and coal from any side. 1 coal = 4 smelts, 2 s each.
  Iron ore -> iron plate, copper ore -> copper plate, 2 stone -> brick. Outputs on the arrow side.
- **Chest**: stores up to 1000 items.

The game starts with a running demo: iron miner -> belt -> smelter -> chest, with coal arriving on its own belt from the top.
Keep coal and ore on separate belts. If they share one, a backed-up belt can starve the smelter of fuel.

**Inspecting:** left-click any building (or an ore tile) to open the panel. It shows status (Working / No fuel / Output blocked / Ore depleted …), progress bars, buffers, chest contents and a live items-per-minute rate. The status light on each machine shows the same at a glance: green = working, yellow = waiting, red/orange = problem.

## Controls

| Key | Action |
|---|---|
| WASD / arrows / middle-drag | Move camera (Shift = faster) |
| Mouse wheel | Zoom |
| 1–4 or click hotbar | Pick building |
| Q / Esc | Back to inspect mode (Esc again closes the panel) |
| R | Rotate (the tool, or the hovered building in inspect mode) |
| Left click | Build on empty tiles / inspect existing buildings |
| Right click (hold) | Remove |
| Space | Pause |

## Structure

| File | Purpose |
|---|---|
| `src/defs.hpp` | Item, ore, building and status tables. **New item / building = one line** |
| `src/world.*` | Grid, veins, placement rules, simulation (`tick()`). No rendering, so testable |
| `src/render.*` | Drawing with visibility culling |
| `src/main.cpp` | Window, input, inspect panel, HUD, fixed 30 Hz tick |
| `tests/world_test.cpp` | Tests without a framework |
