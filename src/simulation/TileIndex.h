#pragma once

#include <cstddef>

namespace simulation {

// Tile-grid sizes and offsets computed in std::size_t, so a large map can't
// overflow int before the value is used as a container size or index.
inline std::size_t tileCount(int width, int height) {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

inline std::size_t tileIndex(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
        + static_cast<std::size_t>(x);
}

}
