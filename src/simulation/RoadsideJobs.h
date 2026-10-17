#pragma once

#include "JobGeneration.h"

namespace simulation {

inline std::optional<GeneratedJob> generateRoadsideJob(
    const std::vector<int>& tiles,
    int mapWidth,
    int mapHeight,
    int roadTile,
    int tileSize,
    float truckX,
    float truckY,
    DeterministicRng& rng) {
    auto job = generateJob(tiles, mapWidth, mapHeight, roadTile, tileSize,
                           truckX, truckY, 0, 50.0f, rng);
    if (!job) return std::nullopt;

    static const char* services[] = {
        "jump-start needed", "flat tire assistance", "fuel delivery", "vehicle lockout"
    };
    job->payout = 85 + static_cast<int>(rng.bounded(66));
    job->jobType = 3;
    job->description = services[rng.bounded(4)];
    return job;
}

}
