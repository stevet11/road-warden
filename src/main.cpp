#include <X11/keysym.h>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <thread>

#include "audio.h"
#include "game.h"
#include "gamepad.h"
#include "platform.h"
#include "render.h"

int main() {
    Platform plat;
    if (!plat.init(1200, 900, "Road Warden")) return 1;

    Audio audio;
    audio.start();
    Gamepad pad;
    Game game(audio);
    game.autopilot = std::getenv("RW_AUTOPILOT") != nullptr;
    // RW_NO_RUMBLE=1 disables force feedback; autopilot test runs never rumble.
    const bool rumbleOn = !std::getenv("RW_NO_RUMBLE") && !game.autopilot;

    using clock = std::chrono::steady_clock;
    const double step = 1.0 / 120.0;
    auto t0 = clock::now();
    auto last = t0;
    double acc = 0;

    while (!plat.quit && !game.wantQuit) {
        auto frameStart = clock::now();
        double now = std::chrono::duration<double>(frameStart - t0).count();
        double frame = std::chrono::duration<double>(frameStart - last).count();
        last = frameStart;
        acc += std::fmin(frame, 0.1);

        plat.pump();
        pad.update(now);

        // ---- build input from keyboard + gamepad
        Input in;
        float kx = (plat.down(XK_Right) || plat.down(XK_d) ? 1.f : 0.f) - (plat.down(XK_Left) || plat.down(XK_a) ? 1.f : 0.f);
        float px = pad.stickX();
        in.steer = std::fabs(px) > std::fabs(kx) ? px : kx;

        float py = pad.stickY();
        in.throttle = std::fmax(pad.rightTrigger(), std::fmax(-py, 0.f));
        if (plat.down(XK_Up) || plat.down(XK_w)) in.throttle = 1.f;
        in.brake = std::fmax(pad.leftTrigger(), std::fmax(py, 0.f));
        if (plat.down(XK_Down) || plat.down(XK_s)) in.brake = 1.f;

        in.fire = plat.down(XK_space) || plat.down(XK_z) || plat.down(XK_j) ||
                  pad.down(Gamepad::A) || pad.down(Gamepad::RB);
        in.emp = plat.pressed(XK_x) || plat.pressed(XK_Shift_L) || plat.pressed(XK_k) ||
                 pad.pressed(Gamepad::B) || pad.pressed(Gamepad::LB) || pad.pressed(Gamepad::Y);
        in.confirm = plat.pressed(XK_Return) || plat.pressed(XK_KP_Enter) || plat.pressed(XK_space) ||
                     pad.pressed(Gamepad::A) || pad.pressed(Gamepad::Start);
        in.pause = plat.pressed(XK_p) || pad.pressed(Gamepad::Start);
        in.back = plat.pressed(XK_Escape) || pad.pressed(Gamepad::Back);

        game.padConnected = pad.connected();
        game.padName = pad.name();

        // ---- fixed-step simulation (edges only delivered to the first step)
        bool first = true;
        while (acc >= step) {
            game.update((float)step, in);
            if (first) {
                first = false;
                in.emp = in.confirm = in.pause = in.back = false;
            }
            acc -= step;
        }
        if (game.rumbleReq.pending) {
            if (rumbleOn) pad.rumble(game.rumbleReq.strong, game.rumbleReq.weak, game.rumbleReq.ms);
            game.rumbleReq.pending = false;
        }

        if (!first) {   // edges consumed by a sim step; otherwise keep them for next frame
            if (plat.pressed(XK_F11)) plat.toggleFullscreen();
            plat.endFrame();
            pad.endFrame();
        }

        gfx::begin(plat.width, plat.height, VW, VH);
        game.render();
        plat.swap();

        // frame cap in case vsync is unavailable
        auto target = frameStart + std::chrono::microseconds(1000000 / 240);
        if (clock::now() < target) std::this_thread::sleep_until(target);
    }

    audio.stop();
    plat.shutdown();
    return 0;
}
