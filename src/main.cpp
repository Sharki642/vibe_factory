#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "render.hpp"
#include "world.hpp"

namespace {

constexpr int kWorldW = 256;
constexpr int kWorldH = 256;
constexpr double kTickDt = 1.0 / kTicksPerSecond; // fixed simulation rate, independent of FPS

constexpr std::array<BuildingId, 4> kHotbar{BuildingId::Belt, BuildingId::Miner, BuildingId::Smelter, BuildingId::Chest};
constexpr float kSlot = 56.0f;
constexpr float kGap = 6.0f;
constexpr float kPanelW = 320.0f;

const Color kPanelBg{18, 20, 26, 235};
const Color kPanelEdge{70, 75, 90, 255};
const Color kTextDim{150, 155, 170, 255};

// Samples a building's "produced" counter so the panel can show items per minute.
struct RateTracker {
    int x = -1, y = -1;
    BuildingId b = BuildingId::None;
    std::deque<std::pair<uint64_t, uint32_t>> samples; // (tick, counter)

    void update(const World& w, int sx, int sy) {
        const BuildingId nb = w.inBounds(sx, sy) ? w.at(sx, sy).building : BuildingId::None;
        if (sx != x || sy != y || nb != b) {
            x = sx, y = sy, b = nb;
            samples.clear();
        }
        if (b == BuildingId::None || w.tickCount() % 15 != 0) return;
        samples.emplace_back(w.tickCount(), w.at(x, y).m.produced);
        while (samples.size() > 121) samples.pop_front(); // last 60 s
    }
    // < 0 means "not enough data yet"
    float perMinute() const {
        if (samples.size() < 11) return -1.0f; // need 5 s of data
        const float secs = static_cast<float>(samples.back().first - samples.front().first) / kTicksPerSecond;
        return static_cast<float>(samples.back().second - samples.front().second) / secs * 60.0f;
    }
};

struct Game {
    World world{kWorldW, kWorldH, 1337};
    Camera2D cam{};
    BuildingId tool = BuildingId::None; // None = inspect cursor
    Dir dir = Dir::Right;
    int selX = -1, selY = -1; // inspected tile
    bool dragging = false;
    int lastX = -1, lastY = -1;
    bool lastPlaced = false;
    bool paused = false;
    RateTracker rate;
};

// ---------------------------------------------------------------- inspect panel layout

enum class RowKind { Title, Sub, Text, Bar, ItemLine, Hint, Gap };
struct Row {
    RowKind kind;
    std::string label, value;
    Color color = WHITE;
    float frac = 0.0f;
    Item item = Item::None;
};

float rowHeight(RowKind k) {
    switch (k) {
    case RowKind::Title: return 30;
    case RowKind::Sub: return 22;
    case RowKind::Text: return 22;
    case RowKind::Bar: return 34;
    case RowKind::ItemLine: return 24;
    case RowKind::Hint: return 18;
    case RowKind::Gap: return 8;
    }
    return 20;
}

std::string rateText(const Game& g) {
    const float r = g.rate.perMinute();
    return r < 0 ? "measuring..." : TextFormat("%.1f / min", r);
}

std::vector<Row> panelRows(const Game& g) {
    std::vector<Row> rows;
    const World& w = g.world;
    if (!w.inBounds(g.selX, g.selY)) return rows;
    const Cell& c = w.at(g.selX, g.selY);
    const Machine& m = c.m;
    auto text = [&](std::string l, std::string v, Color col = WHITE) { rows.push_back({RowKind::Text, std::move(l), std::move(v), col}); };
    auto bar = [&](std::string l, float f, std::string v, Color col) { rows.push_back({RowKind::Bar, std::move(l), std::move(v), col, f}); };
    auto itemLine = [&](Item it, std::string v) { rows.push_back({RowKind::ItemLine, defOf(it).name, std::move(v), WHITE, 0, it}); };
    auto hint = [&](std::string h) { rows.push_back({RowKind::Hint, std::move(h), ""}); };
    auto gap = [&] { rows.push_back({RowKind::Gap, "", ""}); };

    if (c.building == BuildingId::None) {
        rows.push_back({RowKind::Title, defOf(c.ground).name, ""});
        rows.push_back({RowKind::Sub, TextFormat("Tile %d, %d", g.selX, g.selY), ""});
        if (defOf(c.ground).ore != Item::None) {
            text("Ore left", TextFormat("%d", c.ore), c.ore > 0 ? WHITE : Color{240, 80, 70, 255});
            gap();
            hint(c.ore > 0 ? "Place a Miner (2) here to dig it out." : "This tile is mined out.");
        } else {
            gap();
            hint("Nothing to mine here.");
        }
        return rows;
    }

    rows.push_back({RowKind::Title, defOf(c.building).name, ""});
    rows.push_back({RowKind::Sub, TextFormat("Tile %d, %d", g.selX, g.selY), ""});
    if (c.building != BuildingId::Chest || m.status == Status::Full)
        text("Status", defOf(m.status).name, defOf(m.status).color);
    gap();

    switch (c.building) {
    case BuildingId::Miner: {
        const Item ore = defOf(c.ground).ore;
        text("Mining", ore == Item::None ? "-" : defOf(ore).name);
        text("Ore left in tile", TextFormat("%d", c.ore));
        bar("Drilling", static_cast<float>(m.progress) / kMinerTicks,
            TextFormat("%.1f / %.1f s", static_cast<float>(m.progress) / kTicksPerSecond, static_cast<float>(kMinerTicks) / kTicksPerSecond),
            {120, 230, 140, 255});
        text("Output buffer", m.outCount ? TextFormat("%d %s", m.outCount, defOf(m.outItem).name) : "empty");
        text("Total mined", TextFormat("%u", m.produced));
        text("Rate", rateText(g));
        if (m.status == Status::Blocked) { gap(); hint("Put a belt, smelter or chest in front"); hint("of the arrow."); }
        break;
    }
    case BuildingId::Smelter: {
        const Item in = m.inCount ? m.inItem : Item::None;
        if (in != Item::None) text("Recipe", TextFormat("%d %s -> %s", defOf(in).smeltIn, defOf(in).name, defOf(defOf(in).smeltsTo).name));
        else text("Recipe", "any ore -> plate", kTextDim);
        bar("Smelting", static_cast<float>(m.progress) / kSmeltTicks, TextFormat("%d%%", m.progress * 100 / kSmeltTicks), {255, 170, 60, 255});
        bar("Fuel burning", static_cast<float>(m.burnLeft) / kCoalBurnTicks,
            TextFormat("%.1f s left", static_cast<float>(m.burnLeft) / kTicksPerSecond), {240, 110, 50, 255});
        gap();
        if (in != Item::None) itemLine(in, TextFormat("%d / %d", m.inCount, kSmelterInCap));
        else text("Input", "empty", kTextDim);
        itemLine(Item::Coal, TextFormat("%d / %d", m.fuelCount, kSmelterFuelCap));
        if (m.outCount) itemLine(m.outItem, TextFormat("%d / %d  (out)", m.outCount, kSmelterOutCap));
        else text("Output", "empty", kTextDim);
        gap();
        text("Total smelted", TextFormat("%u", m.produced));
        text("Rate", rateText(g));
        if (m.status == Status::NoFuel) { gap(); hint("Bring coal by belt or a coal miner."); }
        if (m.status == Status::NoInput) { gap(); hint("Feed ore from any side."); }
        if (m.status == Status::Blocked) { gap(); hint("Output (arrow side) has nowhere to go."); }
        break;
    }
    case BuildingId::Chest: {
        bar("Filled", static_cast<float>(m.storedTotal) / kChestCap, TextFormat("%u / %d", m.storedTotal, kChestCap),
            m.status == Status::Full ? Color{240, 80, 70, 255} : Color{120, 220, 130, 255});
        gap();
        bool any = false;
        for (size_t i = 1; i < kItemCount; ++i)
            if (m.stored[i]) {
                itemLine(static_cast<Item>(i), TextFormat("%u", m.stored[i]));
                any = true;
            }
        if (!any) text("Contents", "empty", kTextDim);
        gap();
        text("Total received", TextFormat("%u", m.produced));
        text("Rate", rateText(g));
        break;
    }
    case BuildingId::Belt: {
        text("Carrying", TextFormat("%d / %d items", m.beltCount, kBeltSlots));
        for (int i = 0; i < m.beltCount; ++i) itemLine(m.belt[static_cast<size_t>(i)].item, "");
        if (m.status == Status::Blocked) { gap(); hint("Whatever is in front won't accept it."); }
        break;
    }
    default: break;
    }
    return rows;
}

Rectangle panelRect(const std::vector<Row>& rows) {
    float h = 24;
    for (const Row& r : rows) h += rowHeight(r.kind);
    return {static_cast<float>(GetScreenWidth()) - kPanelW - 12, 12, kPanelW, h};
}

Rectangle closeButton(Rectangle p) { return {p.x + p.width - 34, p.y + 10, 24, 24}; }

void drawPanel(const std::vector<Row>& rows) {
    if (rows.empty()) return;
    const Rectangle p = panelRect(rows);
    DrawRectangleRounded(p, 0.04f, 6, kPanelBg);
    DrawRectangleRoundedLinesEx(p, 0.04f, 6, 1.5f, kPanelEdge);

    const Rectangle cb = closeButton(p);
    const bool hover = CheckCollisionPointRec(GetMousePosition(), cb);
    DrawRectangleRounded(cb, 0.3f, 4, hover ? Color{190, 60, 60, 255} : Color{50, 54, 66, 255});
    DrawText("x", static_cast<int>(cb.x) + 8, static_cast<int>(cb.y) + 2, 20, WHITE);

    const int x = static_cast<int>(p.x) + 14, right = static_cast<int>(p.x + p.width) - 14;
    float y = p.y + 12;
    for (const Row& r : rows) {
        const int iy = static_cast<int>(y);
        switch (r.kind) {
        case RowKind::Title: DrawText(r.label.c_str(), x, iy, 24, WHITE); break;
        case RowKind::Sub: DrawText(r.label.c_str(), x, iy, 14, kTextDim); break;
        case RowKind::Hint: DrawText(r.label.c_str(), x, iy, 14, {200, 190, 140, 255}); break;
        case RowKind::Gap: break;
        case RowKind::Text:
            DrawText(r.label.c_str(), x, iy, 18, kTextDim);
            DrawText(r.value.c_str(), right - MeasureText(r.value.c_str(), 18), iy, 18, r.color);
            break;
        case RowKind::Bar: {
            DrawText(r.label.c_str(), x, iy, 16, kTextDim);
            DrawText(r.value.c_str(), right - MeasureText(r.value.c_str(), 16), iy, 16, WHITE);
            const Rectangle b{static_cast<float>(x), y + 19, static_cast<float>(right - x), 9};
            DrawRectangleRounded(b, 0.5f, 4, {40, 43, 52, 255});
            if (r.frac > 0.001f) DrawRectangleRounded({b.x, b.y, std::max(b.height, b.width * std::min(r.frac, 1.0f)), b.height}, 0.5f, 4, r.color);
            break;
        }
        case RowKind::ItemLine:
            drawItem(r.item, {static_cast<float>(x) + 9, y + 9}, 16);
            DrawText(r.label.c_str(), x + 26, iy, 18, WHITE);
            DrawText(r.value.c_str(), right - MeasureText(r.value.c_str(), 18), iy, 18, WHITE);
            break;
        }
        y += rowHeight(r.kind);
    }
}

// ---------------------------------------------------------------- hotbar / HUD

Rectangle hotbarSlot(size_t i) {
    const float n = static_cast<float>(kHotbar.size());
    const float total = n * kSlot + (n - 1) * kGap;
    const float x0 = (static_cast<float>(GetScreenWidth()) - total) * 0.5f;
    return {x0 + static_cast<float>(i) * (kSlot + kGap), static_cast<float>(GetScreenHeight()) - kSlot - 34.0f, kSlot, kSlot};
}

bool mouseOverHotbar() {
    const Rectangle first = hotbarSlot(0), last = hotbarSlot(kHotbar.size() - 1);
    const Rectangle all{first.x - 8, first.y - 8, last.x + last.width - first.x + 16, kSlot + 16};
    return CheckCollisionPointRec(GetMousePosition(), all);
}

void drawHotbar(const Game& g, float time) {
    const BuildingDef* hover = nullptr;
    for (size_t i = 0; i < kHotbar.size(); ++i) {
        const Rectangle r = hotbarSlot(i);
        const bool sel = kHotbar[i] == g.tool;
        DrawRectangleRounded(r, 0.12f, 4, {20, 22, 28, 230});
        drawBuildingIcon(kHotbar[i], g.dir, {r.x + 6, r.y + 6, r.width - 12, r.height - 12}, 1.0f, nullptr, time);
        DrawRectangleRoundedLinesEx(r, 0.12f, 4, sel ? 3.0f : 1.0f, sel ? Color{255, 230, 90, 255} : Color{110, 110, 125, 255});
        DrawText(TextFormat("%d", static_cast<int>(i) + 1), static_cast<int>(r.x) + 5, static_cast<int>(r.y) + 3, 14, WHITE);
        if (CheckCollisionPointRec(GetMousePosition(), r)) hover = &defOf(kHotbar[i]);
    }
    const Rectangle first = hotbarSlot(0);
    if (hover) {
        DrawText(hover->name, static_cast<int>(first.x), static_cast<int>(first.y) - 48, 20, WHITE);
        DrawText(hover->help, static_cast<int>(first.x), static_cast<int>(first.y) - 26, 16, LIGHTGRAY);
    } else {
        const char* mode = g.tool == BuildingId::None ? "Inspect mode - click anything" : TextFormat("Building: %s", defOf(g.tool).name);
        DrawText(mode, static_cast<int>(first.x), static_cast<int>(first.y) - 26, 16, LIGHTGRAY);
    }
}

void drawHud(const Game& g) {
    // Totals across all chests
    const auto totals = g.world.chestTotals();
    int lines = 0;
    for (size_t i = 1; i < kItemCount; ++i) lines += totals[i] ? 1 : 0;
    const float h = 64.0f + static_cast<float>(std::max(lines, 1)) * 22.0f;
    DrawRectangleRounded({10, 10, 230, h}, 0.08f, 6, kPanelBg);
    DrawRectangleRoundedLinesEx({10, 10, 230, h}, 0.08f, 6, 1.5f, kPanelEdge);
    const unsigned long long secs = g.world.tickCount() / kTicksPerSecond;
    DrawText(TextFormat("Time %llu:%02llu   %d FPS", secs / 60, secs % 60, GetFPS()), 22, 20, 16, kTextDim);
    if (g.paused) DrawText("PAUSED", 170, 20, 16, {255, 200, 80, 255});
    DrawText("Stored in chests", 22, 44, 18, WHITE);
    int y = 70;
    if (lines == 0) DrawText("nothing yet", 22, y, 18, kTextDim);
    for (size_t i = 1; i < kItemCount; ++i) {
        if (!totals[i]) continue;
        drawItem(static_cast<Item>(i), {31, static_cast<float>(y) + 9}, 16);
        DrawText(defOf(static_cast<Item>(i)).name, 48, y, 18, WHITE);
        const char* n = TextFormat("%u", totals[i]);
        DrawText(n, 228 - MeasureText(n, 18), y, 18, WHITE);
        y += 22;
    }

    DrawText("WASD/MMB: move   Wheel: zoom   1-4: build   Q/Esc: inspect   R: rotate   LMB: build/inspect   RMB: remove   Space: pause",
             10, GetScreenHeight() - 22, 14, GRAY);
}

// ---------------------------------------------------------------- input

void updateCamera(Game& g, float dt, bool overUi) {
    Camera2D& cam = g.cam;
    float mx = 0, my = 0;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) my -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) my += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) mx -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) mx += 1;
    const float speed = 700.0f * (IsKeyDown(KEY_LEFT_SHIFT) ? 2.5f : 1.0f);
    cam.target.x += mx * speed * dt / cam.zoom;
    cam.target.y += my * speed * dt / cam.zoom;

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        const Vector2 d = GetMouseDelta();
        cam.target.x -= d.x / cam.zoom;
        cam.target.y -= d.y / cam.zoom;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && !overUi) {
        const Vector2 mouse = GetMousePosition();
        const Vector2 worldPos = GetScreenToWorld2D(mouse, cam);
        cam.offset = mouse;
        cam.target = worldPos;
        cam.zoom = std::clamp(cam.zoom * std::pow(1.15f, wheel), 0.15f, 4.0f);
    }
    cam.target.x = std::clamp(cam.target.x, 0.0f, static_cast<float>(kWorldW * kTileSize));
    cam.target.y = std::clamp(cam.target.y, 0.0f, static_cast<float>(kWorldH * kTileSize));
}

void mouseTile(const Camera2D& cam, int& tx, int& ty) {
    const Vector2 w = GetScreenToWorld2D(GetMousePosition(), cam);
    tx = static_cast<int>(std::floor(w.x / kTileSize));
    ty = static_cast<int>(std::floor(w.y / kTileSize));
}

bool mouseOverPanel(const Game& g) {
    const auto rows = panelRows(g);
    return !rows.empty() && CheckCollisionPointRec(GetMousePosition(), panelRect(rows));
}

void handleInput(Game& g) {
    for (size_t i = 0; i < kHotbar.size(); ++i)
        if (IsKeyPressed(KEY_ONE + static_cast<int>(i))) g.tool = kHotbar[i];
    if (IsKeyPressed(KEY_Q)) g.tool = BuildingId::None;
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (g.tool != BuildingId::None) g.tool = BuildingId::None;
        else g.selX = g.selY = -1;
    }
    if (IsKeyPressed(KEY_SPACE)) g.paused = !g.paused;

    int tx, ty;
    mouseTile(g.cam, tx, ty);
    if (IsKeyPressed(KEY_R)) {
        if (g.tool != BuildingId::None) g.dir = rotateCW(g.dir);
        else g.world.rotate(tx, ty);
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g.dragging = false;

    // UI clicks
    if (mouseOverHotbar() || mouseOverPanel(g)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (size_t i = 0; i < kHotbar.size(); ++i)
                if (CheckCollisionPointRec(GetMousePosition(), hotbarSlot(i))) g.tool = g.tool == kHotbar[i] ? BuildingId::None : kHotbar[i];
            const auto rows = panelRows(g);
            if (!rows.empty() && CheckCollisionPointRec(GetMousePosition(), closeButton(panelRect(rows)))) g.selX = g.selY = -1;
        }
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g.world.inBounds(tx, ty)) {
        if (g.world.at(tx, ty).building != BuildingId::None || g.tool == BuildingId::None) {
            g.selX = tx, g.selY = ty; // inspect
        } else {
            g.lastPlaced = g.world.place(tx, ty, g.tool, g.dir);
            g.dragging = true;
            g.lastX = tx, g.lastY = ty;
        }
    } else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && g.dragging && (tx != g.lastX || ty != g.lastY)) {
        // Dragging belts: point each belt toward the next one, so a drawn line just works
        const int dx = tx - g.lastX, dy = ty - g.lastY;
        if (g.tool == BuildingId::Belt && std::abs(dx) + std::abs(dy) == 1) {
            g.dir = dx == 1 ? Dir::Right : dx == -1 ? Dir::Left : dy == 1 ? Dir::Down : Dir::Up;
            if (g.lastPlaced) g.world.place(g.lastX, g.lastY, BuildingId::Belt, g.dir);
        }
        g.lastPlaced = g.world.at(tx, ty).building == BuildingId::None && g.world.place(tx, ty, g.tool, g.dir);
        g.lastX = tx, g.lastY = ty;
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) g.world.remove(tx, ty);
}

} // namespace

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 760, "Factory");
    SetExitKey(KEY_NULL); // Esc is used in-game

    Game game;
    game.cam.zoom = 1.6f;
    game.cam.offset = {static_cast<float>(GetScreenWidth()) * 0.5f, static_cast<float>(GetScreenHeight()) * 0.5f};
    game.cam.target = {(static_cast<float>(game.world.starterX()) - 2.5f) * kTileSize, (static_cast<float>(game.world.starterY()) - 1.5f) * kTileSize};

    double acc = 0.0;
    float time = 0.0f;
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const bool overUi = mouseOverHotbar() || mouseOverPanel(game);

        updateCamera(game, dt, overUi);
        handleInput(game);

        if (!game.paused) {
            time += dt;
            // Fixed tick; the cap prevents a "spiral of death" after a lag spike
            acc += std::min(dt, 0.25f);
            while (acc >= kTickDt) {
                game.world.tick();
                game.rate.update(game.world, game.selX, game.selY);
                acc -= kTickDt;
            }
        }

        BeginDrawing();
        ClearBackground({14, 15, 20, 255});
        BeginMode2D(game.cam);
        drawWorld(game.world, game.cam, time, game.selX, game.selY);
        if (!overUi && game.tool != BuildingId::None) {
            int tx, ty;
            mouseTile(game.cam, tx, ty);
            drawGhost(game.world, tx, ty, game.tool, game.dir);
        }
        EndMode2D();
        drawHud(game);
        drawPanel(panelRows(game));
        drawHotbar(game, time);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
