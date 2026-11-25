#pragma once
#include <cstdint>
#include <string>

// USB gamepad via the Linux joystick API (/dev/input/jsN), with
// hot-plugging and force-feedback rumble through the matching evdev node.
class Gamepad {
public:
    enum Btn { A, B, X, Y, LB, RB, Back, Start, BtnCount };

    ~Gamepad();
    void update(double now);          // read pending events / rescan
    void endFrame();                  // clear press edges

    bool connected() const { return fd >= 0; }
    const std::string& name() const { return devName; }

    bool down(Btn b) const;
    bool pressed(Btn b) const;

    float stickX() const;             // -1..1, dead-zoned (stick or d-pad)
    float stickY() const;             // -1..1, up is negative
    float leftTrigger() const;        // 0..1
    float rightTrigger() const;       // 0..1

    void rumble(float strong, float weak, int ms);

private:
    void scan();
    void close();
    void openRumble(int jsIndex);
    float axisNorm(int a) const;
    float trigger(int a) const;
    int map(Btn b) const { return btnMap[b]; }

    int fd = -1, ffd = -1, effectId = -1;
    int nAxes = 0, nButtons = 0;
    std::string devName;
    int16_t axes[32] = {};
    bool buttons[64] = {};
    bool latch[64] = {};
    bool trigReady[32] = {};
    int btnMap[BtnCount] = {0, 1, 2, 3, 4, 5, 6, 7};
    int trigL = -1, trigR = -1, hatX = -1, hatY = -1;
    double lastScan = -10.0;
};
