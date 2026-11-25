#include "gamepad.h"

#include <linux/joystick.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

Gamepad::~Gamepad() { close(); }

void Gamepad::close() {
    if (ffd >= 0) ::close(ffd);
    if (fd >= 0) ::close(fd);
    fd = ffd = effectId = -1;
    std::memset(axes, 0, sizeof axes);
    std::memset(buttons, 0, sizeof buttons);
    std::memset(latch, 0, sizeof latch);
    std::memset(trigReady, 0, sizeof trigReady);
}

static bool contains(const std::string& s, const char* sub) {
    std::string a = s, b = sub;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a.find(b) != std::string::npos;
}

void Gamepad::scan() {
    for (int i = 0; i < 8; ++i) {
        char path[32];
        std::snprintf(path, sizeof path, "/dev/input/js%d", i);
        int f = ::open(path, O_RDONLY | O_NONBLOCK);
        if (f < 0) continue;
        char nm[128] = "Unknown gamepad";
        ioctl(f, JSIOCGNAME(sizeof nm), nm);
        unsigned char na = 0, nb = 0;
        ioctl(f, JSIOCGAXES, &na);
        ioctl(f, JSIOCGBUTTONS, &nb);
        fd = f;
        devName = nm;
        nAxes = std::min<int>(na, 32);
        nButtons = std::min<int>(nb, 64);

        // Layout defaults: Xbox-style (xpad / xinput-compatible pads).
        int xbox[BtnCount] = {0, 1, 2, 3, 4, 5, 6, 7};
        // Sony (hid-sony / hid-playstation): X O Tri Sq L1 R1 L2 R2 Share Options
        int sony[BtnCount] = {0, 1, 3, 2, 4, 5, 8, 9};
        // Generic HID pads: guess 0..3 face, 4/5 shoulders, 8/9 select/start
        int generic[BtnCount] = {1, 2, 0, 3, 4, 5, 8, 9};
        const int* m = xbox;
        if (contains(devName, "sony") || contains(devName, "playstation") || contains(devName, "wireless controller"))
            m = sony;
        else if (!(contains(devName, "x-box") || contains(devName, "xbox") || contains(devName, "xinput") ||
                   contains(devName, "8bitdo") || contains(devName, "logitech gamepad f")))
            m = (nButtons >= 10) ? generic : xbox;
        std::copy(m, m + BtnCount, btnMap);

        // Axis layout: LX LY LT RX RY RT HatX HatY (xpad, hid-playstation).
        trigL = trigR = hatX = hatY = -1;
        if (nAxes >= 8) { trigL = 2; trigR = 5; hatX = 6; hatY = 7; }
        else if (nAxes >= 6) { trigL = 2; trigR = 5; }
        else if (nAxes == 4) { hatX = 2; hatY = 3; }   // simple pads: stick + hat

        openRumble(i);
        std::fprintf(stderr, "Gamepad connected: %s (%d axes, %d buttons)%s\n", nm, nAxes, nButtons,
                     ffd >= 0 ? " [rumble]" : "");
        return;
    }
}

void Gamepad::openRumble(int jsIndex) {
    char dirPath[64];
    std::snprintf(dirPath, sizeof dirPath, "/sys/class/input/js%d/device", jsIndex);
    DIR* d = opendir(dirPath);
    if (!d) return;
    while (dirent* ent = readdir(d)) {
        if (std::strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[300];
        std::snprintf(path, sizeof path, "/dev/input/%s", ent->d_name);
        int f = ::open(path, O_RDWR | O_NONBLOCK);
        if (f < 0) continue;
        unsigned long ffbits[(FF_CNT + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = {};
        if (ioctl(f, EVIOCGBIT(EV_FF, sizeof ffbits), ffbits) >= 0 &&
            (ffbits[FF_RUMBLE / (8 * sizeof(long))] >> (FF_RUMBLE % (8 * sizeof(long)))) & 1) {
            ffd = f;
            break;
        }
        ::close(f);
    }
    closedir(d);
}

void Gamepad::update(double now) {
    if (fd < 0) {
        if (now - lastScan > 1.5) { lastScan = now; scan(); }
        return;
    }
    js_event e;
    for (;;) {
        ssize_t n = ::read(fd, &e, sizeof e);
        if (n == (ssize_t)sizeof e) {
            bool init = e.type & JS_EVENT_INIT;
            int type = e.type & ~JS_EVENT_INIT;
            if (type == JS_EVENT_BUTTON && e.number < 64) {
                bool v = e.value != 0;
                if (v && !buttons[e.number] && !init) latch[e.number] = true;
                buttons[e.number] = v;
            } else if (type == JS_EVENT_AXIS && e.number < 32) {
                axes[e.number] = e.value;
                // Triggers rest at -32767; don't trust them until seen at rest
                // (some drivers report 0 before first touch => phantom half-press).
                if (e.value < -16000) trigReady[e.number] = true;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
        std::fprintf(stderr, "Gamepad disconnected\n");
        close();
        lastScan = now;
        break;
    }
}

void Gamepad::endFrame() { std::memset(latch, 0, sizeof latch); }

bool Gamepad::down(Btn b) const {
    int i = map(b);
    return fd >= 0 && i >= 0 && i < nButtons && buttons[i];
}
bool Gamepad::pressed(Btn b) const {
    int i = map(b);
    return fd >= 0 && i >= 0 && i < nButtons && latch[i];
}

float Gamepad::axisNorm(int a) const {
    if (fd < 0 || a < 0 || a >= nAxes) return 0.f;
    float v = axes[a] / 32767.f;
    const float dz = 0.2f;
    float m = std::fabs(v);
    if (m < dz) return 0.f;
    return std::copysign(std::min(1.f, (m - dz) / (1.f - dz)), v);
}

float Gamepad::trigger(int a) const {
    if (fd < 0 || a < 0 || a >= nAxes || !trigReady[a]) return 0.f;
    float v = (axes[a] + 32767.f) / 65534.f;
    return v < 0.05f ? 0.f : std::min(1.f, v);
}

float Gamepad::stickX() const {
    float s = axisNorm(0);
    float h = axisNorm(hatX);
    return std::fabs(h) > std::fabs(s) ? h : s;
}
float Gamepad::stickY() const {
    float s = axisNorm(1);
    float h = axisNorm(hatY);
    return std::fabs(h) > std::fabs(s) ? h : s;
}
float Gamepad::leftTrigger() const { return trigger(trigL); }
float Gamepad::rightTrigger() const { return trigger(trigR); }

void Gamepad::rumble(float strong, float weak, int ms) {
    if (ffd < 0) return;
    ff_effect fx{};
    fx.type = FF_RUMBLE;
    fx.id = (short)effectId;
    fx.u.rumble.strong_magnitude = (uint16_t)(std::clamp(strong, 0.f, 1.f) * 0xFFFF);
    fx.u.rumble.weak_magnitude = (uint16_t)(std::clamp(weak, 0.f, 1.f) * 0xFFFF);
    fx.replay.length = (uint16_t)ms;
    if (ioctl(ffd, EVIOCSFF, &fx) < 0) {
        effectId = -1;
        return;
    }
    effectId = fx.id;
    input_event play{};
    play.type = EV_FF;
    play.code = (uint16_t)effectId;
    play.value = 1;
    if (::write(ffd, &play, sizeof play) < 0) { /* ignore */ }
}
