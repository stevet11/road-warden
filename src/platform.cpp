#include "platform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <GL/glx.h>
#include <cstdio>
#include <cstring>

#define DPY (static_cast<Display*>(dpy))

bool Platform::init(int w, int h, const char* title) {
    Display* d = XOpenDisplay(nullptr);
    if (!d) { std::fprintf(stderr, "Cannot open X display\n"); return false; }
    dpy = d;
    int screen = DefaultScreen(d);

    int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                    GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 0, None };
    XVisualInfo* vi = glXChooseVisual(d, screen, attrs);
    if (!vi) { std::fprintf(stderr, "No suitable GLX visual\n"); return false; }

    Window root = RootWindow(d, screen);
    Colormap cmap = XCreateColormap(d, root, vi->visual, AllocNone);
    colormap = cmap;
    XSetWindowAttributes swa{};
    swa.colormap = cmap;
    swa.background_pixel = 0;
    swa.event_mask = KeyPressMask | KeyReleaseMask | StructureNotifyMask | FocusChangeMask | ExposureMask;
    Window wnd = XCreateWindow(d, root, 0, 0, w, h, 0, vi->depth, InputOutput, vi->visual,
                               CWColormap | CWEventMask | CWBackPixel, &swa);
    win = wnd;
    XStoreName(d, wnd, title);
    XClassHint ch; ch.res_name = const_cast<char*>("road_warden"); ch.res_class = const_cast<char*>("RoadWarden");
    XSetClassHint(d, wnd, &ch);

    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    wmDelete = del;
    XSetWMProtocols(d, wnd, &del, 1);

    Bool supported = False;
    XkbSetDetectableAutoRepeat(d, True, &supported);

    XMapWindow(d, wnd);

    GLXContext c = glXCreateContext(d, vi, nullptr, True);
    XFree(vi);
    if (!c) { std::fprintf(stderr, "glXCreateContext failed\n"); return false; }
    ctx = c;
    glXMakeCurrent(d, wnd, c);

    const char* ext = glXQueryExtensionsString(d, screen);
    if (ext && std::strstr(ext, "GLX_EXT_swap_control")) {
        auto fn = (void (*)(Display*, GLXDrawable, int))glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
        if (fn) { fn(d, wnd, 1); vsync = true; }
    } else if (ext && std::strstr(ext, "GLX_MESA_swap_control")) {
        auto fn = (int (*)(unsigned))glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
        if (fn) vsync = fn(1) == 0;
    }

    width = w; height = h;
    return true;
}

void Platform::shutdown() {
    if (!dpy) return;
    glXMakeCurrent(DPY, None, nullptr);
    if (ctx) glXDestroyContext(DPY, static_cast<GLXContext>(ctx));
    if (win) XDestroyWindow(DPY, win);
    if (colormap) XFreeColormap(DPY, colormap);
    XCloseDisplay(DPY);
    dpy = nullptr;
}

void Platform::pump() {
    Display* d = DPY;
    while (XPending(d)) {
        XEvent e;
        XNextEvent(d, &e);
        switch (e.type) {
        case KeyPress: {
            KeySym k = XLookupKeysym(&e.xkey, 0);
            if (!held.count(k)) latched.insert(k);
            held.insert(k);
            break;
        }
        case KeyRelease: {
            KeySym k = XLookupKeysym(&e.xkey, 0);
            held.erase(k);
            break;
        }
        case FocusOut:
            held.clear();
            break;
        case ConfigureNotify:
            width = e.xconfigure.width;
            height = e.xconfigure.height;
            break;
        case ClientMessage:
            if ((unsigned long)e.xclient.data.l[0] == wmDelete) quit = true;
            break;
        default: break;
        }
    }
}

void Platform::swap() { glXSwapBuffers(DPY, win); }

void Platform::endFrame() { latched.clear(); }

void Platform::toggleFullscreen() {
    Display* d = DPY;
    fullscreen = !fullscreen;
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = win;
    e.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = fullscreen ? 1 : 0;
    e.xclient.data.l[1] = (long)XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
    e.xclient.data.l[2] = 0;
    e.xclient.data.l[3] = 1;
    XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(d);
}
