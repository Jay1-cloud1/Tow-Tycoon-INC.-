#pragma once

#include "../DeterministicRng.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace simulation {

struct RivalLot {
    int owner;
    int baseRevenue;
    float incomeMultiplier;
};

struct RivalEconomyState {
    double cash;
    double reputation;
    double aggression;
    int maxLots;
    int personality;
    std::vector<RivalLot> lots;
};

inline void applyRivalWeeklyTurn(RivalEconomyState& state, DeterministicRng& rng) {
    int rivalLots = 0;
    int openLots = 0;
    for (const auto& lot : state.lots) {
        rivalLots += lot.owner == 1 ? 1 : 0;
        openLots += lot.owner == -1 ? 1 : 0;
    }

    const int availableCapacity = state.maxLots - rivalLots;
    if (availableCapacity > 0 && state.cash > 0.0) {
        const bool isFlush = state.cash > 10000.0;
        const bool isLowCash = state.cash < 3000.0;
        std::vector<int> openIndices;
        openIndices.reserve(static_cast<std::size_t>(openLots));
        for (int i = 0; i < static_cast<int>(state.lots.size()); ++i)
            if (state.lots[static_cast<std::size_t>(i)].owner == -1) openIndices.push_back(i);
        std::sort(openIndices.begin(), openIndices.end(), [&](int left, int right) {
            return state.lots[static_cast<std::size_t>(left)].baseRevenue
                > state.lots[static_cast<std::size_t>(right)].baseRevenue;
        });

        const int baseCapacity = (isFlush ? 2 : 1) + (state.personality == 0 ? 1 : 0);
        const int weeklyCapacity = std::min(availableCapacity, baseCapacity);
        int purchased = 0;
        for (int index : openIndices) {
            if (purchased >= weeklyCapacity) break;
            auto& lot = state.lots[static_cast<std::size_t>(index)];
            if (state.personality == 2 && lot.baseRevenue < 520) continue;
            const double minimumBid = lot.baseRevenue * 0.4;
            if (state.cash < minimumBid) continue;

            double bidRate;
            if (isLowCash) bidRate = minimumBid * (1.0 + 0.10 * (1.0 - state.aggression));
            else if (state.personality == 3) bidRate = minimumBid * 0.90;
            else if (state.personality == 0 && isFlush) bidRate = minimumBid * 0.95;
            else if (isFlush && state.aggression > 0.4) bidRate = minimumBid * 0.98;
            else bidRate = minimumBid * 1.02;
            bidRate *= 0.99 + static_cast<double>(rng.bounded(2001)) / 100000.0;
            state.cash -= bidRate;
            lot.owner = 1;
            lot.incomeMultiplier = 1.0f;
            ++purchased;
        }
    }

    rivalLots = 0;
    for (const auto& lot : state.lots) rivalLots += lot.owner == 1 ? 1 : 0;
    state.cash += rivalLots * 450.0 * (state.reputation / 100.0) - rivalLots * 150.0;
    const double reputationGain = state.personality == 1 ? 0.9 : state.personality == 3 ? 0.3 : 0.5;
    if (state.cash < 0.0)
        state.reputation = std::max(0.0, state.reputation - (state.personality == 3 ? 6.0 : 4.0));
    else
        state.reputation = std::min(100.0, state.reputation + reputationGain);
}

}
