#include "road.h"

#include <algorithm>
#include <cmath>

float Road::frand(float a, float b) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return a + (b - a) * (rng / 4294967295.f);
}

void Road::reset(uint32_t seed) {
    rows.clear();
    rng = seed ? seed : 1;
    base = -80;
    cx = scx = tcx = VW / 2;
    hw = shw = thw = 150;
    med = smed = tmed = 0;
    segLen = 80 + 260;     // straight, undivided opening stretch: ~2000px past the start line
    segPos = 0;
    terrain = T_GRASS;
    biomeRows = 1400;
    planned = 0;
    sinceBridge = 0;
}

void Road::nextSegment() {
    scx = cx; shw = hw; smed = med;
    segPos = 0;

    if (planned == 1) {                       // approach done -> bridge deck
        planned = 2;
        tcx = cx; thw = hw; tmed = 0;
        segLen = (int)frand(60, 110);
        return;
    }
    if (planned == 2) {                       // leave bridge with a short straight
        planned = 0;
        sinceBridge = 0;
        tcx = cx; thw = hw; tmed = 0;
        segLen = 24;
        return;
    }
    ++sinceBridge;

    float r = frand(0, 1);
    if (sinceBridge > 6 && r < 0.14f) {       // narrow to a single carriageway, then a bridge
        planned = 1;
        tmed = 0;
        thw = frand(95, 125);
        tcx = cx + frand(-60, 60);
    } else if (r < 0.40f) {                   // open or close a divided highway
        if (med > 0.5f) { tmed = 0; thw = frand(110, 170); }
        else { tmed = frand(35, 70); thw = tmed + frand(115, 150); }
        tcx = VW / 2 + frand(-120, 120);
    } else {                                  // curve / width change
        thw = frand(100, 210);
        tmed = (med > 0.5f && frand(0, 1) < 0.6f) ? std::min(med, thw - 110) : 0.f;
        if (tmed < 20) tmed = 0;
        tcx = VW / 2 + frand(-170, 170);
    }
    if (tmed > 0) thw = std::max(thw, tmed + 105);
    tcx = std::clamp(tcx, thw + 40, VW - thw - 40);

    // Keep slopes drivable: limit lateral drift per world pixel.
    int minLen = (int)std::max({std::fabs(tcx - scx) * 0.55f, std::fabs(thw - shw) * 0.5f,
                                std::fabs(tmed - smed) * 0.9f, 40.f});
    segLen = std::max(minLen, (int)frand(40, 110));
}

void Road::pushRow() {
    if (segPos >= segLen) nextSegment();
    float t = (float)segPos / segLen;
    float s = t * t * (3 - 2 * t);
    cx = scx + (tcx - scx) * s;
    hw = shw + (thw - shw) * s;
    med = smed + (tmed - smed) * s;
    ++segPos;
    rows.push_back({cx, hw, med, (uint8_t)(planned == 2 ? T_WATER : terrain)});
    if (--biomeRows <= 0 && planned != 2) {
        terrain = (terrain + 1) % 3;
        biomeRows = 1400;
    }
}

void Road::ensure(double topY) {
    long need = (long)std::ceil(topY / ROW_H) + 2;
    while (lastIndex() < need) pushRow();
}

void Road::trim(double bottomY) {
    long keep = (long)std::floor(bottomY / ROW_H);
    while (!rows.empty() && base < keep && rows.size() > 4) { rows.pop_front(); ++base; }
}

const RoadRow& Road::row(long i) const {
    long k = std::clamp(i - base, 0L, (long)rows.size() - 1);
    return rows[(size_t)k];
}

RoadRow Road::at(double y) const {
    double fi = y / ROW_H;
    long i0 = (long)std::floor(fi);
    float t = (float)(fi - i0);
    const RoadRow& a = row(i0);
    const RoadRow& b = row(i0 + 1);
    return {a.cx + (b.cx - a.cx) * t, a.hw + (b.hw - a.hw) * t, a.med + (b.med - a.med) * t, a.terrain};
}

float Road::laneX(double y, float f) const {
    RoadRow r = at(y);
    float s = f < 0 ? -1.f : 1.f;
    float af = std::fabs(f);
    return r.cx + s * (r.med + af * (r.hw - r.med));
}

bool Road::drivable(float x, double y, float margin) const {
    RoadRow r = at(y);
    float d = std::fabs(x - r.cx);
    if (d > r.hw - margin) return false;
    if (r.med > 0.5f && d < r.med + margin) return false;
    return true;
}

float Road::clampX(float x, double y, float margin) const {
    RoadRow r = at(y);
    x = std::clamp(x, r.cx - r.hw + margin, r.cx + r.hw - margin);
    if (r.med > 0.5f && x > r.cx - r.med - margin && x < r.cx + r.med + margin)
        x = (x < r.cx) ? r.cx - r.med - margin : r.cx + r.med + margin;
    return x;
}
