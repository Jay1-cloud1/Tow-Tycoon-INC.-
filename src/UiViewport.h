#pragma once

#include <algorithm>
#include <cmath>

struct UiViewport {
    int width;
    int height;
};

inline UiViewport makeUiViewport(int outputWidth, int outputHeight) {
    constexpr int baseWidth = 1280;
    constexpr int baseHeight = 720;
    constexpr int minLandscapeHeight = 560;
    if (outputWidth <= 0 || outputHeight <= 0) return {baseWidth, baseHeight};

    const double aspect = static_cast<double>(outputWidth) / outputHeight;
    const int height = std::clamp(
        static_cast<int>(std::lround(baseWidth / aspect)),
        minLandscapeHeight,
        baseHeight);
    return {baseWidth, height};
}
