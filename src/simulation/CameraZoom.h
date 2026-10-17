#pragma once

#include <algorithm>

namespace simulation {

inline float adjustCameraZoom(float current, int wheelOrKeySteps) {
    constexpr float minimumZoom = 0.75f;
    constexpr float maximumZoom = 5.0f;
    const float factor = wheelOrKeySteps > 0 ? 1.1f : 1.0f / 1.1f;
    float zoom = current;
    const int count = wheelOrKeySteps > 0 ? wheelOrKeySteps : -wheelOrKeySteps;
    for (int i = 0; i < count; ++i) zoom *= factor;
    return std::clamp(zoom, minimumZoom, maximumZoom);
}

}
