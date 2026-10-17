#pragma once

#include <algorithm>

namespace simulation {

class FixedStepClock {
public:
    static constexpr double stepSeconds = 1.0 / 60.0;

    unsigned advance(double elapsedSeconds) {
        accumulator += std::min(std::max(elapsedSeconds, 0.0), 0.25);
        unsigned steps = 0;
        while (accumulator >= stepSeconds) {
            accumulator -= stepSeconds;
            ++steps;
        }
        return steps;
    }

private:
    double accumulator = 0.0;
};

}
