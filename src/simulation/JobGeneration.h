#pragma once

#include "../DeterministicRng.h"
#include "TileIndex.h"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace simulation {

struct GeneratedJob {
    float x;
    float y;
    int payout;
    int carType;
    int angle;
    int jobClass;
    int zone;
    int jobType;
    std::string description;
};

inline std::optional<GeneratedJob> generateJob(
    const std::vector<int>& tiles,
    int mapWidth,
    int mapHeight,
    int roadTile,
    int tileSize,
    float truckX,
    float truckY,
    int maxJobClass,
    float reputation,
    DeterministicRng& rng) {
    if (mapWidth <= 0 || mapHeight <= 0 || tileSize <= 0
        || tiles.size() != tileCount(mapWidth, mapHeight))
        return std::nullopt;

    static const char* cars[] = {
        "red sedan", "blue sedan", "green SUV", "pickup truck", "delivery van", "sports car"
    };
    static const char* streets[] = {
        "Main St", "Oak Ave", "2nd St", "Elm Rd",
        "Harbor Blvd", "Pine St", "Sunset Dr", "Industrial Way"
    };
    for (int attempt = 0; attempt < 300; ++attempt) {
        const int x = static_cast<int>(rng.bounded(static_cast<std::uint32_t>(mapWidth)));
        const int y = static_cast<int>(rng.bounded(static_cast<std::uint32_t>(mapHeight)));
        if (tiles[tileIndex(x, y, mapWidth)] != roadTile) continue;

        const float worldX = (x + 0.5f) * tileSize;
        const float worldY = (y + 0.5f) * tileSize;
        const float dx = worldX - truckX, dy = worldY - truckY;
        if (dx * dx + dy * dy < 90.0f * 90.0f) continue;

        const int zone = x < mapWidth / 3 ? 0 : (x < 2 * mapWidth / 3 ? 1 : 2);
        const int roll = static_cast<int>(rng.bounded(100));
        int jobClass = 0;
        if (maxJobClass > 0) {
            const int mediumThreshold = zone == 2 ? 45 : zone == 1 ? 65 : 85;
            const int heavyThreshold = zone == 2 ? 75 : zone == 1 ? 90 : 97;
            jobClass = roll < mediumThreshold ? 0
                : roll < heavyThreshold ? 1 : 2;
            if (maxJobClass == 1 && jobClass == 2) jobClass = 1;
        }

        int payout = jobClass == 0 ? 75 + static_cast<int>(rng.bounded(101))
            : jobClass == 1 ? 200 + static_cast<int>(rng.bounded(101))
            : 500 + static_cast<int>(rng.bounded(501));
        if (zone == 2) payout = static_cast<int>(payout * 1.15f);

        int jobType = 0;
        const int typeRoll = static_cast<int>(rng.bounded(100));
        if (typeRoll < 8) {
            jobType = 1;
            payout = static_cast<int>(payout * 1.4f);
        } else if (typeRoll < 14 && reputation >= 55.0f) {
            jobType = 2;
            payout = static_cast<int>(payout * 1.5f);
        }

        const int carType = static_cast<int>(rng.bounded(6));
        const int angle = static_cast<int>(rng.bounded(4)) * 90;
        const char* prefixes[] = {"", "police impound - ", "repossession - "};
        std::ostringstream description;
        description << prefixes[jobType] << cars[carType] << " on "
                    << streets[rng.bounded(8)];
        return GeneratedJob{
            worldX, worldY, payout, carType, angle, jobClass, zone, jobType,
            description.str()
        };
    }
    return std::nullopt;
}

}
