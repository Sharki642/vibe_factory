#pragma once
// Data-driven definitions: a new item / ore / building is one line in a table here.
#include <cstddef>
#include <cstdint>
#include <raylib.h>

// ---------- Directions (clockwise; value * 90 = angle in degrees, screen y points down) ----------
enum class Dir : uint8_t { Right, Down, Left, Up };

constexpr Dir rotateCW(Dir d) { return static_cast<Dir>((static_cast<uint8_t>(d) + 1) & 3); }
constexpr Dir opposite(Dir d) { return static_cast<Dir>((static_cast<uint8_t>(d) + 2) & 3); }
constexpr float dirAngle(Dir d) { return static_cast<float>(static_cast<uint8_t>(d)) * 90.0f; }
constexpr int dirDX(Dir d) { return d == Dir::Right ? 1 : d == Dir::Left ? -1 : 0; }
constexpr int dirDY(Dir d) { return d == Dir::Down ? 1 : d == Dir::Up ? -1 : 0; }

// ---------- Items ----------
enum class Item : uint8_t { None, IronOre, CopperOre, Coal, Stone, IronPlate, CopperPlate, Brick, Count };
constexpr size_t kItemCount = static_cast<size_t>(Item::Count);

struct ItemDef {
    const char* name;
    Color color;
    Item smeltsTo;     // None = cannot be smelted
    uint8_t smeltIn;   // how many inputs one smelt consumes
    bool isFuel;
};

inline constexpr ItemDef kItemDefs[] = {
    {"-", BLANK, Item::None, 0, false},
    {"Iron ore", {120, 155, 195, 255}, Item::IronPlate, 1, false},
    {"Copper ore", {215, 125, 70, 255}, Item::CopperPlate, 1, false},
    {"Coal", {38, 38, 44, 255}, Item::None, 0, true},
    {"Stone", {160, 152, 135, 255}, Item::Brick, 2, false},
    {"Iron plate", {205, 215, 230, 255}, Item::None, 0, false},
    {"Copper plate", {245, 165, 95, 255}, Item::None, 0, false},
    {"Brick", {180, 85, 60, 255}, Item::None, 0, false},
};

// ---------- Ground (ore veins) ----------
enum class Ground : uint8_t { Plain, Iron, Copper, Coal, Stone, Count };

struct GroundDef {
    const char* name;
    Color rock;  // color of the ore chunks drawn on the tile
    Item ore;    // what a miner gets here (None = nothing to mine)
};

inline constexpr GroundDef kGroundDefs[] = {
    {"Plain ground", {0, 0, 0, 0}, Item::None},
    {"Iron ore vein", {120, 155, 195, 255}, Item::IronOre},
    {"Copper ore vein", {215, 125, 70, 255}, Item::CopperOre},
    {"Coal vein", {22, 22, 26, 255}, Item::Coal},
    {"Stone vein", {160, 152, 135, 255}, Item::Stone},
};

// ---------- Buildings ----------
enum class BuildingId : uint8_t { None, Belt, Miner, Smelter, Chest, Count };

struct BuildingDef {
    const char* name;
    const char* help;  // shown in the hotbar tooltip
    Color color;
    bool rotatable;
    bool needsOre;     // may only stand on a tile that still has ore
};

inline constexpr BuildingDef kBuildingDefs[] = {
    {"-", "", BLANK, false, false},
    {"Belt", "Moves items in the arrow direction", {230, 195, 60, 255}, true, false},
    {"Miner", "Place on ore. Outputs in the arrow direction", {85, 160, 105, 255}, true, true},
    {"Smelter", "Ore + coal -> plates. Outputs in the arrow direction", {185, 85, 65, 255}, true, false},
    {"Chest", "Stores everything delivered to it", {150, 105, 60, 255}, false, false},
};

// ---------- Machine status (shown in the inspect panel and as a status light) ----------
enum class Status : uint8_t { Idle, Working, NoInput, NoFuel, Blocked, Depleted, Full, Count };

struct StatusDef {
    const char* name;
    Color color;
};

inline constexpr StatusDef kStatusDefs[] = {
    {"Idle", {150, 150, 160, 255}},
    {"Working", {90, 220, 110, 255}},
    {"Waiting for ore", {240, 200, 60, 255}},
    {"No fuel - needs coal", {240, 80, 70, 255}},
    {"Output blocked", {240, 140, 50, 255}},
    {"Ore depleted", {240, 80, 70, 255}},
    {"Full", {240, 80, 70, 255}},
};

constexpr const BuildingDef& defOf(BuildingId id) { return kBuildingDefs[static_cast<size_t>(id)]; }
constexpr const GroundDef& defOf(Ground g) { return kGroundDefs[static_cast<size_t>(g)]; }
constexpr const ItemDef& defOf(Item i) { return kItemDefs[static_cast<size_t>(i)]; }
constexpr const StatusDef& defOf(Status s) { return kStatusDefs[static_cast<size_t>(s)]; }

static_assert(sizeof(kBuildingDefs) / sizeof(kBuildingDefs[0]) == static_cast<size_t>(BuildingId::Count));
static_assert(sizeof(kGroundDefs) / sizeof(kGroundDefs[0]) == static_cast<size_t>(Ground::Count));
static_assert(sizeof(kItemDefs) / sizeof(kItemDefs[0]) == kItemCount);
static_assert(sizeof(kStatusDefs) / sizeof(kStatusDefs[0]) == static_cast<size_t>(Status::Count));
