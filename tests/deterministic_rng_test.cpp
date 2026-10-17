#include "DeterministicRng.h"

#include <array>
#include <cstdint>

int main() {
    constexpr std::array<std::uint64_t, 5> reference{{
        13161956497586561035ULL,
        14663483216071361993ULL,
        3765287255879986010ULL,
        8575537320440087138ULL,
        4226762000762084508ULL
    }};
    DeterministicRng vectorRng(1337);
    for (const auto value : reference) {
        if (vectorRng.next() != value) return 1;
    }

    DeterministicRng first(1337);
    DeterministicRng second(1337);
    for (int i = 0; i < 1000; ++i) {
        if (first.next() != second.next()) return 2;
    }

    const std::uint64_t savedState = first.state();
    std::array<std::uint64_t, 32> expected{};
    for (auto& value : expected) value = first.next();

    DeterministicRng restored;
    restored.setState(savedState);
    for (const auto value : expected) {
        if (restored.next() != value) return 3;
    }

    for (const std::uint32_t bound : {1u, 3u, 100u, 1000u}) {
        for (int i = 0; i < 1000; ++i) {
            if (first.bounded(bound) >= bound) return 4;
        }
    }
    if (first.bounded(0) != 0) return 5;
    return 0;
}
