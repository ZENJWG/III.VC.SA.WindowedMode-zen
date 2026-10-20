#include "../source/ResolutionPolicy.h"
#include <cassert>

int main()
{
    using namespace ResolutionPolicy;
    static_assert(DesktopAuto(0, 0));
    static_assert(!DesktopAuto(3840, 2160));
    static_assert(!DesktopAuto(0, 2160));
    static_assert(!DesktopAuto(3840, 0));

    constexpr Extent valid{3840, 2160};
    constexpr Extent previous{640, 448};
    auto chosen = ValidClientOrFallback(valid, previous);
    assert(chosen.width == 3840 && chosen.height == 2160);

    // Minimize, 0-sized Win32 client, and failed GetClientRect.
    chosen = ValidClientOrFallback({0, 0}, valid);
    assert(chosen.width == 3840 && chosen.height == 2160);
    chosen = ValidClientOrFallback({0, 2160}, valid);
    assert(chosen.width == 3840 && chosen.height == 2160);
    chosen = ValidClientOrFallback({3840, 0}, valid);
    assert(chosen.width == 3840 && chosen.height == 2160);

    // Broken INIs must not silently create a narrow low-res render target.
    chosen = NormalizeRequested({0, 1080}, {160, 112});
    assert(DesktopAuto(chosen.width, chosen.height));
    chosen = NormalizeRequested({-1, 2160}, {160, 112});
    assert(DesktopAuto(chosen.width, chosen.height));
    chosen = NormalizeRequested({3840, 2160}, {160, 112});
    assert(chosen.width == 3840 && chosen.height == 2160);
    chosen = NormalizeRequested({999999, 999999}, {160, 112});
    assert(chosen.width == 16384 && chosen.height == 16384);
    chosen = NormalizeRequested({10, 20}, {160, 112});
    assert(chosen.width == 160 && chosen.height == 112);

    // Reset follows the real 4K client instead of old RenderWare parameters.
    chosen = ResetExtent({3840, 2160}, {1920, 1080}, {640, 448}, {640, 448});
    assert(chosen.width == 3840 && chosen.height == 2160);
    // Minimized client: keep previous native resolution, never reset to 0x0.
    chosen = ResetExtent({0, 0}, {3840, 2160}, {640, 448}, {640, 448});
    assert(chosen.width == 3840 && chosen.height == 2160);
    chosen = ResetExtent({0, 0}, {0, 0}, {1920, 1080}, {640, 448});
    assert(chosen.width == 1920 && chosen.height == 1080);
    chosen = ResetExtent({0, 0}, {0, 0}, {0, 0}, {640, 448});
    assert(chosen.width == 640 && chosen.height == 448);

    assert(!ShouldNotifyRenderer({3840, 2160}, {3840, 2160}));
    assert(!ShouldNotifyRenderer({3840, 2160}, {0, 0}));
    assert(ShouldNotifyRenderer({3840, 2160}, {2560, 1440}));
}
