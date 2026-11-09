#include "world.hpp"

#include <algorithm>
#include <random>

World::World(int width, int height, uint32_t seed)
    : w_(width), h_(height), cells_(static_cast<size_t>(width) * static_cast<size_t>(height)) {
    generateVeins(seed);
    if (w_ >= 40 && h_ >= 40) buildStarterArea();
}

// ---------------------------------------------------------------- world generation

void World::generateVeins(uint32_t seed) {
    // Many small, irregular veins: grow each one from a seed tile by a short random walk.
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> px(0, w_ - 1), py(0, h_ - 1), size(4, 11), pick(0, 99), amount(180, 420);
    const int veins = (w_ * h_) / 220;

    for (int i = 0; i < veins; ++i) {
        const int r = pick(rng);
        const Ground g = r < 35 ? Ground::Iron : r < 60 ? Ground::Copper : r < 85 ? Ground::Coal : Ground::Stone;
        std::vector<std::pair<int, int>> tiles{{px(rng), py(rng)}};
        const int n = size(rng);
        for (int guard = 0; static_cast<int>(tiles.size()) < n && guard < 64; ++guard) {
            const auto [bx, by] = tiles[static_cast<size_t>(pick(rng)) % tiles.size()];
            const Dir d = static_cast<Dir>(pick(rng) & 3);
            const std::pair<int, int> t{bx + dirDX(d), by + dirDY(d)};
            if (inBounds(t.first, t.second) && std::find(tiles.begin(), tiles.end(), t) == tiles.end()) tiles.push_back(t);
        }
        for (const auto& [x, y] : tiles)
            if (inBounds(x, y)) setGround(x, y, g, static_cast<uint16_t>(amount(rng)));
    }
}

void World::buildStarterArea() {
    const int cx = starterX(), cy = starterY();
    // Clear a calm area around the start
    for (int y = cy - 14; y <= cy + 14; ++y)
        for (int x = cx - 16; x <= cx + 16; ++x)
            if (inBounds(x, y)) setGround(x, y, Ground::Plain, 0);

    auto vein = [&](Ground g, std::initializer_list<std::pair<int, int>> offsets, uint16_t amount) {
        for (const auto& [dx, dy] : offsets) setGround(cx + dx, cy + dy, g, amount);
    };
    vein(Ground::Iron, {{-9, -2}, {-8, -2}, {-7, -2}, {-8, -3}, {-9, -3}, {-8, -1}, {-9, -1}}, 350);
    vein(Ground::Coal, {{-1, -7}, {0, -7}, {0, -6}, {1, -6}, {-1, -6}, {1, -7}}, 350);
    vein(Ground::Copper, {{3, 3}, {4, 3}, {5, 3}, {3, 4}, {4, 4}, {5, 4}, {4, 5}}, 300);
    vein(Ground::Stone, {{-6, 4}, {-5, 4}, {-5, 5}, {-6, 5}, {-4, 5}, {-5, 6}}, 300);
    vein(Ground::Coal, {{8, -5}, {9, -5}, {8, -4}, {9, -4}, {10, -4}}, 300);
    vein(Ground::Iron, {{-3, 8}, {-2, 8}, {-2, 9}, {-3, 9}, {-1, 9}}, 300);

    // Demo factory: iron miner -> belt -> smelter -> chest, with coal arriving from the top on its own belt.
    place(cx - 7, cy - 2, BuildingId::Miner, Dir::Right);
    for (int x = cx - 6; x <= cx - 1; ++x) place(x, cy - 2, BuildingId::Belt, Dir::Right);
    place(cx, cy - 6, BuildingId::Miner, Dir::Down);
    for (int y = cy - 5; y <= cy - 3; ++y) place(cx, y, BuildingId::Belt, Dir::Down);
    place(cx, cy - 2, BuildingId::Smelter, Dir::Right);
    place(cx + 1, cy - 2, BuildingId::Chest, Dir::Right);
}

void World::setGround(int x, int y, Ground g, uint16_t ore) {
    if (!inBounds(x, y)) return;
    Cell& c = cells_[idx(x, y)];
    c.ground = g;
    c.ore = defOf(g).ore == Item::None ? 0 : ore;
}

// ---------------------------------------------------------------- building

bool World::canPlace(int x, int y, BuildingId id) const {
    if (!inBounds(x, y) || id == BuildingId::None) return false;
    const Cell& c = at(x, y);
    if (c.building != BuildingId::None) return c.building == id;
    return !defOf(id).needsOre || c.ore > 0;
}

bool World::place(int x, int y, BuildingId id, Dir dir) {
    if (!canPlace(x, y, id)) return false;
    Cell& c = cells_[idx(x, y)];
    const Dir newDir = defOf(id).rotatable ? dir : Dir::Right;
    if (c.building == id) { // just re-orient, keep contents
        c.dir = newDir;
        return true;
    }
    c.building = id;
    c.dir = newDir;
    c.m = {};
    dirty_ = true;
    return true;
}

bool World::remove(int x, int y) {
    if (!inBounds(x, y)) return false;
    Cell& c = cells_[idx(x, y)];
    if (c.building == BuildingId::None) return false;
    c.building = BuildingId::None;
    c.m = {};
    dirty_ = true;
    return true;
}

bool World::rotate(int x, int y) {
    if (!inBounds(x, y)) return false;
    Cell& c = cells_[idx(x, y)];
    if (c.building == BuildingId::None || !defOf(c.building).rotatable) return false;
    c.dir = rotateCW(c.dir);
    return true;
}

// ---------------------------------------------------------------- item transfer

bool World::insert(int x, int y, Item item, int fromX, int fromY) {
    if (!inBounds(x, y) || item == Item::None) return false;
    Cell& c = cells_[idx(x, y)];
    Machine& m = c.m;

    switch (c.building) {
    case BuildingId::Belt: {
        if (x + dirDX(c.dir) == fromX && y + dirDY(c.dir) == fromY) return false; // head-on
        if (m.beltCount >= kBeltSlots) return false;
        if (m.beltCount > 0 && m.belt[m.beltCount - 1].pos < kBeltSpacing) return false; // back is occupied
        m.belt[m.beltCount++] = {item, 0.0f};
        return true;
    }
    case BuildingId::Smelter: {
        const ItemDef& d = defOf(item);
        if (d.isFuel) {
            if (m.fuelCount >= kSmelterFuelCap) return false;
            ++m.fuelCount;
            return true;
        }
        if (d.smeltsTo == Item::None) return false;
        if (m.inCount > 0 && m.inItem != item) return false;
        if (m.inCount == 0 && m.outCount > 0 && m.outItem != d.smeltsTo) return false;
        if (m.inCount >= kSmelterInCap) return false;
        m.inItem = item;
        ++m.inCount;
        return true;
    }
    case BuildingId::Chest: {
        if (m.storedTotal >= static_cast<uint32_t>(kChestCap)) {
            m.status = Status::Full;
            return false;
        }
        ++m.stored[static_cast<size_t>(item)];
        ++m.storedTotal;
        ++m.produced;
        m.status = m.storedTotal >= static_cast<uint32_t>(kChestCap) ? Status::Full : Status::Idle;
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------- simulation

void World::tick() {
    ++tick_;
    if (dirty_) {
        buildings_.clear();
        for (size_t i = 0; i < cells_.size(); ++i)
            if (cells_[i].building != BuildingId::None) buildings_.push_back(static_cast<uint32_t>(i));
        dirty_ = false;
    }
    for (const uint32_t i : buildings_) {
        Cell& c = cells_[i];
        const int x = static_cast<int>(i % static_cast<uint32_t>(w_)), y = static_cast<int>(i / static_cast<uint32_t>(w_));
        switch (c.building) {
        case BuildingId::Belt: tickBelt(x, y, c); break;
        case BuildingId::Miner: tickMiner(x, y, c); break;
        case BuildingId::Smelter: tickSmelter(x, y, c); break;
        default: break; // chests are passive
        }
    }
}

void World::tickBelt(int x, int y, Cell& c) {
    Machine& m = c.m;
    if (m.beltCount == 0) {
        m.status = Status::Idle;
        return;
    }
    for (int i = 0; i < m.beltCount; ++i) {
        const float limit = i == 0 ? 1.0f : m.belt[static_cast<size_t>(i) - 1].pos - kBeltSpacing;
        BeltItem& it = m.belt[static_cast<size_t>(i)];
        it.pos = std::max(it.pos, std::min(it.pos + kBeltSpeed, limit)); // never moves backwards
    }
    m.status = Status::Working;
    if (m.belt[0].pos >= 1.0f) {
        if (pushFront(x, y, c, m.belt[0].item)) {
            std::move(m.belt.begin() + 1, m.belt.begin() + m.beltCount, m.belt.begin());
            --m.beltCount;
        } else {
            m.status = Status::Blocked;
        }
    }
}

void World::tickMiner(int x, int y, Cell& c) {
    Machine& m = c.m;
    if (m.outCount > 0 && pushFront(x, y, c, m.outItem)) --m.outCount;
    if (m.outCount > 0) {
        m.status = Status::Blocked;
        return;
    }
    if (c.ore == 0) {
        m.status = Status::Depleted;
        m.progress = 0;
        return;
    }
    m.status = Status::Working;
    if (++m.progress >= kMinerTicks) {
        m.progress = 0;
        --c.ore;
        m.outItem = defOf(c.ground).ore;
        m.outCount = 1;
        ++m.produced;
        if (pushFront(x, y, c, m.outItem)) --m.outCount;
    }
}

void World::tickSmelter(int x, int y, Cell& c) {
    Machine& m = c.m;
    const bool pushFailed = m.outCount > 0 && !pushFront(x, y, c, m.outItem);
    if (m.outCount > 0 && !pushFailed) --m.outCount;

    const ItemDef& in = defOf(m.inItem);
    const bool hasInput = m.inCount > 0 && in.smeltsTo != Item::None && m.inCount >= in.smeltIn;
    const bool outRoom = m.outCount < kSmelterOutCap && (m.outCount == 0 || m.outItem == in.smeltsTo);

    if (!hasInput) {
        m.status = pushFailed ? Status::Blocked : Status::NoInput;
        return;
    }
    if (!outRoom) {
        m.status = Status::Blocked;
        return;
    }
    if (m.burnLeft == 0 && m.fuelCount > 0) {
        --m.fuelCount;
        m.burnLeft = kCoalBurnTicks;
    }
    if (m.burnLeft == 0) {
        m.status = Status::NoFuel;
        return;
    }
    m.status = Status::Working;
    --m.burnLeft;
    if (++m.progress >= kSmeltTicks) {
        m.progress = 0;
        m.inCount = static_cast<uint16_t>(m.inCount - in.smeltIn);
        m.outItem = in.smeltsTo;
        ++m.outCount;
        ++m.produced;
        if (m.inCount == 0) m.inItem = Item::None;
    }
}

std::array<uint32_t, kItemCount> World::chestTotals() const {
    std::array<uint32_t, kItemCount> total{};
    for (const uint32_t i : buildings_) {
        const Cell& c = cells_[i];
        if (c.building != BuildingId::Chest) continue;
        for (size_t k = 0; k < kItemCount; ++k) total[k] += c.m.stored[k];
    }
    return total;
}
