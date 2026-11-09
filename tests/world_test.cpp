// Minimal tests without a framework. (assert() would vanish in Release via NDEBUG, so own CHECK.)
#include <cstdio>

#include "world.hpp"

static int g_failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

static void run(World& w, int ticks) {
    for (int i = 0; i < ticks; ++i) w.tick();
}

// Small empty world (no starter area below 40x40) with all ore removed.
static World emptyWorld() {
    World w(16, 16, 1);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) w.setGround(x, y, Ground::Plain, 0);
    return w;
}

static void testPlacement() {
    World w = emptyWorld();
    w.setGround(2, 2, Ground::Iron, 10);

    CHECK(!w.place(-1, 0, BuildingId::Belt, Dir::Right));
    CHECK(!w.place(0, 16, BuildingId::Belt, Dir::Right));
    CHECK(!w.remove(16, 16));

    CHECK(!w.place(5, 5, BuildingId::Miner, Dir::Right)); // no ore
    CHECK(w.place(2, 2, BuildingId::Miner, Dir::Down));
    CHECK(w.at(2, 2).dir == Dir::Down);
    CHECK(!w.place(2, 2, BuildingId::Chest, Dir::Right)); // occupied by something else
    CHECK(w.place(2, 2, BuildingId::Miner, Dir::Left));   // same type -> re-orient
    CHECK(w.at(2, 2).dir == Dir::Left);
    CHECK(w.rotate(2, 2));
    CHECK(w.at(2, 2).dir == Dir::Up);

    CHECK(w.place(5, 5, BuildingId::Chest, Dir::Left));
    CHECK(w.at(5, 5).dir == Dir::Right); // chests are not rotatable
    CHECK(w.remove(5, 5));
    CHECK(!w.remove(5, 5));
}

static void testMinerToChestAndDepletion() {
    World w = emptyWorld();
    w.setGround(2, 2, Ground::Iron, 3);
    w.place(2, 2, BuildingId::Miner, Dir::Right);
    w.place(3, 2, BuildingId::Chest, Dir::Right);

    run(w, kMinerTicks);
    CHECK(w.at(3, 2).m.stored[static_cast<size_t>(Item::IronOre)] == 1);
    CHECK(w.at(2, 2).m.status == Status::Working);

    run(w, kMinerTicks * 5);
    CHECK(w.at(3, 2).m.storedTotal == 3);
    CHECK(w.at(2, 2).ore == 0);
    CHECK(w.at(2, 2).m.status == Status::Depleted);
    CHECK(w.at(2, 2).m.produced == 3);
}

static void testMinerBlocked() {
    World w = emptyWorld();
    w.setGround(2, 2, Ground::Coal, 100);
    w.place(2, 2, BuildingId::Miner, Dir::Right); // nothing in front
    run(w, kMinerTicks + 1);
    CHECK(w.at(2, 2).m.status == Status::Blocked);
    CHECK(w.at(2, 2).m.produced == 1);
    run(w, kMinerTicks * 3);
    CHECK(w.at(2, 2).m.produced == 1); // stops while blocked
}

static void testBeltMovesItems() {
    World w = emptyWorld();
    for (int x = 1; x <= 4; ++x) w.place(x, 3, BuildingId::Belt, Dir::Right);
    w.place(5, 3, BuildingId::Chest, Dir::Right);
    CHECK(w.insert(1, 3, Item::Stone, 0, 3));
    CHECK(!w.insert(1, 3, Item::Stone, 0, 3)); // back slot still occupied
    CHECK(!w.insert(1, 3, Item::Stone, 2, 3)); // head-on from the tile it points to
    run(w, 4 * 15 + 5);                        // 4 tiles at 15 ticks/tile
    CHECK(w.at(5, 3).m.stored[static_cast<size_t>(Item::Stone)] == 1);
}

static void testSmelterNeedsFuel() {
    World w = emptyWorld();
    w.place(5, 5, BuildingId::Smelter, Dir::Right);
    w.place(6, 5, BuildingId::Chest, Dir::Right);
    for (int i = 0; i < 3; ++i) CHECK(w.insert(5, 5, Item::IronOre, 4, 5));
    CHECK(!w.insert(5, 5, Item::CopperOre, 4, 5)); // different ore while busy
    CHECK(!w.insert(5, 5, Item::IronPlate, 4, 5)); // not smeltable

    run(w, kSmeltTicks * 2);
    CHECK(w.at(5, 5).m.status == Status::NoFuel);
    CHECK(w.at(5, 5).m.produced == 0);

    CHECK(w.insert(5, 5, Item::Coal, 5, 4));
    run(w, kSmeltTicks * 3 + 2);
    CHECK(w.at(5, 5).m.produced == 3);
    CHECK(w.at(6, 5).m.stored[static_cast<size_t>(Item::IronPlate)] == 3);
    CHECK(w.at(5, 5).m.status == Status::NoInput);
    CHECK(w.at(5, 5).m.burnLeft == kCoalBurnTicks - 3 * kSmeltTicks); // one coal = 4 smelts
}

static void testStoneNeedsTwo() {
    World w = emptyWorld();
    w.place(5, 5, BuildingId::Smelter, Dir::Right);
    w.place(6, 5, BuildingId::Chest, Dir::Right);
    w.insert(5, 5, Item::Coal, 4, 5);
    w.insert(5, 5, Item::Stone, 4, 5);
    run(w, kSmeltTicks * 2);
    CHECK(w.at(5, 5).m.produced == 0); // one stone is not enough
    w.insert(5, 5, Item::Stone, 4, 5);
    run(w, kSmeltTicks + 2);
    CHECK(w.at(6, 5).m.stored[static_cast<size_t>(Item::Brick)] == 1);
}

static void testStarterFactoryMakesPlates() {
    World w(64, 64, 42);
    run(w, kTicksPerSecond * 60); // one minute
    const auto totals = w.chestTotals();
    const uint32_t plates = totals[static_cast<size_t>(Item::IronPlate)];
    std::printf("starter factory: %u iron plates after 60 s\n", plates);
    CHECK(plates >= 20);
}

static void testDeterministic() {
    World a(64, 64, 7), b(64, 64, 7);
    bool same = true;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) same = same && a.at(x, y).ground == b.at(x, y).ground && a.at(x, y).ore == b.at(x, y).ore;
    CHECK(same);
    CHECK(rotateCW(rotateCW(rotateCW(rotateCW(Dir::Right)))) == Dir::Right);
}

int main() {
    testPlacement();
    testMinerToChestAndDepletion();
    testMinerBlocked();
    testBeltMovesItems();
    testSmelterNeedsFuel();
    testStoneNeedsTwo();
    testStarterFactoryMakesPlates();
    testDeterministic();
    if (g_failed == 0) std::printf("all tests ok\n");
    return g_failed == 0 ? 0 : 1;
}
