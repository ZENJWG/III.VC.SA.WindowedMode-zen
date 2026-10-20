#pragma once

#include <chrono>

// Reports measured frames per second once per elapsed second. A monotonic
// clock avoids timeGetTime's second-boundary jitter and 49-day wraparound.
// This is a display counter; it intentionally does not pace game frames.
class FpsCounter
{
public:
    using Clock = std::chrono::steady_clock;

    explicit FpsCounter(Clock::time_point start = Clock::now()) noexcept
        : periodStart(start) {}

    bool update() noexcept { return update(Clock::now()); }

    // Explicit timestamps make frame-rate calculations deterministic in tests.
    bool update(Clock::time_point now) noexcept
    {
        ++frames;
        const auto elapsed = std::chrono::duration<double>(now - periodStart).count();
        if (elapsed < 1.0) return false;

        fps = static_cast<unsigned int>(frames / elapsed + 0.5);
        frames = 0;
        periodStart = now;
        return true;
    }

    unsigned int get() const noexcept { return fps; }

private:
    Clock::time_point periodStart;
    unsigned int fps = 0;
    unsigned int frames = 0;
};
