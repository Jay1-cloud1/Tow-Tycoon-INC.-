#pragma once

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace simulation {

inline std::vector<std::pair<int, int>> findRoadPath(
    const std::vector<int>& tiles,
    int width,
    int height,
    int roadTile,
    int startX,
    int startY,
    int goalX,
    int goalY) {
    if (width <= 0 || height <= 0 || tiles.size() != static_cast<std::size_t>(width * height))
        return {};

    auto isRoad = [&](int x, int y) {
        return x >= 0 && x < width && y >= 0 && y < height
            && tiles[static_cast<std::size_t>(y * width + x)] == roadTile;
    };
    auto snap = [&](int& x, int& y) {
        if (isRoad(x, y)) return;
        int best = INT_MAX, bestX = x, bestY = y;
        for (int radius = 1; radius < 8 && best == INT_MAX; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (isRoad(nx, ny)) {
                        const int distance = dx * dx + dy * dy;
                        if (distance < best) {
                            best = distance;
                            bestX = nx;
                            bestY = ny;
                        }
                    }
                }
            }
        }
        x = bestX;
        y = bestY;
    };

    snap(startX, startY);
    snap(goalX, goalY);
    if (!isRoad(startX, startY) || !isRoad(goalX, goalY)) return {};

    const int nodeCount = width * height;
    std::vector<int> cameFrom(static_cast<std::size_t>(nodeCount), -1);
    std::vector<int> cost(static_cast<std::size_t>(nodeCount), INT_MAX);
    auto id = [width](int x, int y) { return y * width + x; };
    using QueueItem = std::pair<int, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> frontier;

    const int start = id(startX, startY);
    const int goal = id(goalX, goalY);
    cost[start] = 0;
    frontier.push({std::abs(startX - goalX) + std::abs(startY - goalY), start});
    constexpr int dx[] = {1, -1, 0, 0};
    constexpr int dy[] = {0, 0, 1, -1};

    while (!frontier.empty()) {
        const int current = frontier.top().second;
        frontier.pop();
        if (current == goal) break;
        const int x = current % width, y = current / width;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + dx[direction], ny = y + dy[direction];
            if (!isRoad(nx, ny)) continue;
            const int next = id(nx, ny), nextCost = cost[current] + 1;
            if (nextCost < cost[next]) {
                cost[next] = nextCost;
                cameFrom[next] = current;
                frontier.push({nextCost + std::abs(nx - goalX) + std::abs(ny - goalY), next});
            }
        }
    }

    if (goal != start && cameFrom[goal] == -1) return {};
    std::vector<std::pair<int, int>> path;
    for (int current = goal; current != -1; current = cameFrom[current])
        path.push_back({current % width, current / width});
    std::reverse(path.begin(), path.end());
    return path;
}

}
