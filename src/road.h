#pragma once
#include <cstdint>
#include <deque>

constexpr float VW = 800.f, VH = 600.f;   // logical view size

enum Terrain : uint8_t { T_GRASS, T_DESERT, T_SNOW, T_WATER };

struct RoadRow {
    float cx, hw, med;   // centre x, half width, median half width (0 = undivided)
    uint8_t terrain;
};

// Procedurally generated, endlessly scrolling road. World y increases "up the screen".
class Road {
public:
    static constexpr float ROW_H = 8.f;

    void reset(uint32_t seed);
    void ensure(double topY);          // generate rows up to this world y
    void trim(double bottomY);         // drop rows below this world y

    RoadRow at(double y) const;        // interpolated
    const RoadRow& row(long i) const;  // absolute row index (clamped)
    long firstIndex() const { return base; }
    long lastIndex() const { return base + (long)rows.size() - 1; }

    float laneX(double y, float f) const;               // f in [-1,1] across the carriageways
    bool drivable(float x, double y, float margin) const;
    float clampX(float x, double y, float margin) const;

private:
    void pushRow();
    void nextSegment();
    float frand(float a, float b);

    std::deque<RoadRow> rows;
    long base = 0;
    float cx = 400, hw = 150, med = 0;
    float scx = 400, shw = 150, smed = 0;
    float tcx = 400, thw = 150, tmed = 0;
    int segLen = 1, segPos = 0;
    int terrain = T_GRASS, biomeRows = 0;
    int planned = 0;      // 0 none, 1 approaching bridge, 2 on bridge
    int sinceBridge = 0;
    uint32_t rng = 1;
};
