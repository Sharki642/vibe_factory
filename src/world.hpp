#pragma once
// The world is pure data + logic. No rendering, no window -> testable.
#include <array>
#include <cstdint>
#include <vector>

#include "defs.hpp"

// Simulation runs at 30 ticks per second (see main.cpp).
constexpr int kTicksPerSecond = 30;
constexpr int kMinerTicks = 60;        // 2 s per ore
constexpr int kSmeltTicks = 60;        // 2 s per smelt
constexpr int kCoalBurnTicks = 240;    // one coal lasts 4 smelts
constexpr int kSmelterInCap = 20;
constexpr int kSmelterFuelCap = 10;
constexpr int kSmelterOutCap = 20;
constexpr int kChestCap = 1000;
constexpr int kBeltSlots = 4;
constexpr float kBeltSpacing = 1.0f / kBeltSlots;
constexpr float kBeltSpeed = 1.0f / 15.0f; // tiles per tick -> 2 tiles/s

struct BeltItem {
    Item item = Item::None;
    float pos = 0.0f; // 0 = back edge, 1 = front edge
};

// Runtime state of whatever building stands on a cell. Reset when the building is removed.
struct Machine {
    Status status = Status::Idle;
    int progress = 0;          // miner / smelter: ticks into the current cycle
    int burnLeft = 0;          // smelter: ticks left on the coal currently burning
    Item inItem = Item::None;  // smelter input
    uint16_t inCount = 0;
    uint16_t fuelCount = 0;    // smelter: coal waiting to be burned
    Item outItem = Item::None; // miner / smelter output buffer
    uint16_t outCount = 0;
    uint32_t produced = 0;     // miner: mined, smelter: smelted, chest: received

    uint8_t beltCount = 0;                   // belt: items on it, [0] is the front-most
    std::array<BeltItem, kBeltSlots> belt{};

    std::array<uint32_t, kItemCount> stored{}; // chest contents
    uint32_t storedTotal = 0;
};

struct Cell {
    Ground ground = Ground::Plain;
    uint16_t ore = 0; // ore left in this tile
    BuildingId building = BuildingId::None;
    Dir dir = Dir::Right;
    Machine m;
};

class World {
public:
    // Worlds of at least 40x40 get a starter area with a small working factory in the middle.
    World(int width, int height, uint32_t seed);

    int width() const { return w_; }
    int height() const { return h_; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    const Cell& at(int x, int y) const { return cells_[idx(x, y)]; }

    bool canPlace(int x, int y, BuildingId id) const;
    // Places on an empty tile. Same building already there -> only changes its direction (keeps its contents).
    bool place(int x, int y, BuildingId id, Dir dir);
    bool remove(int x, int y);
    bool rotate(int x, int y);

    // Hands an item to the building at (x, y), coming from (fromX, fromY). false = not accepted.
    bool insert(int x, int y, Item item, int fromX, int fromY);

    void setGround(int x, int y, Ground g, uint16_t ore);
    void tick();
    uint64_t tickCount() const { return tick_; }

    std::array<uint32_t, kItemCount> chestTotals() const;
    int starterX() const { return w_ / 2; }
    int starterY() const { return h_ / 2; }

private:
    size_t idx(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x); }
    void generateVeins(uint32_t seed);
    void buildStarterArea();
    void tickBelt(int x, int y, Cell& c);
    void tickMiner(int x, int y, Cell& c);
    void tickSmelter(int x, int y, Cell& c);
    bool pushFront(int x, int y, const Cell& c, Item item) { return insert(x + dirDX(c.dir), y + dirDY(c.dir), item, x, y); }

    int w_, h_;
    std::vector<Cell> cells_;
    std::vector<uint32_t> buildings_; // indices of cells with a building, rebuilt when dirty
    bool dirty_ = true;
    uint64_t tick_ = 0;
};
