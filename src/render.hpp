#pragma once
#include <raylib.h>

#include "world.hpp"

constexpr int kTileSize = 32; // world units: 1 tile = 32 units

// Draws only visible tiles (culled via the camera). `time` drives animations.
// (selX, selY) is the inspected tile, highlighted; pass -1 for none.
void drawWorld(const World& world, const Camera2D& cam, float time, int selX, int selY);

// Translucent preview of the building under the mouse (call inside BeginMode2D).
void drawGhost(const World& world, int tx, int ty, BuildingId id, Dir dir);

// Building icon in a rectangle (world and hotbar share it). m may be null (hotbar / ghost).
void drawBuildingIcon(BuildingId id, Dir dir, Rectangle r, float alpha = 1.0f, const Machine* m = nullptr, float time = 0.0f);

// A single item icon centered at c.
void drawItem(Item item, Vector2 c, float size);
