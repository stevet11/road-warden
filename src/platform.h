#pragma once
#include <unordered_set>

// Minimal X11 + GLX window with keyboard state.
class Platform {
public:
    bool init(int w, int h, const char* title);
    void shutdown();
    void pump();               // process pending window events
    void swap();
    void endFrame();           // clear per-frame key edges
    void toggleFullscreen();

    bool down(unsigned long keysym) const { return held.count(keysym) != 0; }
    bool pressed(unsigned long keysym) const { return latched.count(keysym) != 0; }

    bool quit = false;
    bool vsync = false;
    int width = 0, height = 0;

private:
    void* dpy = nullptr;       // Display*
    unsigned long win = 0;     // Window
    void* ctx = nullptr;       // GLXContext
    unsigned long wmDelete = 0;
    unsigned long colormap = 0;
    bool fullscreen = false;
    std::unordered_set<unsigned long> held, latched;
};
