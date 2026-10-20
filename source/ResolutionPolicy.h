#pragma once
#include <cstdint>

namespace ResolutionPolicy
{
    struct Extent { int32_t width; int32_t height; };

    constexpr bool DesktopAuto(int width, int height) noexcept
    {
        return width == 0 && height == 0;
    }

    constexpr bool Valid(Extent size) noexcept
    {
        return size.width > 0 && size.height > 0;
    }

    // Treat incomplete (0xN) or negative INI extents as desktop-auto rather
    // than a tiny client or an overflow. Large dimensions are bounded before
    // Win32 frame-size arithmetic and Direct3D allocations.
    constexpr Extent NormalizeRequested(Extent configured, Extent minimum,
                                        int32_t maximum = 16384) noexcept
    {
        if (!Valid(configured)) return {0, 0};
        return {
            configured.width < minimum.width ? minimum.width :
                configured.width > maximum ? maximum : configured.width,
            configured.height < minimum.height ? minimum.height :
                configured.height > maximum ? maximum : configured.height
        };
    }

    // Never feed minimized/0x0 window client sizes to Direct3D Reset.
    // Preserve the last known physical window client size if possible.
    constexpr Extent ValidClientOrFallback(Extent client, Extent lastValid) noexcept
    {
        return Valid(client) ? client : lastValid;
    }

    // Prefer the physical HWND client, then the last working buffer. Only as
    // a last resort use the device's requested size (e.g. while minimized).
    constexpr Extent ResetExtent(Extent client, Extent lastWorking,
                                 Extent previousRequested, Extent fallback) noexcept
    {
        return Valid(client) ? client :
               Valid(lastWorking) ? lastWorking :
               Valid(previousRequested) ? previousRequested : fallback;
    }

    constexpr bool ShouldNotifyRenderer(Extent previous, Extent current) noexcept
    {
        return Valid(current) &&
            (current.width != previous.width || current.height != previous.height);
    }
}
