#include "render.hpp"

#include <algorithm>
#include <cmath>

namespace {

Color withAlpha(Color c, float a) {
    c.a = static_cast<unsigned char>(static_cast<float>(c.a) * a);
    return c;
}

Color shade(Color c, float f) {
    auto ch = [f](unsigned char v) { return static_cast<unsigned char>(std::clamp(static_cast<float>(v) * f, 0.0f, 255.0f)); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

uint32_t hash2(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Local frame of a rotated tile: u along the facing direction, v sideways, both in tile units (-0.5..0.5).
struct Frame {
    Vector2 c, f, s;
    float t;
    Vector2 p(float u, float v) const { return {c.x + (f.x * u + s.x * v) * t, c.y + (f.y * u + s.y * v) * t}; }
};

Frame frameOf(Rectangle r, Dir d) {
    const Vector2 f{static_cast<float>(dirDX(d)), static_cast<float>(dirDY(d))};
    return {{r.x + r.width * 0.5f, r.y + r.height * 0.5f}, f, {-f.y, f.x}, r.width};
}

void drawArrow(const Frame& fr, float u, float size, Color col) {
    DrawPoly(fr.p(u, 0), 3, size * fr.t, std::atan2(fr.f.y, fr.f.x) * RAD2DEG, col);
}

void drawStatusLight(Rectangle r, const Machine& m, float alpha) {
    const Vector2 p{r.x + r.width * 0.84f, r.y + r.height * 0.16f};
    DrawCircleV(p, r.width * 0.09f, withAlpha(BLACK, alpha * 0.7f));
    DrawCircleV(p, r.width * 0.065f, withAlpha(defOf(m.status).color, alpha));
}

void drawBar(Rectangle r, float frac, Color col) {
    DrawRectangleRec(r, {0, 0, 0, 170});
    DrawRectangleRec({r.x, r.y, r.width * std::clamp(frac, 0.0f, 1.0f), r.height}, col);
}

void drawBelt(Rectangle r, Dir dir, float alpha, const Machine* m, float time) {
    const Frame fr = frameOf(r, dir);
    DrawRectangleRec(r, withAlpha({52, 52, 58, 255}, alpha));
    // side rails
    DrawLineEx(fr.p(-0.5f, -0.42f), fr.p(0.5f, -0.42f), r.width * 0.08f, withAlpha({100, 100, 110, 255}, alpha));
    DrawLineEx(fr.p(-0.5f, 0.42f), fr.p(0.5f, 0.42f), r.width * 0.08f, withAlpha({100, 100, 110, 255}, alpha));
    // moving chevrons (they stop when the belt is jammed)
    const bool moving = !m || m->status != Status::Blocked;
    const float offset = moving ? std::fmod(time * kBeltSpeed * kTicksPerSecond, 0.5f) : 0.0f;
    const Color chev = withAlpha(defOf(BuildingId::Belt).color, alpha * 0.55f);
    for (int i = 0; i < 2; ++i) {
        const float u = -0.3f + static_cast<float>(i) * 0.5f + offset;
        if (u > 0.42f) continue;
        DrawLineEx(fr.p(u - 0.12f, -0.22f), fr.p(u, 0.0f), r.width * 0.06f, chev);
        DrawLineEx(fr.p(u, 0.0f), fr.p(u - 0.12f, 0.22f), r.width * 0.06f, chev);
    }
}

void drawMiner(Rectangle r, Dir dir, float alpha, const Machine* m, float time) {
    const Frame fr = frameOf(r, dir);
    const BuildingDef& def = defOf(BuildingId::Miner);
    const float pad = r.width * 0.07f;
    const Rectangle inner{r.x + pad, r.y + pad, r.width - 2 * pad, r.height - 2 * pad};
    DrawRectangleRounded(inner, 0.2f, 4, withAlpha(def.color, alpha));
    DrawRectangleRoundedLinesEx(inner, 0.2f, 4, std::max(1.0f, r.width * 0.04f), withAlpha(shade(def.color, 0.5f), alpha));

    // drill head: spins while mining
    const bool working = m && m->status == Status::Working;
    const float spin = working ? time * 360.0f : 0.0f;
    DrawCircleV(fr.c, r.width * 0.27f, withAlpha({45, 60, 50, 255}, alpha));
    for (int i = 0; i < 3; ++i) {
        const float a = (spin + static_cast<float>(i) * 120.0f) * DEG2RAD;
        const Vector2 tip{fr.c.x + std::cos(a) * r.width * 0.22f, fr.c.y + std::sin(a) * r.width * 0.22f};
        DrawLineEx(fr.c, tip, r.width * 0.07f, withAlpha({200, 205, 190, 255}, alpha));
    }
    DrawCircleV(fr.c, r.width * 0.06f, withAlpha({200, 205, 190, 255}, alpha));
    drawArrow(fr, 0.4f, 0.1f, withAlpha(WHITE, alpha));

    if (m) {
        drawStatusLight(r, *m, alpha);
        if (working) drawBar({r.x + r.width * 0.12f, r.y + r.height * 0.86f, r.width * 0.76f, r.height * 0.06f},
                             static_cast<float>(m->progress) / kMinerTicks, {120, 230, 140, 255});
    }
}

void drawSmelter(Rectangle r, Dir dir, float alpha, const Machine* m, float time) {
    const Frame fr = frameOf(r, dir);
    const BuildingDef& def = defOf(BuildingId::Smelter);
    const float pad = r.width * 0.07f;
    const Rectangle inner{r.x + pad, r.y + pad, r.width - 2 * pad, r.height - 2 * pad};
    DrawRectangleRec(inner, withAlpha(def.color, alpha));
    // brick pattern
    const Color mortar = withAlpha(shade(def.color, 0.6f), alpha);
    for (int row = 1; row < 4; ++row) {
        const float y = inner.y + inner.height * static_cast<float>(row) / 4.0f;
        DrawLineEx({inner.x, y}, {inner.x + inner.width, y}, 1.0f, mortar);
    }
    DrawRectangleLinesEx(inner, std::max(1.0f, r.width * 0.04f), withAlpha(shade(def.color, 0.45f), alpha));

    // furnace mouth: glows and flickers while smelting
    const bool working = m && m->status == Status::Working;
    const Rectangle mouth{fr.c.x - r.width * 0.2f, fr.c.y - r.height * 0.14f, r.width * 0.4f, r.height * 0.32f};
    DrawRectangleRounded(mouth, 0.4f, 4, withAlpha({25, 18, 16, 255}, alpha));
    if (working) {
        const float flick = 0.75f + 0.25f * std::sin(time * 18.0f + fr.c.x * 0.3f);
        const Rectangle fire{mouth.x + 2, mouth.y + mouth.height * 0.35f, mouth.width - 4, mouth.height * 0.65f - 2};
        DrawRectangleRounded(fire, 0.5f, 4, withAlpha({255, static_cast<unsigned char>(120 + 80 * flick), 30, 255}, alpha * flick));
        // smoke puffs
        for (int i = 0; i < 2; ++i) {
            const float t = std::fmod(time * 0.8f + static_cast<float>(i) * 0.5f, 1.0f);
            DrawCircleV({r.x + r.width * 0.25f + t * 4.0f, r.y + r.height * 0.1f - t * r.height * 0.45f}, r.width * (0.06f + t * 0.08f),
                        withAlpha({170, 170, 175, 255}, alpha * (1.0f - t) * 0.6f));
        }
    }
    drawArrow(fr, 0.4f, 0.1f, withAlpha(WHITE, alpha));

    if (m) {
        drawStatusLight(r, *m, alpha);
        if (m->progress > 0) drawBar({r.x + r.width * 0.12f, r.y + r.height * 0.86f, r.width * 0.76f, r.height * 0.06f},
                                     static_cast<float>(m->progress) / kSmeltTicks, {255, 170, 60, 255});
    }
}

void drawChest(Rectangle r, float alpha, const Machine* m) {
    const BuildingDef& def = defOf(BuildingId::Chest);
    const float pad = r.width * 0.1f;
    const Rectangle box{r.x + pad, r.y + pad * 1.4f, r.width - 2 * pad, r.height - 2.4f * pad};
    DrawRectangleRec(box, withAlpha(def.color, alpha));
    DrawRectangleRec({box.x, box.y, box.width, box.height * 0.3f}, withAlpha(shade(def.color, 1.2f), alpha)); // lid
    DrawRectangleLinesEx(box, std::max(1.0f, r.width * 0.04f), withAlpha(shade(def.color, 0.45f), alpha));
    DrawLineEx({box.x, box.y + box.height * 0.3f}, {box.x + box.width, box.y + box.height * 0.3f}, 1.5f, withAlpha(shade(def.color, 0.45f), alpha));
    DrawRectangleRec({box.x + box.width * 0.42f, box.y + box.height * 0.2f, box.width * 0.16f, box.height * 0.22f}, withAlpha({220, 200, 90, 255}, alpha));
    if (m) {
        drawBar({box.x + 3, box.y + box.height * 0.72f, box.width - 6, box.height * 0.12f}, static_cast<float>(m->storedTotal) / kChestCap,
                m->status == Status::Full ? Color{240, 80, 70, 255} : Color{120, 220, 130, 255});
        if (m->status == Status::Full) drawStatusLight(r, *m, alpha);
    }
}

void drawGroundTile(const Cell& c, int x, int y, Rectangle r, bool detailed) {
    const uint32_t h = hash2(x, y);
    const unsigned char v = static_cast<unsigned char>(36 + (h & 7));
    DrawRectangleRec(r, {v, static_cast<unsigned char>(v + 4), static_cast<unsigned char>(v + 10), 255});
    if (c.ore == 0) return;

    const Color rock = defOf(c.ground).rock;
    if (!detailed) { // far zoom: flat tint is enough (and much cheaper)
        DrawRectangleRec(r, withAlpha(rock, 0.75f));
        return;
    }
    DrawRectangleRec(r, withAlpha(rock, 0.18f));
    // more chunks = more ore left
    const int chunks = std::clamp(1 + c.ore / 70, 1, 6);
    for (int i = 0; i < chunks; ++i) {
        const uint32_t hh = hash2(x * 7 + i, y * 13 - i);
        const Vector2 p{r.x + r.width * (0.18f + static_cast<float>(hh & 255) / 255.0f * 0.64f),
                        r.y + r.height * (0.18f + static_cast<float>((hh >> 8) & 255) / 255.0f * 0.64f)};
        const float rad = r.width * (0.09f + static_cast<float>((hh >> 16) & 15) / 15.0f * 0.06f);
        DrawPoly(p, 5, rad, static_cast<float>((hh >> 20) & 63), rock);
        DrawPolyLinesEx(p, 5, rad, static_cast<float>((hh >> 20) & 63), 1.0f, shade(rock, c.ground == Ground::Coal ? 2.4f : 0.6f));
    }
}

Rectangle tileRect(int x, int y) {
    return {static_cast<float>(x * kTileSize), static_cast<float>(y * kTileSize), static_cast<float>(kTileSize), static_cast<float>(kTileSize)};
}

} // namespace

void drawItem(Item item, Vector2 c, float size) {
    if (item == Item::None) return;
    const ItemDef& d = defOf(item);
    const bool raw = d.smeltsTo != Item::None || d.isFuel;
    const Color edge = item == Item::Coal ? Color{120, 120, 130, 255} : shade(d.color, 0.45f);
    if (raw) {
        DrawPoly(c, 6, size * 0.5f, 0, d.color);
        DrawPolyLinesEx(c, 6, size * 0.5f, 0, std::max(1.0f, size * 0.1f), edge);
    } else {
        const Rectangle r{c.x - size * 0.45f, c.y - size * 0.32f, size * 0.9f, size * 0.64f};
        DrawRectangleRec(r, d.color);
        DrawRectangleLinesEx(r, std::max(1.0f, size * 0.08f), edge);
    }
}

void drawBuildingIcon(BuildingId id, Dir dir, Rectangle r, float alpha, const Machine* m, float time) {
    switch (id) {
    case BuildingId::Belt: drawBelt(r, dir, alpha, m, time); break;
    case BuildingId::Miner: drawMiner(r, dir, alpha, m, time); break;
    case BuildingId::Smelter: drawSmelter(r, dir, alpha, m, time); break;
    case BuildingId::Chest: drawChest(r, alpha, m); break;
    default: break;
    }
}

void drawWorld(const World& world, const Camera2D& cam, float time, int selX, int selY) {
    const Vector2 tl = GetScreenToWorld2D({0, 0}, cam);
    const Vector2 br = GetScreenToWorld2D({static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, cam);
    const int x0 = std::max(0, static_cast<int>(tl.x / kTileSize) - 1);
    const int y0 = std::max(0, static_cast<int>(tl.y / kTileSize) - 1);
    const int x1 = std::min(world.width() - 1, static_cast<int>(br.x / kTileSize) + 1);
    const int y1 = std::min(world.height() - 1, static_cast<int>(br.y / kTileSize) + 1);
    const bool detailed = cam.zoom >= 0.5f;

    // pass 1: ground + buildings
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const Cell& c = world.at(x, y);
            const Rectangle r = tileRect(x, y);
            drawGroundTile(c, x, y, r, detailed);
            if (c.building != BuildingId::None) {
                if (detailed) drawBuildingIcon(c.building, c.dir, r, 1.0f, &c.m, time);
                else DrawRectangleRec(r, defOf(c.building).color);
            }
        }

    // pass 2: items on belts (on top, so they can slide across tile borders)
    if (detailed)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const Cell& c = world.at(x, y);
                if (c.building != BuildingId::Belt) continue;
                const Rectangle r = tileRect(x, y);
                const Vector2 cen{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
                for (int i = 0; i < c.m.beltCount; ++i) {
                    const BeltItem& it = c.m.belt[static_cast<size_t>(i)];
                    const float u = (it.pos - 0.5f) * kTileSize;
                    drawItem(it.item, {cen.x + static_cast<float>(dirDX(c.dir)) * u, cen.y + static_cast<float>(dirDY(c.dir)) * u}, kTileSize * 0.38f);
                }
            }

    if (cam.zoom >= 0.6f) {
        const Color grid{255, 255, 255, 10};
        for (int x = x0; x <= x1 + 1; ++x) DrawLine(x * kTileSize, y0 * kTileSize, x * kTileSize, (y1 + 1) * kTileSize, grid);
        for (int y = y0; y <= y1 + 1; ++y) DrawLine(x0 * kTileSize, y * kTileSize, (x1 + 1) * kTileSize, y * kTileSize, grid);
    }

    if (world.inBounds(selX, selY)) {
        const float pulse = 0.6f + 0.4f * std::sin(time * 6.0f);
        const Rectangle r = tileRect(selX, selY);
        DrawRectangleLinesEx({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 3.0f / std::min(cam.zoom, 1.0f), withAlpha({255, 230, 90, 255}, pulse));
    }

    DrawRectangleLinesEx({0, 0, static_cast<float>(world.width() * kTileSize), static_cast<float>(world.height() * kTileSize)}, 3.0f,
                         {200, 60, 60, 255});
}

void drawGhost(const World& world, int tx, int ty, BuildingId id, Dir dir) {
    if (!world.inBounds(tx, ty) || id == BuildingId::None) return;
    const Rectangle r = tileRect(tx, ty);
    const bool occupied = world.at(tx, ty).building != BuildingId::None;
    const bool valid = world.canPlace(tx, ty, id);
    if (!occupied) drawBuildingIcon(id, dir, r, 0.6f);
    DrawRectangleLinesEx(r, 2.0f, valid ? Color{120, 255, 140, 220} : Color{255, 80, 80, 220});
    if (!valid) DrawRectangleRec(r, {255, 60, 60, 60});
}
