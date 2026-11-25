#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {
constexpr float MAX_SPEED = 520.f;
constexpr float CRUISE = 170.f;
constexpr float MIN_SPEED = 40.f;
constexpr float DEMO_SPEED = 260.f;
constexpr float EMP_RADIUS = 200.f;

float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
float approach(float v, float target, float step) {
    if (v < target) return std::min(v + step, target);
    return std::max(v - step, target);
}
uint32_t hashi(long i) {
    uint32_t h = (uint32_t)(i * 2654435761u) ^ 0x5bd1e995u;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return h;
}
std::string pad6(long v) {
    char b[32];
    std::snprintf(b, sizeof b, "%06ld", std::max(0L, v));
    return b;
}
std::string hiscorePath() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.road_warden_hiscore";
}

const Color CIV_COLORS[] = {rgb(0x3b6fd8), rgb(0xe8c23a), rgb(0xe0e0e0), rgb(0x3fa86b), rgb(0xe07b2f), rgb(0x9b59d0), rgb(0x5bc8d8)};

// Terrain palette: [terrain][stripe]
const Color TERRAIN[4][2] = {
    {rgb(0x3d8f3d), rgb(0x378638)},
    {rgb(0xd8b46a), rgb(0xd0ab60)},
    {rgb(0xe9eff3), rgb(0xdee6ec)},
    {rgb(0x2a5ea6), rgb(0x2e66b1)},
};
const Color MEDIAN_FILL[4] = {rgb(0x2f7531), rgb(0xb8975a), rgb(0xc9d3da), rgb(0x2a5ea6)};
}  // namespace

Game::Game(Audio& a) : audio(a) {
    loadHiscore();
    rng = 0xC0FFEEu;
    resetWorld();
}

float Game::rnd(float a, float b) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return a + (b - a) * (rng / 4294967295.f);
}

void Game::loadHiscore() {
    std::ifstream f(hiscorePath());
    long v = 0;
    if (f >> v) hiscore = v;
}
void Game::saveHiscore() {
    std::ofstream f(hiscorePath());
    if (f) f << hiscore << "\n";
}

void Game::rumble(float strong, float weak, int ms) {
    // keep the strongest pending request this frame
    if (!rumbleReq.pending || strong + weak > rumbleReq.strong + rumbleReq.weak) {
        rumbleReq = {strong, weak, ms, true};
    }
}

void Game::message(const std::string& s, Color c, float time) {
    msgText = s; msgColor = c; msgTime = time;
}

void Game::addScore(int pts, float x, double y, Color c) {
    score = std::max(0.0, score + pts);
    char b[32];
    std::snprintf(b, sizeof b, "%+d", pts);
    floats.push_back({b, x, y, 1.2f, c});
}

// ---------------------------------------------------------------- flow

void Game::resetWorld() {
    road.reset((uint32_t)(rng ^ (uint32_t)(clock * 1000)) | 1u);
    cars.clear(); bullets.clear(); mines.clear(); drones.clear(); bombs.clear();
    pickups.clear(); particles.clear(); rings.clear(); floats.clear();
    player = Car{};
    player.kind = K_PLAYER;
    player.x = VW / 2;
    player.y = 140;
    player.w = 24; player.h = 42;
    player.mass = 1.6f;
    player.speed = DEMO_SPEED;
    playerAlive = false;
    camY = 0;
    anchor = 140;
    civTimer = 0.5f;
    road.ensure(camY + VH + 800);
}

void Game::startGame() {
    resetWorld();
    if (const char* sec = std::getenv("RW_SECTOR")) {   // dev/test: start further down the road
        player.y += 15000.0 * std::max(0, std::atoi(sec) - 1);
        camY = player.y - anchor;
        road.ensure(camY + VH + 800);
        road.trim(camY - 500);
    }
    state = State::Playing;
    playerAlive = true;
    player.speed = CRUISE;
    lives = 3;
    emp = 2;
    level = 0;
    score = 0;
    nextLife = 10000;
    invuln = 3.f; shield = 0; spread = 0; fireCd = 0;
    rammerTimer = 4.f; armorTimer = 10.f; droneTimer = 14.f;
    nextPickupY = player.y + 3000;
    message("GET READY!", rgb(0xffe14d), 2.f);
    audio.play(Sfx::ExtraLife, 0.6f);
}

void Game::gameOver() {
    state = State::GameOver;
    stateTimer = 0;
    if ((long)score > hiscore) { hiscore = (long)score; saveHiscore(); }
}

void Game::killPlayer(const char* why) {
    if (!playerAlive) return;
    if (autopilot) std::fprintf(stderr, "death: %s (sector %d, y=%.0f)\n", why, level + 1, player.y);
    playerAlive = false;
    bool water = road.at(player.y).terrain == T_WATER && !road.drivable(player.x, player.y, 0);
    explode(player.x, player.y, 2.0f, water);
    audio.play(Sfx::BigExplosion, 1.f);
    rumble(1.f, 1.f, 650);
    shake = 0.6f;
    --lives;
    deathTimer = 2.4f;
    player.vx = player.push = 0;
}

void Game::respawnPlayer() {
    double y = player.y;
    float candidates[] = {road.at(y).cx, road.laneX(y, -0.5f), road.laneX(y, 0.5f)};
    float x = candidates[1];
    for (float c : candidates)
        if (road.drivable(c, y, 20) && road.drivable(c, y + 120, 20)) { x = c; break; }
    player.x = x;
    player.speed = CRUISE;
    player.vx = player.push = 0;
    playerAlive = true;
    invuln = 3.f;
    // clear the immediate area so the player gets a fair restart
    for (auto& c : cars)
        if (c.kind != K_CIV && c.kind != K_TRUCK && c.y > y - 250 && c.y < y + 500) c.dead = true;
    for (auto& c : cars)
        if (c.y > y - 60 && c.y < y + 120 && std::fabs(c.x - x) < 40) c.dead = true;
    for (auto& m : mines) if (m.y > y - 100 && m.y < y + 600) m.dead = true;
    bombs.clear();
    audio.play(Sfx::Pickup, 0.6f);
}

// ---------------------------------------------------------------- update

void Game::update(float dt, const Input& in) {
    clock += dt;
    if (msgTime > 0) msgTime -= dt;
    stateTimer += dt;

    switch (state) {
    case State::Title:
        if (in.back) { wantQuit = true; return; }
        if (in.confirm || in.pause || (autopilot && stateTimer > 1.f)) { startGame(); return; }
        updateWorld(dt, nullptr);
        break;
    case State::Playing:
        if (autopilot) {
            Input ai = in;
            float look = (float)(player.y + 90);
            float best = player.x, bestD = 1e9f;
            for (float f : {-0.75f, -0.5f, -0.3f, 0.3f, 0.5f, 0.75f}) {
                float x = road.laneX(look, f);
                bool blocked = false;
                for (const auto& c : cars)
                    if (c.y > player.y && c.y < player.y + 160 && std::fabs(c.x - x) < 30) blocked = true;
                float d = std::fabs(x - player.x) + (blocked ? 400.f : 0.f);
                if (d < bestD) { bestD = d; best = x; }
            }
            ai.steer = clampf((best - player.x) / 40.f, -1, 1);
            ai.throttle = 0.6f;
            ai.fire = false;
            for (const auto& c : cars)
                if ((c.kind == K_RAMMER || c.kind == K_ARMOR) && c.y > player.y && std::fabs(c.x - player.x) < 20) ai.fire = true;
            for (const auto& d : drones) if (std::fabs(d.x - player.x) < 20) ai.fire = true;
            ai.emp = std::fmod(clock, 7.f) < 0.01f;
            updateWorld(dt, &ai);
            break;
        }
        if (in.pause || in.back) { state = State::Paused; audio.play(Sfx::Blip); audio.setEngine(0, 0); return; }
        updateWorld(dt, &in);
        break;
    case State::Paused:
        if (in.confirm || in.pause) { state = State::Playing; audio.play(Sfx::Blip); }
        else if (in.back) { gameOver(); state = State::Title; resetWorld(); }
        break;
    case State::GameOver:
        if (stateTimer > 1.5f && (in.confirm || in.pause || in.back || autopilot)) { state = State::Title; resetWorld(); return; }
        updateWorld(dt, nullptr);
        break;
    }
}

void Game::updateWorld(float dt, const Input* in) {
    bool demo = (state != State::Playing);

    if (!demo && playerAlive && in) {
        controlPlayer(dt, *in);
    } else if (demo) {
        player.speed = DEMO_SPEED;
        player.y += DEMO_SPEED * dt;
        player.x = road.at(player.y).cx;
    } else {
        // dead, waiting to respawn
        deathTimer -= dt;
        if (deathTimer <= 0) {
            if (lives <= 0) gameOver();
            else respawnPlayer();
        }
    }
    audio.setEngine(player.speed / MAX_SPEED, (!demo && playerAlive) ? 0.6f : 0.f);

    float targetAnchor = 110.f + 70.f * (player.speed / MAX_SPEED);
    anchor += (targetAnchor - anchor) * std::min(1.f, dt * 2.f);
    camY = player.y - anchor;
    road.ensure(camY + VH + 800);
    road.trim(camY - 500);

    if (!demo) {
        level = (int)(player.y / 15000.0);
        if ((long)score >= nextLife) {
            nextLife += 10000;
            ++lives;
            message("EXTRA CAR!", rgb(0x7dff8a));
            audio.play(Sfx::ExtraLife);
        }
    }

    spawn(dt, demo);
    updateCars(dt);
    updateDrones(dt);
    updateProjectiles(dt);
    collideCars();

    // off-road checks
    if (!demo && playerAlive && !road.drivable(player.x, player.y, player.w * 0.25f)) {
        if (shield > 0 || invuln > 0) {
            // Shield (or post-spawn invulnerability) deflects off the road edge / median instead of crashing (costs some shield time).
            float safeX = road.clampX(player.x, player.y, player.w * 0.25f + 2.f);
            float dir = safeX > player.x ? 1.f : -1.f;
            player.x = safeX;
            player.vx = 0;
            player.push = dir * 260.f;
            player.speed *= 0.85f;
            if (shield > 0 && invuln <= 0) shield = std::max(0.01f, shield - 1.5f);
            if (bumpSoundCd <= 0) {
                audio.play(Sfx::Bump, 0.8f);
                bumpSoundCd = 0.15f;
                for (int k = 0; k < 8; ++k)
                    particles.push_back({player.x - dir * player.w * 0.5f, player.y, -dir * rnd(40, 160), rnd(-60, 120) + player.speed,
                                         0.3f, 0.3f, 2.5f, rgb(0x7dff8a)});
            }
            rumble(0.4f, 0.6f, 150);
            shake = std::max(shake, 0.15f);
        } else {
            killPlayer("off road");
        }
    }
    for (auto& c : cars)
        if (!c.dead && !road.drivable(c.x, c.y, c.w * 0.25f)) destroyCar(c, c.credit > 0, true);

    // despawn far-away objects
    double lo = camY - 320, hi = camY + VH + 750;
    for (auto& c : cars) if (c.y < lo || c.y > hi) c.dead = true;
    for (auto& m : mines) if (m.y < camY - 60) m.dead = true;
    for (auto& p : pickups) if (p.y < camY - 60) p.dead = true;
    for (auto& d : drones) if (d.y > camY + VH + 300 || d.y < camY - 200) d.dead = true;

    auto sweep = [](auto& v) { v.erase(std::remove_if(v.begin(), v.end(), [](const auto& e) { return e.dead; }), v.end()); };
    sweep(cars); sweep(bullets); sweep(mines); sweep(drones); sweep(bombs); sweep(pickups);

    // effects
    for (auto& p : particles) {
        p.x += p.vx * dt; p.y += p.vy * dt;
        p.vx *= std::exp(-2.f * dt); p.vy *= std::exp(-2.f * dt);
        p.life -= dt;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }), particles.end());
    for (auto& r : rings) { r.life -= dt; r.r += (r.maxR - r.r) * std::min(1.f, dt * 8.f); r.y += player.speed * dt; }
    rings.erase(std::remove_if(rings.begin(), rings.end(), [](const Ring& r) { return r.life <= 0; }), rings.end());
    for (auto& f : floats) { f.life -= dt; f.y += (player.speed + 40) * dt; }
    floats.erase(std::remove_if(floats.begin(), floats.end(), [](const FloatText& f) { return f.life <= 0; }), floats.end());
    bumpSoundCd -= dt;
    shake = std::max(0.f, shake - dt);
}

void Game::controlPlayer(float dt, const Input& in) {
    float target = CRUISE + in.throttle * (MAX_SPEED - CRUISE);
    float rate = 240.f + 160.f * in.throttle;
    if (in.brake > 0.05f) {
        target = std::min(target, CRUISE - in.brake * (CRUISE - MIN_SPEED));
        rate = 520.f;
    } else if (target < player.speed) {
        rate = 150.f;
    }
    player.speed = approach(player.speed, target, rate * dt);

    float steerMax = 150.f + 200.f * std::min(1.f, player.speed / 300.f);
    player.vx = approach(player.vx, in.steer * steerMax, 2400.f * dt);
    player.push *= std::exp(-4.f * dt);
    player.x += (player.vx + player.push) * dt;
    player.y += player.speed * dt;

    fireCd -= dt;
    if (in.fire && fireCd <= 0) {
        float vy = player.speed + 950.f;
        float nose = (float)(player.y + player.h * 0.5f);
        if (spread > 0) {
            fireCd = 0.09f;
            for (int k = -1; k <= 1; ++k) bullets.push_back({player.x + k * 6.f, nose, k * 160.f, vy});
        } else {
            fireCd = 0.13f;
            bullets.push_back({player.x - 5.f, nose, 0, vy});
            bullets.push_back({player.x + 5.f, nose, 0, vy});
        }
        audio.play(Sfx::Gun, 0.5f);
    }
    if (in.emp) triggerEmp();

    score += player.speed * dt * 0.05;
    invuln = std::max(0.f, invuln - dt);
    shield = std::max(0.f, shield - dt);
    spread = std::max(0.f, spread - dt);
    player.t += dt;
}

void Game::triggerEmp() {
    if (emp <= 0) { audio.play(Sfx::Hit, 0.5f); return; }
    --emp;
    rings.push_back({player.x, player.y, 10, EMP_RADIUS, 0.6f, rgb(0x6fe8ff)});
    audio.play(Sfx::Emp);
    rumble(0.5f, 0.8f, 300);
    for (auto& c : cars) {
        if (c.dead || c.kind == K_CIV || c.kind == K_TRUCK) continue;
        float dx = c.x - player.x; float dy = (float)(c.y - player.y);
        if (dx * dx + dy * dy < EMP_RADIUS * EMP_RADIUS) destroyCar(c, true, false);
    }
    for (auto& d : drones) {
        float dx = d.x - player.x; float dy = (float)(d.y - player.y);
        if (!d.dead && dx * dx + dy * dy < EMP_RADIUS * EMP_RADIUS * 1.6f) {
            d.dead = true;
            explode(d.x, d.y, 1.2f, false);
            addScore(400, d.x, d.y, rgb(0xffe14d));
        }
    }
    for (auto& m : mines) {
        float dx = m.x - player.x; float dy = (float)(m.y - player.y);
        if (!m.dead && dx * dx + dy * dy < EMP_RADIUS * EMP_RADIUS) { m.dead = true; explode(m.x, m.y, 0.4f, false); }
    }
    bombs.clear();
}

// ---------------------------------------------------------------- spawning

bool Game::spawnCar(int kind, bool ahead) {
    Car c;
    c.kind = kind;
    switch (kind) {
    case K_CIV:
        c.w = 22; c.h = 38; c.mass = 1; c.hp = 1;
        c.cruise = rnd(150, 290);
        c.color = CIV_COLORS[(int)rnd(0, 6.99f)];
        break;
    case K_TRUCK:
        c.w = 28; c.h = 66; c.mass = 2.5f; c.hp = 3;
        c.cruise = rnd(130, 200);
        c.color = CIV_COLORS[(int)rnd(0, 6.99f)];
        break;
    case K_RAMMER:
        c.w = 24; c.h = 40; c.mass = 1.3f; c.hp = 2;
        c.cruise = 330; c.life = rnd(18, 26);
        c.color = rgb(0xb8202a);
        break;
    case K_ARMOR:
        c.w = 32; c.h = 58; c.mass = 4.f; c.hp = 9;
        c.cruise = 300; c.life = rnd(24, 32);
        c.mineCd = 1.2f;
        c.color = rgb(0x4d5a3a);
        break;
    }
    c.side = rnd(0, 1) < 0.5f ? -1.f : 1.f;
    c.laneTimer = rnd(2, 6);
    double y = ahead ? camY + VH + 90 : camY - 90;
    for (int tries = 0; tries < 6; ++tries) {
        float f = (rnd(0, 1) < 0.5f ? -1.f : 1.f) * rnd(0.25f, 0.8f);
        float x = road.laneX(y, f);
        if (!road.drivable(x, y, c.w * 0.5f + 2)) continue;
        bool clear = true;
        for (const auto& o : cars)
            if (std::fabs(o.y - y) < (o.h + c.h) * 0.5f + 30 && std::fabs(o.x - x) < (o.w + c.w) * 0.5f + 10) { clear = false; break; }
        if (!clear) continue;
        c.x = x; c.y = y; c.lane = f;
        if (kind == K_RAMMER) c.speed = player.speed + 150;
        else if (kind == K_ARMOR) c.speed = std::max(0.f, player.speed - 100);
        else c.speed = c.cruise;
        cars.push_back(c);
        return true;
    }
    return false;
}

void Game::spawn(float dt, bool demo) {
    int civs = 0, rammers = 0, armors = 0;
    for (const auto& c : cars) {
        if (c.kind == K_CIV || c.kind == K_TRUCK) ++civs;
        else if (c.kind == K_RAMMER) ++rammers;
        else if (c.kind == K_ARMOR) ++armors;
    }
    civTimer -= dt;
    if (civTimer <= 0) {
        civTimer = rnd(0.7f, 1.6f);
        if (civs < 8) {
            int kind = rnd(0, 1) < 0.22f ? K_TRUCK : K_CIV;
            bool ahead = player.speed > 230 || rnd(0, 1) < 0.5f;
            spawnCar(kind, ahead);
        }
    }
    if (demo || !playerAlive) return;

    rammerTimer -= dt;
    if (rammerTimer <= 0) {
        rammerTimer = std::max(2.2f, 6.5f - level * 0.7f) * rnd(0.7f, 1.3f);
        if (rammers < 2 + level / 2) spawnCar(K_RAMMER, false);
    }
    if (level >= 1) {
        armorTimer -= dt;
        if (armorTimer <= 0) {
            armorTimer = std::max(6.f, 16.f - level * 1.5f) * rnd(0.8f, 1.2f);
            if (armors < 1 + level / 3) spawnCar(K_ARMOR, true);
        }
    }
    if (level >= 2) {
        droneTimer -= dt;
        if (droneTimer <= 0) {
            droneTimer = std::max(9.f, 22.f - level * 2.f) * rnd(0.8f, 1.2f);
            if ((int)drones.size() < 1 + (level >= 5)) {
                Drone d;
                d.x = rnd(150, VW - 150);
                d.y = camY + VH + 80;
                d.hp = 5 + level / 2;
                drones.push_back(d);
                message("WARNING: DRONE!", rgb(0xff6a4d), 1.5f);
            }
        }
    }
    if (player.y > nextPickupY) {
        nextPickupY = player.y + rnd(2600, 4200);
        double y = camY + VH + 60;
        float x = road.laneX(y, (rnd(0, 1) < 0.5f ? -1.f : 1.f) * rnd(0.3f, 0.7f));
        float r = rnd(0, 1);
        int type = r < 0.38f ? 0 : r < 0.66f ? 1 : r < 0.94f ? 2 : 3;
        pickups.push_back({x, y, type});
    }
}

// ---------------------------------------------------------------- AI

void Game::updateCars(float dt) {
    for (auto& c : cars) {
        if (c.dead) continue;
        c.t += dt;
        c.flash = std::max(0.f, c.flash - dt);
        c.credit = std::max(0.f, c.credit - dt);

        float dX = c.x, dS = c.speed, maxLat = 120, acc = 200;
        switch (c.kind) {
        case K_CIV:
        case K_TRUCK: {
            c.laneTimer -= dt;
            if (c.laneTimer <= 0) {
                c.laneTimer = rnd(3, 8);
                if (rnd(0, 1) < 0.45f) c.lane = (rnd(0, 1) < 0.5f ? -1.f : 1.f) * rnd(0.25f, 0.8f);
            }
            dX = road.laneX(c.y + 40, c.lane);
            dS = c.cruise;
            auto avoid = [&](const Car& o) {
                double gap = o.y - c.y;
                if (gap > 0 && gap < (o.h + c.h) * 0.5f + 55 && std::fabs(o.x - c.x) < (o.w + c.w) * 0.5f + 4)
                    dS = std::min(dS, o.speed - 15);
            };
            for (const auto& o : cars) if (&o != &c && !o.dead) avoid(o);
            if (playerAlive) avoid(player);
            maxLat = c.kind == K_TRUCK ? 80 : 110;
            acc = 160;
            break;
        }
        case K_RAMMER: {
            maxLat = 270; acc = 420;
            if (!playerAlive || c.t > c.life) {
                dS = playerAlive ? player.speed - 180 : c.cruise;
                dX = road.laneX(c.y + 40, c.lane);
            } else {
                double dy = player.y - c.y;
                if (dy > 70) {
                    dS = player.speed + 160;
                    dX = player.x + c.side * 48;
                } else {
                    double ty = player.y + 14 * std::sin(c.t * 1.7f);
                    dS = player.speed + clampf((float)(ty - c.y) * 3.f, -220, 220);
                    dX = std::fabs(dy) < 55 ? player.x : player.x + c.side * 48;
                }
            }
            dX = road.clampX(dX, c.y + 30, c.w * 0.5f + 3);
            break;
        }
        case K_ARMOR: {
            maxLat = 90; acc = 260;
            if (!playerAlive || c.t > c.life) {
                dS = player.speed + 280;
                dX = road.laneX(c.y + 60, c.lane);
            } else {
                double ty = player.y + 240;
                dS = player.speed + clampf((float)(ty - c.y) * 1.5f, -260, 260);
                dX = player.x;
                c.mineCd -= dt;
                if (c.mineCd <= 0 && c.y - player.y > 120 && c.y < camY + VH - 20) {
                    c.mineCd = std::max(0.6f, 1.4f - level * 0.08f);
                    mines.push_back({c.x, c.y - c.h * 0.5f - 10});
                    audio.play(Sfx::Mine, 0.5f);
                }
            }
            dX = road.clampX(dX, c.y + 60, c.w * 0.5f + 4);
            break;
        }
        }

        float ctrl = clampf((dX - c.x) * 4.f, -maxLat, maxLat);
        c.vx += (ctrl - c.vx) * std::min(1.f, dt * 6.f);
        c.push *= std::exp(-3.5f * dt);
        c.x += (c.vx + c.push) * dt;
        c.speed = std::max(0.f, approach(c.speed, dS, acc * dt));
        c.y += c.speed * dt;
    }
}

void Game::updateDrones(float dt) {
    for (auto& d : drones) {
        d.t += dt;
        d.flash = std::max(0.f, d.flash - dt);
        d.life -= dt;
        if (d.life <= 0 || !playerAlive) d.leaving = true;
        if (d.leaving) {
            d.y += (player.speed + 320) * dt;
            continue;
        }
        double ty = player.y + 360;
        d.y += (player.speed + clampf((float)(ty - d.y) * 2.f, -320, 320)) * dt;
        d.vx += ((player.x - d.x) * 1.8f - d.vx) * std::min(1.f, dt * 1.5f);
        d.x += d.vx * dt;
        d.bombCd -= dt;
        if (d.bombCd <= 0 && std::fabs(d.y - ty) < 80) {
            d.bombCd = std::max(1.1f, 2.2f - level * 0.1f);
            float lead = 0.9f;
            Bomb b;
            b.x0 = d.x; b.y0 = d.y;
            b.x1 = road.clampX(player.x + (player.vx + player.push) * lead * 0.7f, player.y + player.speed * lead, 10);
            b.y1 = player.y + player.speed * lead;
            b.dur = lead;
            bombs.push_back(b);
            audio.play(Sfx::BombDrop, 0.7f);
        }
    }
}

// ---------------------------------------------------------------- projectiles & hazards

void Game::updateProjectiles(float dt) {
    // player bullets
    for (auto& b : bullets) {
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.y > camY + VH + 30) { b.dead = true; continue; }
        for (auto& c : cars) {
            if (c.dead) continue;
            if (std::fabs(b.x - c.x) < c.w * 0.5f + 2 && std::fabs(b.y - c.y) < c.h * 0.5f + 4) {
                b.dead = true;
                c.flash = 0.08f;
                for (int i = 0; i < 4; ++i)
                    particles.push_back({b.x, b.y, rnd(-120, 120), rnd(-60, 120) + c.speed, 0.25f, 0.25f, 2, rgb(0xffe9a0)});
                if (--c.hp <= 0) destroyCar(c, true, false);
                else audio.play(Sfx::Hit, 0.4f);
                break;
            }
        }
        if (b.dead) continue;
        for (auto& d : drones) {
            if (d.dead) continue;
            if (std::fabs(b.x - d.x) < 22 && std::fabs(b.y - d.y) < 18) {
                b.dead = true;
                d.flash = 0.08f;
                if (--d.hp <= 0) {
                    d.dead = true;
                    explode(d.x, d.y, 1.3f, false);
                    audio.play(Sfx::Explosion);
                    addScore(400, d.x, d.y, rgb(0xffe14d));
                } else audio.play(Sfx::Hit, 0.4f);
                break;
            }
        }
        if (b.dead) continue;
        for (auto& m : mines) {
            if (!m.dead && std::fabs(b.x - m.x) < 9 && std::fabs(b.y - m.y) < 10) {
                m.dead = b.dead = true;
                explode(m.x, m.y, 0.4f, false);
                addScore(25, m.x, m.y, rgb(0xffffff));
                break;
            }
        }
    }

    // mines vs cars
    auto hitsCar = [](float x, double y, const Car& c, float r) {
        return std::fabs(x - c.x) < c.w * 0.5f + r && std::fabs(y - c.y) < c.h * 0.5f + r;
    };
    for (auto& m : mines) {
        m.t += dt;
        if (m.dead) continue;
        if (playerAlive && state == State::Playing && hitsCar(m.x, m.y, player, 4)) {
            m.dead = true;
            explode(m.x, m.y, 0.6f, false);
            if (invuln <= 0 && shield <= 0) killPlayer("mine");
            else audio.play(Sfx::Explosion, 0.6f);
            continue;
        }
        for (auto& c : cars) {
            if (c.dead || c.kind == K_ARMOR) continue;
            if (hitsCar(m.x, m.y, c, 3)) { m.dead = true; destroyCar(c, false, false); break; }
        }
    }

    // bombs
    for (auto& b : bombs) {
        b.t += dt;
        if (b.t < b.dur) continue;
        b.dead = true;
        bool water = !road.drivable(b.x1, b.y1, 0) && road.at(b.y1).terrain == T_WATER;
        explode(b.x1, b.y1, 1.0f, water);
        audio.play(water ? Sfx::Splash : Sfx::Explosion, 0.8f);
        const float R = 34;
        if (playerAlive && state == State::Playing && hitsCar(b.x1, b.y1, player, R * 0.6f) && invuln <= 0 && shield <= 0)
            killPlayer("bomb");
        for (auto& c : cars)
            if (!c.dead && hitsCar(b.x1, b.y1, c, R * 0.6f)) destroyCar(c, false, false);
    }

    // pickups
    for (auto& p : pickups) {
        p.t += dt;
        if (p.dead || !playerAlive || state != State::Playing) continue;
        if (std::fabs(p.x - player.x) < player.w * 0.5f + 12 && std::fabs(p.y - player.y) < player.h * 0.5f + 12) {
            p.dead = true;
            audio.play(Sfx::Pickup);
            rumble(0.f, 0.4f, 120);
            switch (p.type) {
            case 0: emp = std::min(emp + 1, 5); message("EMP CHARGE +1", rgb(0x6fe8ff)); break;
            case 1: shield = 9.f; message("SHIELD!", rgb(0x7dff8a)); break;
            case 2: spread = 12.f; message("SPREAD GUN!", rgb(0xffb14d)); break;
            case 3: ++lives; message("EXTRA CAR!", rgb(0x7dff8a)); audio.play(Sfx::ExtraLife); break;
            }
            addScore(100, p.x, p.y, rgb(0xffffff));
        }
    }
}

// ---------------------------------------------------------------- collisions

void Game::collideCars() {
    std::vector<Car*> all;
    all.reserve(cars.size() + 1);
    if (playerAlive && state == State::Playing) all.push_back(&player);
    for (auto& c : cars) if (!c.dead) all.push_back(&c);

    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            Car& a = *all[i];
            Car& b = *all[j];
            float dx = b.x - a.x;
            float dy = (float)(b.y - a.y);
            float ox = (a.w + b.w) * 0.5f - std::fabs(dx);
            float oy = (a.h + b.h) * 0.5f - std::fabs(dy);
            if (ox <= 0 || oy <= 0) continue;

            bool aP = a.kind == K_PLAYER, bP = b.kind == K_PLAYER;
            float ma = a.mass, mb = b.mass;
            if (aP && shield > 0) ma *= 6;
            if (bP && shield > 0) mb *= 6;
            float tot = ma + mb;
            float impact;

            if (ox < oy * 0.8f) {
                // side swipe
                float s = dx > 0 ? 1.f : -1.f;
                a.x -= s * ox * (mb / tot);
                b.x += s * ox * (ma / tot);
                float rel = std::fabs((a.vx + a.push) - (b.vx + b.push));
                float J = 230.f + rel * 0.5f;
                a.push -= s * J * (mb / tot) * 1.6f;
                b.push += s * J * (ma / tot) * 1.6f;
                impact = J;
            } else {
                // rear-end
                Car& front = dy > 0 ? b : a;
                Car& back = dy > 0 ? a : b;
                float mf = (&front == &a) ? ma : mb, mk = (&back == &a) ? ma : mb;
                front.y += oy * (mk / tot);
                back.y -= oy * (mf / tot);
                float v = (front.speed * mf + back.speed * mk) / tot;
                impact = std::fabs(back.speed - front.speed);
                front.speed = std::max(front.speed, v + 25);
                back.speed = std::min(back.speed, v - 45);
                float kick = (dx >= 0 ? 1.f : -1.f) * (60.f + impact * 0.3f);
                front.push += kick * (mk / tot);
                back.push -= kick * (mf / tot);
            }

            if (aP || bP) {
                Car& other = aP ? b : a;
                other.credit = 2.0f;
                rumble(0.35f, 0.6f, 140);
                shake = std::max(shake, 0.12f);
            }
            if (bumpSoundCd <= 0) {
                audio.play(Sfx::Bump, std::min(1.f, 0.4f + impact / 400.f));
                bumpSoundCd = 0.1f;
                for (int k = 0; k < 6; ++k)
                    particles.push_back({(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5, rnd(-150, 150), rnd(-80, 80) + a.speed,
                                         0.3f, 0.3f, 2, rgb(0xfff2b0)});
            }
        }
    }
}

// ---------------------------------------------------------------- destruction

void Game::destroyCar(Car& c, bool byPlayer, bool bumped) {
    if (c.dead) return;
    c.dead = true;
    bool water = road.at(c.y).terrain == T_WATER && !road.drivable(c.x, c.y, 0);
    explode(c.x, c.y, c.kind == K_ARMOR || c.kind == K_TRUCK ? 1.6f : 1.0f, water);
    audio.play(water ? Sfx::Splash : Sfx::Explosion, 0.8f);
    if (!byPlayer || state != State::Playing) return;

    switch (c.kind) {
    case K_CIV:
    case K_TRUCK:
        addScore(-750, c.x, c.y, rgb(0xff5050));
        message("CIVILIAN HIT! -750", rgb(0xff5050), 1.5f);
        break;
    case K_RAMMER:
        addScore(bumped ? 400 : 150, c.x, c.y, rgb(0xffe14d));
        if (bumped) message("RUN OFF THE ROAD!", rgb(0xffe14d), 1.2f);
        break;
    case K_ARMOR:
        addScore(bumped ? 1200 : 600, c.x, c.y, rgb(0xffe14d));
        break;
    }
}

void Game::explode(float x, double y, float power, bool water) {
    int n = (int)(26 * power);
    for (int i = 0; i < n; ++i) {
        float a = rnd(0, 6.2831853f), s = rnd(30, 220) * power;
        Color c;
        if (water) c = rnd(0, 1) < 0.5f ? rgb(0xffffff) : rgb(0x8cc8ff);
        else {
            float r = rnd(0, 1);
            c = r < 0.35f ? rgb(0xffe14d) : r < 0.7f ? rgb(0xff7a2a) : r < 0.85f ? rgb(0xd83a1e) : rgb(0x555555);
        }
        float life = rnd(0.4f, 0.9f) * (0.7f + 0.3f * power);
        particles.push_back({x, y, std::cos(a) * s, std::sin(a) * s + player.speed * 0.3f, life, life, rnd(2, 5) * (0.6f + 0.4f * power), c});
    }
    if (particles.size() > 2500) particles.erase(particles.begin(), particles.begin() + (particles.size() - 2500));
    rings.push_back({x, y, 4, 30 * power, 0.25f, water ? rgb(0xffffff, 0.7f) : rgb(0xffd080, 0.7f)});
}

// ---------------------------------------------------------------- rendering

void Game::drawRoad() {
    long i0 = (long)std::floor(camY / Road::ROW_H) - 1;
    long i1 = (long)std::ceil((camY + VH) / Road::ROW_H) + 1;

    for (long i = i0; i < i1; ++i) {
        const RoadRow& a = road.row(i);
        const RoadRow& b = road.row(i + 1);
        float ya = sy(i * Road::ROW_H) + 0.6f, yb = sy((i + 1) * Road::ROW_H) - 0.6f;   // overlap: no seams
        int ter = a.terrain;
        bool bridge = ter == T_WATER;
        gfx::rect(0, yb, VW, ya - yb, TERRAIN[ter][(i / 6) & 1]);

        if (bridge) {   // bridge deck & railing
            gfx::quad(a.cx - a.hw - 14, ya, a.cx + a.hw + 14, ya, b.cx + b.hw + 14, yb, b.cx - b.hw - 14, yb, rgb(0x8a8f96));
            if ((i & 3) == 0) {
                gfx::rect(a.cx - a.hw - 14, yb, 4, ya - yb, rgb(0x5d6168));
                gfx::rect(a.cx + a.hw + 10, yb, 4, ya - yb, rgb(0x5d6168));
            }
        }

        Color curb = bridge ? rgb(0xc8ccd2) : (((i / 2) & 1) ? rgb(0xd23a32) : rgb(0xf0f0f0));
        Color asphalt = bridge ? rgb(0x5a5a62) : rgb(0x4a4a52);
        gfx::quad(a.cx - a.hw - 6, ya, a.cx - a.hw, ya, b.cx - b.hw, yb, b.cx - b.hw - 6, yb, curb);
        gfx::quad(a.cx + a.hw, ya, a.cx + a.hw + 6, ya, b.cx + b.hw + 6, yb, b.cx + b.hw, yb, curb);

        bool divided = a.med > 0.5f || b.med > 0.5f;
        if (!divided) {
            gfx::quad(a.cx - a.hw, ya, a.cx + a.hw, ya, b.cx + b.hw, yb, b.cx - b.hw, yb, asphalt);
        } else {
            gfx::quad(a.cx - a.hw, ya, a.cx - a.med, ya, b.cx - b.med, yb, b.cx - b.hw, yb, asphalt);
            gfx::quad(a.cx + a.med, ya, a.cx + a.hw, ya, b.cx + b.hw, yb, b.cx + b.med, yb, asphalt);
            Color barrier = ((i / 2) & 1) ? rgb(0xf2c230) : rgb(0x222222);
            float ma = std::max(0.f, a.med - 4), mb = std::max(0.f, b.med - 4);
            gfx::quad(a.cx - a.med, ya, a.cx - ma, ya, b.cx - mb, yb, b.cx - b.med, yb, barrier);
            gfx::quad(a.cx + ma, ya, a.cx + a.med, ya, b.cx + b.med, yb, b.cx + mb, yb, barrier);
            if (ma > 0 || mb > 0)
                gfx::quad(a.cx - ma, ya, a.cx + ma, ya, b.cx + mb, yb, b.cx - mb, yb, MEDIAN_FILL[ter]);
        }

        // lane dashes
        if (((i / 3) & 1) == 0) {
            Color dash = rgb(0xf4f4f4, 0.85f);
            if (!divided) {
                int n = std::max(1, (int)std::lround(2 * a.hw / 62.f));
                for (int k = 1; k < n; ++k) {
                    float fa = a.cx - a.hw + k * 2 * a.hw / n, fb = b.cx - b.hw + k * 2 * b.hw / n;
                    Color dc = (n % 2 == 0 && k == n / 2) ? rgb(0xf2c230) : dash;
                    gfx::quad(fa - 1.5f, ya, fa + 1.5f, ya, fb + 1.5f, yb, fb - 1.5f, yb, dc);
                }
            } else {
                int n = std::max(1, (int)std::lround((a.hw - a.med) / 62.f));
                for (int side = -1; side <= 1; side += 2)
                    for (int k = 1; k < n; ++k) {
                        float fa = a.cx + side * (a.med + k * (a.hw - a.med) / n);
                        float fb = b.cx + side * (b.med + k * (b.hw - b.med) / n);
                        gfx::quad(fa - 1.5f, ya, fa + 1.5f, ya, fb + 1.5f, yb, fb - 1.5f, yb, dash);
                    }
            }
        }
    }

    // roadside decoration
    for (long i = i0 - 4; i < i1 + 4; ++i) {
        if (i % 3 != 0) continue;
        uint32_t h = hashi(i);
        const RoadRow& r = road.row(i);
        float y = sy(i * Road::ROW_H);
        for (int k = 0; k < 2; ++k) {
            uint32_t hk = hashi(i * 7 + k);
            if ((hk & 3) == 0) continue;
            float x = (float)(hk % 800);
            float d = std::fabs(x - r.cx);
            bool inMedian = r.med > 28 && d < r.med - 14;
            if (!inMedian && d < r.hw + (r.terrain == T_WATER ? 26 : 20)) continue;
            float s = 0.7f + (float)((h >> (k * 4)) & 15) / 25.f;
            switch (r.terrain) {
            case T_GRASS:
                gfx::circle(x + 3, y + 3, 9 * s, rgb(0x000000, 0.25f), 10);
                gfx::circle(x, y, 9 * s, rgb(0x1f5e24), 10);
                gfx::circle(x - 2 * s, y - 2 * s, 5 * s, rgb(0x2f7a32), 8);
                break;
            case T_DESERT:
                if (hk & 4) {
                    gfx::rect(x - 2 * s, y - 10 * s, 4 * s, 16 * s, rgb(0x4d8a3a));
                    gfx::rect(x - 7 * s, y - 5 * s, 3 * s, 6 * s, rgb(0x4d8a3a));
                    gfx::rect(x + 4 * s, y - 7 * s, 3 * s, 6 * s, rgb(0x4d8a3a));
                } else {
                    gfx::circle(x, y, 6 * s, rgb(0xa88a52), 8);
                }
                break;
            case T_SNOW:
                gfx::tri(x, y - 14 * s, x - 8 * s, y + 6 * s, x + 8 * s, y + 6 * s, rgb(0x1e4d33));
                gfx::tri(x, y - 14 * s, x - 4 * s, y - 4 * s, x + 4 * s, y - 4 * s, rgb(0xffffff));
                break;
            case T_WATER:
                gfx::rect(x - 8 * s, y, 16 * s, 2, rgb(0xffffff, 0.35f));
                break;
            }
        }
    }
}

void Game::drawCar(const Car& c, bool isPlayer) {
    float x = c.x, y = sy(c.y);
    float tilt = clampf((c.vx + c.push) * 0.035f, -16, 16);
    float w = c.w, h = c.h;
    float hw = w * 0.5f, hh = h * 0.5f;

    gfx::rect(x - hw + 4, y - hh + 5, w, h, rgb(0x000000, 0.28f));   // shadow
    gfx::push(x, y, tilt);
    Color tyre = rgb(0x151515);
    gfx::rect(-hw - 3, -hh + 5, 5, 11, tyre);
    gfx::rect(hw - 2, -hh + 5, 5, 11, tyre);
    gfx::rect(-hw - 3, hh - 15, 5, 11, tyre);
    gfx::rect(hw - 2, hh - 15, 5, 11, tyre);

    Color body = c.color;
    Color dark = mix(body, rgb(0x000000), 0.35f);
    Color glass = rgb(0x1c2a44);

    switch (c.kind) {
    case K_PLAYER: {
        body = rgb(0xdfe6ee); dark = rgb(0x8796a8);
        gfx::quad(-hw + 4, -hh, hw - 4, -hh, hw, -hh + 10, -hw, -hh + 10, body);    // wedge nose
        gfx::rect(-hw, -hh + 10, w, h - 10, body);
        gfx::rect(-3, -hh, 6, h, rgb(0x1fb6e0));                                  // centre stripe
        gfx::rect(-hw + 3, -hh + 13, w - 6, 8, glass);
        gfx::rect(-hw + 4, -hh + 21, w - 8, 9, dark);
        gfx::rect(-hw + 4, hh - 10, w - 8, 4, glass);
        gfx::rect(-hw - 1, hh - 4, w + 2, 4, rgb(0x2a2f38));                       // spoiler
        gfx::rect(-hw + 3, -hh + 1, 4, 3, rgb(0xfff6c0));
        gfx::rect(hw - 7, -hh + 1, 4, 3, rgb(0xfff6c0));
        // gun barrels
        gfx::rect(-7, -hh - 5, 3, 7, rgb(0x2a2f38));
        gfx::rect(4, -hh - 5, 3, 7, rgb(0x2a2f38));
        break;
    }
    case K_TRUCK: {
        gfx::rect(-hw, -hh, w, 18, body);
        gfx::rect(-hw + 3, -hh + 4, w - 6, 6, glass);
        gfx::rect(-hw - 1, -hh + 20, w + 2, h - 20, rgb(0xb9bcc2));
        gfx::rect(-hw + 2, -hh + 24, w - 4, h - 28, rgb(0xa5a9b0));
        gfx::rect(-hw + 2, -hh + 1, 4, 2, rgb(0xfff6c0));
        gfx::rect(hw - 6, -hh + 1, 4, 2, rgb(0xfff6c0));
        break;
    }
    case K_RAMMER: {
        gfx::rect(-hw, -hh, w, h, body);
        gfx::rect(-hw + 3, -hh + 9, w - 6, 7, glass);
        gfx::rect(-hw + 3, -hh + 16, w - 6, 12, rgb(0x1a1a1a));
        gfx::rect(-hw + 3, hh - 9, w - 6, 4, glass);
        for (int k = 0; k < 3; ++k) {   // side spikes
            float sy0 = -hh + 8 + k * 11;
            gfx::tri(-hw, sy0, -hw, sy0 + 6, -hw - 6, sy0 + 3, rgb(0xc8c8c8));
            gfx::tri(hw, sy0, hw, sy0 + 6, hw + 6, sy0 + 3, rgb(0xc8c8c8));
        }
        gfx::rect(-hw + 2, -hh, w - 4, 3, rgb(0x2b2b2b));   // ram bar
        break;
    }
    case K_ARMOR: {
        gfx::rect(-hw, -hh, w, h, body);
        gfx::rect(-hw + 3, -hh + 5, w - 6, 6, glass);
        gfx::rect(-hw + 2, -hh + 14, w - 4, h - 18, dark);
        gfx::circle(0, 6, 8, rgb(0x3a4430), 12);
        gfx::rect(-2, 6, 4, hh - 4, rgb(0x222222));        // rear mine chute
        for (int k = 0; k < 4; ++k) gfx::rect(-hw + 4, -hh + 16 + k * 9, w - 8, 2, rgb(0x5f6d48));
        gfx::rect(-hw + 2, hh - 3, 6, 3, rgb(0xff3030));
        gfx::rect(hw - 8, hh - 3, 6, 3, rgb(0xff3030));
        break;
    }
    default: {  // civilian sedan
        gfx::rect(-hw, -hh + 2, w, h - 4, body);
        gfx::rect(-hw + 2, -hh, w - 4, h, body);
        gfx::rect(-hw + 3, -hh + 9, w - 6, 7, glass);
        gfx::rect(-hw + 3, -hh + 16, w - 6, 10, mix(body, rgb(0xffffff), 0.18f));
        gfx::rect(-hw + 3, hh - 10, w - 6, 5, glass);
        gfx::rect(-hw + 2, -hh + 1, 4, 2, rgb(0xfff6c0));
        gfx::rect(hw - 6, -hh + 1, 4, 2, rgb(0xfff6c0));
        gfx::rect(-hw + 2, hh - 2, 4, 2, rgb(0xd02020));
        gfx::rect(hw - 6, hh - 2, 4, 2, rgb(0xd02020));
        break;
    }
    }
    if (c.flash > 0) gfx::rect(-hw, -hh, w, h, rgb(0xffffff, 0.75f));
    gfx::pop();

    if (isPlayer && shield > 0) {
        float a = shield < 2 ? 0.25f + 0.25f * std::sin(clock * 30) : 0.45f;
        gfx::ring(x, y, 30, 3, rgb(0x7dff8a, a), 32);
    }
}

void Game::drawHud() {
    gfx::rect(0, 0, VW, 36, rgb(0x000000, 0.6f));
    gfx::textShadow("SCORE", 12, 6, 2, rgb(0x9aa4b0));
    gfx::textShadow(pad6((long)score), 12, 20, 2, rgb(0xffffff));
    gfx::textShadow("HIGH", 140, 6, 2, rgb(0x9aa4b0));
    gfx::textShadow(pad6(std::max(hiscore, (long)score)), 140, 20, 2, rgb(0xffe14d));

    // lives as little cars
    gfx::textShadow("CARS", 270, 6, 2, rgb(0x9aa4b0));
    for (int i = 0; i < std::min(lives, 8); ++i) {
        float x = 272 + i * 12;
        gfx::rect(x, 19, 8, 13, rgb(0xdfe6ee));
        gfx::rect(x + 3, 19, 2, 13, rgb(0x1fb6e0));
    }

    gfx::textShadow("EMP", 390, 6, 2, rgb(0x9aa4b0));
    for (int i = 0; i < emp; ++i) gfx::circle(396 + i * 14, 26, 5, rgb(0x6fe8ff), 10);

    // speedometer
    gfx::textShadow("SPEED", 500, 6, 2, rgb(0x9aa4b0));
    float s = player.speed / MAX_SPEED;
    gfx::rect(500, 21, 120, 9, rgb(0x222831));
    gfx::rect(500, 21, 120 * s, 9, mix(rgb(0x3fd06b), rgb(0xff5040), s));
    char kmh[24];
    std::snprintf(kmh, sizeof kmh, "%d", (int)(player.speed * 0.55f));
    gfx::textShadow(kmh, 626, 21, 1.4f, rgb(0xffffff));

    float px = 690;
    if (shield > 0) { gfx::textShadow("SHIELD", px, 6, 2, rgb(0x7dff8a)); gfx::rect(px, 22, 60 * shield / 9.f, 5, rgb(0x7dff8a)); }
    if (spread > 0) { gfx::textShadow("SPREAD", px, shield > 0 ? 28 : 6, shield > 0 ? 1 : 2, rgb(0xffb14d));
                      if (shield <= 0) gfx::rect(px, 22, 60 * spread / 12.f, 5, rgb(0xffb14d)); }

    char lv[32];
    std::snprintf(lv, sizeof lv, "SECTOR %d", level + 1);
    gfx::textShadow(lv, VW - 10, VH - 18, 1.5f, rgb(0xffffff, 0.6f), gfx::Right);
}

void Game::drawTitle() {
    gfx::rect(0, 0, VW, VH, rgb(0x000000, 0.35f));
    gfx::rect(110, 90, VW - 220, 420, rgb(0x0b0f18, 0.82f));
    gfx::rect(110, 90, VW - 220, 4, rgb(0x1fb6e0));
    gfx::rect(110, 506, VW - 220, 4, rgb(0x1fb6e0));

    float bob = std::sin(clock * 2.f) * 3.f;
    gfx::textShadow("ROAD WARDEN", VW / 2, 120 + bob, 8, rgb(0xffffff), gfx::Center);
    gfx::text("HIGHWAY PURSUIT", VW / 2, 190, 2.5f, rgb(0x1fb6e0), gfx::Center);

    float y = 240;
    Color k = rgb(0xffe14d), v = rgb(0xd0d6de);
    auto line = [&](const char* key, const char* what) {
        gfx::text(key, 330, y, 2, k, gfx::Right);
        gfx::text(what, 350, y, 2, v);
        y += 22;
    };
    line("STICK / DPAD", "STEER");
    line("RT  /  UP", "ACCELERATE");
    line("LT  /  DOWN", "BRAKE");
    line("A RB / SPACE", "FIRE GUNS");
    line("B LB / X", "EMP BLAST");
    line("START / ESC", "PAUSE");

    gfx::text("RUN THE ENEMY OFF THE ROAD FOR BONUS POINTS.", VW / 2, 384, 1.5f, v, gfx::Center);
    gfx::text("DON'T HIT CIVILIANS. STAY ON THE ASPHALT!", VW / 2, 400, 1.5f, v, gfx::Center);

    std::string pad = padConnected ? "CONTROLLER: " + padName : "NO CONTROLLER - USING KEYBOARD";
    if (pad.size() > 48) pad = pad.substr(0, 48);
    gfx::text(pad, VW / 2, 428, 1.5f, padConnected ? rgb(0x7dff8a) : rgb(0xffb14d), gfx::Center);

    if (std::fmod(clock, 1.f) < 0.65f)
        gfx::textShadow("PRESS START", VW / 2, 455, 3, rgb(0xffffff), gfx::Center);
    gfx::text("HIGH SCORE " + pad6(hiscore), VW / 2, 488, 1.5f, k, gfx::Center);
}

void Game::render() {
    float sx = 0, syy = 0;
    if (shake > 0) { sx = rnd(-1, 1) * shake * 10; syy = rnd(-1, 1) * shake * 10; }
    gfx::push(sx, syy, 0);

    drawRoad();

    for (const auto& m : mines) {
        float x = m.x, y = sy(m.y);
        Color c = std::fmod(m.t, 0.5f) < 0.25f ? rgb(0xff3030) : rgb(0x801010);
        gfx::circle(x, y, 7, rgb(0x222222), 10);
        for (int k = 0; k < 4; ++k) {
            float a = k * 1.5708f + 0.785f;
            gfx::rect(x + std::cos(a) * 7 - 1.5f, y + std::sin(a) * 7 - 1.5f, 3, 3, rgb(0x9a9a9a));
        }
        gfx::circle(x, y, 2.5f, c, 8);
    }
    for (const auto& p : pickups) {
        float x = p.x, y = sy(p.y);
        static const char* letters[] = {"E", "S", "W", "+"};
        static const unsigned cols[] = {0x6fe8ff, 0x7dff8a, 0xffb14d, 0xff6ad5};
        float pulse = 1.f + 0.12f * std::sin(p.t * 8);
        gfx::rect(x - 11 * pulse + 3, y - 11 * pulse + 3, 22 * pulse, 22 * pulse, rgb(0x000000, 0.3f));
        gfx::rect(x - 11 * pulse, y - 11 * pulse, 22 * pulse, 22 * pulse, rgb(cols[p.type]));
        gfx::rect(x - 8, y - 8, 16, 16, rgb(0x10151f));
        gfx::text(letters[p.type], x, y - 5, 1.5f, rgb(cols[p.type]), gfx::Center);
    }
    for (const auto& b : bombs) {   // landing markers
        float u = b.t / b.dur;
        gfx::ring(b.x1, sy(b.y1), 30 * (1.1f - 0.6f * u), 2, rgb(0xff3030, 0.4f + 0.5f * u), 24);
        gfx::circle(b.x1, sy(b.y1), 10 * u, rgb(0x000000, 0.35f), 12);
    }
    for (const auto& d : drones) gfx::circle(d.x + 18, sy(d.y) + 30, 16, rgb(0x000000, 0.25f), 14);

    for (const auto& c : cars) drawCar(c, false);
    if (playerAlive && state != State::Title && state != State::GameOver) {
        bool blink = invuln > 0 && std::fmod(clock, 0.16f) < 0.08f;
        if (!blink) drawCar(player, true);
    }

    for (const auto& b : bullets) {
        float y = sy(b.y);
        gfx::rect(b.x - 1.5f, y - 6, 3, 10, rgb(0xfff0a0));
        gfx::rect(b.x - 0.5f, y - 9, 1, 4, rgb(0xffffff));
    }
    for (const auto& p : particles) {
        float a = p.life / p.maxLife;
        gfx::rect(p.x - p.size * 0.5f, sy(p.y) - p.size * 0.5f, p.size, p.size, withAlpha(p.c, a));
    }

    for (const auto& d : drones) {
        float x = d.x, y = sy(d.y);
        float rot = d.t * 40.f;
        Color body = d.flash > 0 ? rgb(0xffffff) : rgb(0x30363f);
        for (int k = 0; k < 4; ++k) {
            float a = k * 1.5708f + 0.785f;
            float ax = x + std::cos(a) * 16, ay = y + std::sin(a) * 12;
            gfx::rect(std::min(x, ax), std::min(y, ay) - 1, std::fabs(ax - x) + 1, std::fabs(ay - y) + 2, rgb(0x20242a));
            gfx::push(ax, ay, rot * (k % 2 ? 1 : -1));
            gfx::rect(-9, -1.5f, 18, 3, rgb(0xb0b8c2, 0.7f));
            gfx::pop();
        }
        gfx::circle(x, y, 9, body, 14);
        gfx::circle(x, y + 2, 3, std::fmod(d.t, 0.4f) < 0.2f ? rgb(0xff3030) : rgb(0x601010), 8);
    }
    for (const auto& b : bombs) {
        float u = b.t / b.dur;
        float x = b.x0 + (b.x1 - b.x0) * u;
        float y = sy(b.y0 + (b.y1 - b.y0) * u);
        gfx::circle(x, y, 5.5f - 2.f * u, rgb(0x111111), 10);
    }
    for (const auto& r : rings) gfx::ring(r.x, sy(r.y), r.r, 4, withAlpha(r.c, std::min(1.f, r.life * 2.f)), 48);
    for (const auto& f : floats)
        gfx::textShadow(f.s, f.x, sy(f.y), 2, withAlpha(f.c, std::min(1.f, f.life * 2.f)), gfx::Center);

    gfx::pop();

    if (state != State::Title) drawHud();

    if (msgTime > 0 && state == State::Playing)
        gfx::textShadow(msgText, VW / 2, 200, 3, withAlpha(msgColor, std::min(1.f, msgTime * 2.f)), gfx::Center);

    switch (state) {
    case State::Title: drawTitle(); break;
    case State::Paused:
        gfx::rect(0, 0, VW, VH, rgb(0x000000, 0.5f));
        gfx::textShadow("PAUSED", VW / 2, 240, 6, rgb(0xffffff), gfx::Center);
        gfx::text("START: RESUME     BACK / ESC: QUIT TO TITLE", VW / 2, 310, 1.5f, rgb(0xd0d6de), gfx::Center);
        break;
    case State::GameOver:
        gfx::rect(0, 0, VW, VH, rgb(0x000000, 0.45f));
        gfx::textShadow("GAME OVER", VW / 2, 220, 7, rgb(0xff5040), gfx::Center);
        gfx::textShadow("SCORE " + pad6((long)score), VW / 2, 300, 3, rgb(0xffffff), gfx::Center);
        if ((long)score >= hiscore && score > 0)
            gfx::textShadow("NEW HIGH SCORE!", VW / 2, 340, 3, rgb(0xffe14d), gfx::Center);
        if (stateTimer > 1.5f && std::fmod(clock, 1.f) < 0.65f)
            gfx::text("PRESS START", VW / 2, 400, 2, rgb(0xd0d6de), gfx::Center);
        break;
    default: break;
    }
}
