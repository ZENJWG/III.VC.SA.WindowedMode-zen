// Regression: Windows headers may define min(a,b) as a function-like macro.
// Compile this under GCC/Clang and MSVC to protect the D3D reset expression.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include "../source/ResolutionPolicy.h"

// Simulate the win32 min macro after the standard headers are loaded.
#define min(a,b) broken_windows_min_macro_should_not_expand

struct D3D8MockParams
{
    unsigned long BackBufferWidth;
    unsigned long BackBufferHeight;
};
struct D3D9MockParams
{
    unsigned int BackBufferWidth;
    unsigned int BackBufferHeight;
};

template <typename T>
ResolutionPolicy::Extent ResetRequested(T parameters)
{
    constexpr auto maxBackBufferDimension = decltype(parameters.BackBufferWidth){16384u};
    const auto size = ResolutionPolicy::ResetExtent(
        {0, 0}, {0, 0},
        {static_cast<int32_t>((std::min)(parameters.BackBufferWidth, maxBackBufferDimension)),
         static_cast<int32_t>((std::min)(parameters.BackBufferHeight, maxBackBufferDimension))},
        {640, 480});
    return size;
}

int main()
{
    const auto d3d8 = ResetRequested(D3D8MockParams{3840, 2160});
    assert(d3d8.width == 3840 && d3d8.height == 2160);
    const auto d3d9 = ResetRequested(D3D9MockParams{UINT32_MAX, 720});
    assert(d3d9.width == 16384 && d3d9.height == 720);
    const auto fallback = ResetRequested(D3D9MockParams{0, 0});
    assert(fallback.width == 640 && fallback.height == 480);
}
