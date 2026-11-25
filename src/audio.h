#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstddef>

enum class Sfx { Gun, Explosion, BigExplosion, Bump, Pickup, Emp, BombDrop, Splash, Blip, ExtraLife, Hit, Mine };

// Tiny software synth streaming through PulseAudio (libpulse-simple, loaded
// at runtime). If PulseAudio isn't available the game simply runs silent.
class Audio {
public:
    ~Audio();
    bool start();
    void stop();
    void play(Sfx s, float vol = 1.f);
    void setEngine(float speed01, float vol) { engSpeed = speed01; engVol = vol; }
    bool ok() const { return running; }

private:
    struct Voice { Sfx s; float t = 0, dur = 0.1f, vol = 1, phase = 0, lp = 0, lp2 = 0; };
    void run();
    float voiceSample(Voice& v);
    float noise();

    void* lib = nullptr;
    void* stream = nullptr;
    std::thread th;
    std::atomic<bool> running{false};
    std::mutex m;
    std::vector<Voice> pending, voices;
    std::atomic<float> engSpeed{0.f}, engVol{0.f};
    float engPhase = 0, engPhase2 = 0, engLp = 0, engVolSmooth = 0, engSpeedSmooth = 0;
    uint32_t rng = 0x9E3779B9u;

    using FnWrite = int (*)(void*, const void*, size_t, int*);
    using FnFree = void (*)(void*);
    FnWrite paWrite = nullptr;
    FnFree paFree = nullptr;
};
