#pragma once
#include <string>

struct Color { float r, g, b, a; };

inline Color rgb(unsigned hex, float a = 1.f) {
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a};
}
inline Color withAlpha(Color c, float a) { c.a = a; return c; }
inline Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

namespace gfx {
void begin(int winW, int winH, float viewW, float viewH);   // letterboxed ortho
void rect(float x, float y, float w, float h, Color c);
void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c);
void tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c);
void circle(float x, float y, float r, Color c, int seg = 18);
void ring(float x, float y, float r, float thick, Color c, int seg = 40);
void push(float x, float y, float angleDeg);
void pop();

enum Align { Left, Center, Right };
float textWidth(const std::string& s, float scale);
void text(const std::string& s, float x, float y, float scale, Color c, Align a = Left);
void textShadow(const std::string& s, float x, float y, float scale, Color c, Align a = Left);
}
