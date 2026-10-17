#pragma once

#include <cstdint>

class DeterministicRng {
public:
    explicit DeterministicRng(std::uint64_t seed = 0) : m_state(seed) {}

    std::uint64_t next() {
        std::uint64_t value = (m_state += 0x9e3779b97f4a7c15ULL);
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    std::uint32_t bounded(std::uint32_t bound) {
        if (bound == 0) return 0;

        const std::uint32_t threshold = static_cast<std::uint32_t>(-bound) % bound;
        for (;;) {
            const std::uint32_t value = static_cast<std::uint32_t>(next());
            if (value >= threshold) return value % bound;
        }
    }

    std::uint64_t state() const { return m_state; }
    void setState(std::uint64_t state) { m_state = state; }

private:
    std::uint64_t m_state;
};
