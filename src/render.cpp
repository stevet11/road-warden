#include "render.h"

#include <GL/gl.h>
#include <cctype>
#include <cmath>

namespace {
struct Glyph { char ch; const char* rows[7]; };

const Glyph FONT[] = {
    {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
    {'C', {" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "}},
    {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
    {'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
    {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
    {'G', {" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"}},
    {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'I', {" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'J', {"  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "}},
    {'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
    {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
    {'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
    {'N', {"#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"}},
    {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
    {'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
    {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
    {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
    {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "}},
    {'X', {"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}},
    {'Y', {"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
    {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
    {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
    {'3', {"#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "}},
    {'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
    {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
    {'6', {"  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "}},
    {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
    {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
    {'9', {" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "}},
    {':', {"     ", "  #  ", "  #  ", "     ", "  #  ", "  #  ", "     "}},
    {'-', {"     ", "     ", "     ", " ### ", "     ", "     ", "     "}},
    {'!', {"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "}},
    {'.', {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}},
    {'/', {"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}},
    {'+', {"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}},
    {'?', {" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "}},
    {',', {"     ", "     ", "     ", "     ", " ##  ", "  #  ", " #   "}},
    {'(', {"   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "}},
    {')', {" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "}},
    {'\'', {"  #  ", "  #  ", " #   ", "     ", "     ", "     ", "     "}},
    {'>', {" #   ", "  #  ", "   # ", "    #", "   # ", "  #  ", " #   "}},
    {'<', {"   # ", "  #  ", " #   ", "#    ", " #   ", "  #  ", "   # "}},
};

const Glyph* find(char c) {
    c = (char)std::toupper((unsigned char)c);
    for (const auto& g : FONT) if (g.ch == c) return &g;
    return nullptr;
}

inline void col(Color c) { glColor4f(c.r, c.g, c.b, c.a); }
}

namespace gfx {

void begin(int winW, int winH, float viewW, float viewH) {
    glViewport(0, 0, winW, winH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    float scale = std::fmin(winW / viewW, winH / viewH);
    int vw = (int)(viewW * scale), vh = (int)(viewH * scale);
    glViewport((winW - vw) / 2, (winH - vh) / 2, vw, vh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, viewW, viewH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void rect(float x, float y, float w, float h, Color c) {
    col(c);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
    col(c);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x2, y2); glVertex2f(x3, y3);
    glEnd();
}

void tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    col(c);
    glBegin(GL_TRIANGLES);
    glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
}

void circle(float x, float y, float r, Color c, int seg) {
    col(c);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= seg; ++i) {
        float a = i * 6.2831853f / seg;
        glVertex2f(x + std::cos(a) * r, y + std::sin(a) * r);
    }
    glEnd();
}

void ring(float x, float y, float r, float thick, Color c, int seg) {
    col(c);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= seg; ++i) {
        float a = i * 6.2831853f / seg;
        float cs = std::cos(a), sn = std::sin(a);
        glVertex2f(x + cs * r, y + sn * r);
        glVertex2f(x + cs * (r + thick), y + sn * (r + thick));
    }
    glEnd();
}

void push(float x, float y, float angleDeg) {
    glPushMatrix();
    glTranslatef(x, y, 0);
    glRotatef(angleDeg, 0, 0, 1);
}
void pop() { glPopMatrix(); }

float textWidth(const std::string& s, float scale) {
    return s.empty() ? 0.f : (s.size() * 6 - 1) * scale;
}

void text(const std::string& s, float x, float y, float scale, Color c, Align a) {
    float w = textWidth(s, scale);
    if (a == Center) x -= w * 0.5f;
    else if (a == Right) x -= w;
    col(c);
    glBegin(GL_QUADS);
    for (char ch : s) {
        if (const Glyph* g = find(ch)) {
            for (int r = 0; r < 7; ++r)
                for (int k = 0; k < 5; ++k)
                    if (g->rows[r][k] == '#') {
                        float px = x + k * scale, py = y + r * scale;
                        glVertex2f(px, py); glVertex2f(px + scale, py);
                        glVertex2f(px + scale, py + scale); glVertex2f(px, py + scale);
                    }
        }
        x += 6 * scale;
    }
    glEnd();
}

void textShadow(const std::string& s, float x, float y, float scale, Color c, Align a) {
    float o = std::fmax(1.f, scale * 0.5f);
    text(s, x + o, y + o, scale, {0, 0, 0, c.a * 0.7f}, a);
    text(s, x, y, scale, c, a);
}

}  // namespace gfx
