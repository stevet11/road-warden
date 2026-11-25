#include "audio.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace {
constexpr int SR = 44100;
constexpr float TAU = 6.2831853f;

struct PaSampleSpec { int format; uint32_t rate; uint8_t channels; };
struct PaBufferAttr { uint32_t maxlength, tlength, prebuf, minreq, fragsize; };
constexpr int PA_SAMPLE_S16LE = 3;
constexpr int PA_STREAM_PLAYBACK = 1;
using FnNew = void* (*)(const char*, const char*, int, const char*, const char*,
                        const PaSampleSpec*, const void*, const PaBufferAttr*, int*);

float sq(float phase) { return std::fmod(phase, 1.f) < 0.5f ? 1.f : -1.f; }
}

Audio::~Audio() { stop(); }

bool Audio::start() {
    lib = dlopen("libpulse-simple.so.0", RTLD_NOW);
    if (!lib) { std::fprintf(stderr, "Audio: libpulse-simple not found, running silent\n"); return false; }
    auto paNew = (FnNew)dlsym(lib, "pa_simple_new");
    paWrite = (FnWrite)dlsym(lib, "pa_simple_write");
    paFree = (FnFree)dlsym(lib, "pa_simple_free");
    if (!paNew || !paWrite || !paFree) { dlclose(lib); lib = nullptr; return false; }

    PaSampleSpec spec{PA_SAMPLE_S16LE, SR, 1};
    PaBufferAttr attr{(uint32_t)-1, 4096, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};
    int err = 0;
    stream = paNew(nullptr, "Road Warden", PA_STREAM_PLAYBACK, nullptr, "game", &spec, nullptr, &attr, &err);
    if (!stream) {
        std::fprintf(stderr, "Audio: could not connect to PulseAudio (%d), running silent\n", err);
        dlclose(lib); lib = nullptr;
        return false;
    }
    running = true;
    th = std::thread(&Audio::run, this);
    return true;
}

void Audio::stop() {
    if (running) {
        running = false;
        if (th.joinable()) th.join();
    }
    if (stream) { paFree(stream); stream = nullptr; }
    if (lib) { dlclose(lib); lib = nullptr; }
}

void Audio::play(Sfx s, float vol) {
    if (!running) return;
    Voice v;
    v.s = s;
    v.vol = vol;
    switch (s) {
    case Sfx::Gun: v.dur = 0.07f; break;
    case Sfx::Explosion: v.dur = 0.9f; break;
    case Sfx::BigExplosion: v.dur = 1.8f; break;
    case Sfx::Bump: v.dur = 0.18f; break;
    case Sfx::Pickup: v.dur = 0.27f; break;
    case Sfx::Emp: v.dur = 0.8f; break;
    case Sfx::BombDrop: v.dur = 0.9f; break;
    case Sfx::Splash: v.dur = 0.6f; break;
    case Sfx::Blip: v.dur = 0.06f; break;
    case Sfx::ExtraLife: v.dur = 0.5f; break;
    case Sfx::Hit: v.dur = 0.05f; break;
    case Sfx::Mine: v.dur = 0.12f; break;
    }
    std::lock_guard<std::mutex> lk(m);
    if (pending.size() < 32) pending.push_back(v);
}

float Audio::noise() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng / 4294967295.f) * 2.f - 1.f;
}

float Audio::voiceSample(Voice& v) {
    const float dt = 1.f / SR;
    float u = v.t / v.dur;           // 0..1 progress
    float out = 0.f;
    switch (v.s) {
    case Sfx::Gun: {
        float f = 1200.f - 850.f * u;
        v.phase += f * dt;
        float env = (1 - u) * (1 - u);
        out = (sq(v.phase) * 0.45f + noise() * 0.35f) * env * 0.6f;
        break;
    }
    case Sfx::Hit: {
        v.phase += 260.f * dt;
        out = (sq(v.phase) * 0.5f + noise() * 0.5f) * (1 - u) * 0.6f;
        break;
    }
    case Sfx::Explosion:
    case Sfx::BigExplosion: {
        float a = 0.22f * (1 - u) + 0.015f;
        v.lp += (noise() - v.lp) * a;
        v.lp2 += (v.lp - v.lp2) * 0.3f;
        float env = std::pow(1 - u, 1.6f);
        out = v.lp2 * 3.2f * env;
        if (v.s == Sfx::BigExplosion) {
            v.phase += (55.f - 25.f * u) * dt;
            out += std::sin(v.phase * TAU) * 0.5f * env;
        }
        break;
    }
    case Sfx::Bump: {
        v.phase += (120.f - 70.f * u) * dt;
        out = std::sin(v.phase * TAU) * (1 - u) * 0.8f;
        if (v.t < 0.02f) out += noise() * 0.4f;
        break;
    }
    case Sfx::Pickup:
    case Sfx::ExtraLife: {
        static const float pk[] = {660.f, 880.f, 1320.f};
        static const float xl[] = {523.f, 659.f, 784.f, 1047.f, 1319.f};
        int n = v.s == Sfx::Pickup ? 3 : 5;
        const float* notes = v.s == Sfx::Pickup ? pk : xl;
        int idx = std::min(n - 1, (int)(u * n));
        v.phase += notes[idx] * dt;
        out = sq(v.phase) * 0.3f * (1 - 0.5f * u);
        break;
    }
    case Sfx::Emp: {
        float f = 1600.f * std::pow(0.04f, u);
        v.phase += f * dt;
        out = std::sin(v.phase * TAU) * (0.6f + 0.4f * std::sin(v.t * 70.f)) * (1 - u) * 0.7f;
        break;
    }
    case Sfx::BombDrop: {
        v.phase += (1700.f - 1000.f * u) * dt;
        out = std::sin(v.phase * TAU) * 0.18f * (0.3f + 0.7f * u);
        break;
    }
    case Sfx::Splash: {
        float n = noise();
        v.lp += (n - v.lp) * 0.5f;
        out = (n - v.lp) * (1 - u) * (1 - u) * 1.2f;
        break;
    }
    case Sfx::Blip: {
        v.phase += 880.f * dt;
        out = sq(v.phase) * 0.25f * (1 - u);
        break;
    }
    case Sfx::Mine: {
        v.phase += (400.f + 400.f * u) * dt;
        out = sq(v.phase) * 0.2f * (1 - u);
        break;
    }
    }
    v.t += dt;
    return out * v.vol;
}

void Audio::run() {
    constexpr int N = 512;
    int16_t buf[N];
    const float dt = 1.f / SR;
    while (running) {
        {
            std::lock_guard<std::mutex> lk(m);
            for (auto& p : pending) if (voices.size() < 48) voices.push_back(p);
            pending.clear();
        }
        float tgtVol = engVol.load(), tgtSpd = engSpeed.load();
        for (int i = 0; i < N; ++i) {
            engVolSmooth += (tgtVol - engVolSmooth) * 0.0005f;
            engSpeedSmooth += (tgtSpd - engSpeedSmooth) * 0.0008f;
            float f = 42.f + engSpeedSmooth * 120.f;
            engPhase += f * dt;
            engPhase2 += f * 1.505f * dt;
            float saw = (std::fmod(engPhase, 1.f) * 2.f - 1.f) * 0.6f + (std::fmod(engPhase2, 1.f) * 2.f - 1.f) * 0.4f;
            engLp += (saw - engLp) * 0.12f;
            float s = engLp * 0.35f * engVolSmooth;

            for (auto& v : voices) s += voiceSample(v);
            s = std::tanh(s * 0.9f);
            buf[i] = (int16_t)(s * 30000.f);
            if ((i & 63) == 63)
                voices.erase(std::remove_if(voices.begin(), voices.end(),
                                            [](const Voice& v) { return v.t >= v.dur; }),
                             voices.end());
        }
        int err = 0;
        if (paWrite(stream, buf, sizeof buf, &err) < 0) {
            std::fprintf(stderr, "Audio: write failed (%d), going silent\n", err);
            running = false;
        }
    }
}
