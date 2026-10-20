#include "../source/FpsCounter.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main()
{
    using Clock = FpsCounter::Clock;
    using namespace std::chrono_literals;
    const Clock::time_point start{};
    FpsCounter counter(start);
    assert(counter.get() == 0);

    // 60 frames spaced at ~16.7 ms, with an inclusive one-second window.
    for (int frame = 1; frame < 60; ++frame)
    {
        assert(!counter.update(start + frame * 16666667ns));
    }
    assert(counter.update(start + 1000ms));
    assert(counter.get() == 60);

    // The count and sampling start are reset together.
    for (int frame = 1; frame < 30; ++frame)
        assert(!counter.update(start + 1000ms + frame * 33333333ns));
    assert(counter.update(start + 2000ms));
    assert(counter.get() == 30);

    // A long frame measures real elapsed time (not just frame count).
    assert(counter.update(start + 4000ms));
    assert(counter.get() == 1); // 1 frame / 2 seconds rounds to 1 FPS

    // Two independent counters do not share timing or display state.
    FpsCounter other(start + 4000ms);
    assert(other.get() == 0);
    assert(other.update(start + 5000ms));
    assert(other.get() == 1);
    assert(counter.get() == 1);

    std::cout << "FPS counter regression tests passed\n";
    return 0;
}
