#pragma once
#include <string>
#include <vector>
#include <cstdint>

#include "audio.h"
#include "render.h"
#include "road.h"

struct Input {
    float steer = 0, throttle = 0, brake = 0;
    bool fire = false;
    bool emp = false;       // edge
    bool confirm = false;   // edge: Enter / Space / Start / A
    bool pause = false;     // edge: Start / P / Esc
    bool back = false;      // edge: Esc / Back
};

class Game {
public:
    explicit Game(Audio& a);
    void update(float dt, const Input& in);
    void render();

    bool wantQuit = false;
    bool autopilot = false;   // dev/test: drives the car itself (RW_AUTOPILOT=1)
    std::string padName;
    bool padConnected = false;

    struct Rumble { float strong = 0, weak = 0; int ms = 0; bool pending = false; } rumbleReq;

private:
    enum class State { Title, Playing, Paused, GameOver };
    enum Kind { K_PLAYER, K_CIV, K_TRUCK, K_RAMMER, K_ARMOR };

    struct Car {
        int kind = K_CIV;
        float x = 0; double y = 0;
        float vx = 0, push = 0, speed = 0, cruise = 0, lane = 0.5f;
        float w = 22, h = 38, mass = 1;
        int hp = 1;
        float flash = 0, credit = 0, t = 0, life = 0, mineCd = 0, laneTimer = 0;
        float side = 1;
        Color color{1, 1, 1, 1};
        bool dead = false;
    };
    struct Bullet { float x; double y; float vx, vy; bool dead = false; };
    struct Mine { float x; double y; float t = 0; bool dead = false; };
    struct Drone { float x; double y; float vx = 0; int hp = 5; float bombCd = 1.5f, life = 16, flash = 0, t = 0; bool leaving = false, dead = false; };
    struct Bomb { float x0; double y0; float x1; double y1; float t = 0, dur = 1.0f; bool dead = false; };
    struct Pickup { float x; double y; int type; float t = 0; bool dead = false; };
    struct Particle { float x; double y; float vx, vy, life, maxLife, size; Color c; };
    struct Ring { float x; double y; float r, maxR, life; Color c; };
    struct FloatText { std::string s; float x; double y; float life; Color c; };

    // flow
    void resetWorld();
    void startGame();
    void gameOver();
    void updateWorld(float dt, const Input* in);
    void controlPlayer(float dt, const Input& in);
    void respawnPlayer();
    void killPlayer(const char* why);
    void spawn(float dt, bool demo);
    bool spawnCar(int kind, bool ahead);
    void updateCars(float dt);
    void updateDrones(float dt);
    void updateProjectiles(float dt);
    void collideCars();
    void triggerEmp();
    void destroyCar(Car& c, bool byPlayer, bool bumped);
    void explode(float x, double y, float power, bool water);
    void addScore(int pts, float x, double y, Color c);
    void message(const std::string& s, Color c, float time = 2.f);
    void rumble(float strong, float weak, int ms);
    void loadHiscore();
    void saveHiscore();

    // drawing
    float sy(double y) const { return (float)(VH - (y - camY)); }
    void drawRoad();
    void drawCar(const Car& c, bool isPlayer);
    void drawHud();
    void drawTitle();

    float rnd(float a, float b);

    Audio& audio;
    State state = State::Title;
    Road road;
    uint32_t rng = 12345;
    double camY = 0;
    float anchor = 140;
    float clock = 0;

    Car player;
    bool playerAlive = false;
    float invuln = 0, shield = 0, spread = 0, deathTimer = 0, fireCd = 0;
    int lives = 0, emp = 0, level = 0;
    double score = 0;
    long hiscore = 0;
    long nextLife = 0;
    double nextPickupY = 0;
    float civTimer = 0, rammerTimer = 0, armorTimer = 0, droneTimer = 0;
    float bumpSoundCd = 0, stateTimer = 0, shake = 0;

    std::vector<Car> cars;
    std::vector<Bullet> bullets;
    std::vector<Mine> mines;
    std::vector<Drone> drones;
    std::vector<Bomb> bombs;
    std::vector<Pickup> pickups;
    std::vector<Particle> particles;
    std::vector<Ring> rings;
    std::vector<FloatText> floats;

    std::string msgText;
    Color msgColor{1, 1, 1, 1};
    float msgTime = 0;
};
