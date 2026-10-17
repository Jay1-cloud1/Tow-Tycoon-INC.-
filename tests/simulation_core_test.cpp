#include "simulation/FixedStepClock.h"
#include "simulation/CameraZoom.h"
#include "simulation/JobGeneration.h"
#include "simulation/RoadPathfinder.h"
#include "simulation/RoadsideJobs.h"
#include "simulation/RivalEconomy.h"
#include "simulation/WeeklyFinances.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

bool sameJob(const simulation::GeneratedJob& left, const simulation::GeneratedJob& right) {
    return left.x == right.x && left.y == right.y && left.payout == right.payout
        && left.carType == right.carType && left.angle == right.angle
        && left.jobClass == right.jobClass && left.zone == right.zone
        && left.jobType == right.jobType && left.description == right.description;
}

bool testFixedTicks() {
    simulation::FixedStepClock clockA, clockB;
    unsigned ticksA = 0, ticksB = 0;
    for (int i = 0; i < 30; ++i) ticksA += clockA.advance(1.0 / 30.0);
    for (int i = 0; i < 60; ++i) ticksB += clockB.advance(1.0 / 60.0);
    if (ticksA != 60 || ticksA != ticksB) return false;

    simulation::FixedStepClock clampedClock;
    if (clampedClock.advance(1.0) != 15) return false;
    return true;
}

bool testJobReplay() {
    constexpr int width = 32, height = 24, road = 1;
    std::vector<int> tiles(width * height, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (x % 8 == 0 || y % 8 == 0) tiles[y * width + x] = road;

    DeterministicRng rngA(20261016), rngB(20261016);
    for (int i = 0; i < 100; ++i) {
        const auto a = simulation::generateJob(tiles,width,height,road,16,240,160,2,72,rngA);
        const auto b = simulation::generateJob(tiles,width,height,road,16,240,160,2,72,rngB);
        if (a.has_value() != b.has_value()) return false;
        if (a && !sameJob(*a,*b)) return false;
    }
    return rngA.state() == rngB.state();
}

bool testWeeklyFinances() {
    simulation::WeeklyFinanceState state{
        1200, 825, 1000, 400, 99.7f, {{20,50},{100,30}}
    };
    simulation::applyWeeklyFinances(state,2,3,4,1.2f,130,45,15);
    if (state.cash != 604 || state.weeklyRevenueAccum != 0
        || state.lastWeekRevenue != 825 || state.lastWeekExpense != 596
        || state.reputation != 100.0f || state.loans.size() != 1
        || state.loans[0].balance != 70 || state.loans[0].weeklyPayment != 30)
        return false;
    return true;
}

bool testLoanRepayment() {
    simulation::WeeklyFinanceState state{900,0,0,0,50.0f,{{1200,100},{250,50}}};
    if (simulation::repayLoan(state,0,500)!=500 || state.cash!=400
        || state.loans[0].balance!=700)
        return false;
    if (simulation::repayLoan(state,1,1000)!=250 || state.cash!=150
        || state.loans.size()!=1 || state.loans[0].balance!=700)
        return false;
    if (simulation::repayLoan(state,0,1000)!=150 || state.cash!=0
        || state.loans[0].balance!=550)
        return false;
    return simulation::repayLoan(state,3,500)==0;
}

bool testRoadsideServiceJobs() {
    constexpr int width=32,height=24,road=1;
    std::vector<int> tiles(width*height,0);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
        if(x%8==0||y%8==0) tiles[y*width+x]=road;
    DeterministicRng rng(77881);
    for(int i=0;i<100;++i){
        const auto job=simulation::generateRoadsideJob(tiles,width,height,road,16,240,160,rng);
        if(!job || job->jobType!=3 || job->payout<85 || job->payout>150
           || job->description.empty())
            return false;
    }
    return true;
}

bool testCameraZoom() {
    const float zoomIn=simulation::adjustCameraZoom(2.0f,1);
    const float zoomOut=simulation::adjustCameraZoom(2.0f,-1);
    return zoomIn>2.0f && zoomOut<2.0f
        && simulation::adjustCameraZoom(4.9f,3)==5.0f
        && simulation::adjustCameraZoom(0.8f,-3)==0.75f;
}

bool testRouting() {
    constexpr int width = 7, height = 7, road = 1;
    std::vector<int> tiles(width * height, 0);
    for (int i = 0; i < width; ++i) {
        tiles[3 * width + i] = road;
        tiles[i * width + 3] = road;
    }
    const auto path = simulation::findRoadPath(tiles,width,height,road,0,3,6,3);
    if (path.size() != 7 || path.front() != std::pair<int,int>{0,3}
        || path.back() != std::pair<int,int>{6,3})
        return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto [x,y] = path[i];
        if (tiles[y * width + x] != road) return false;
        if (i > 0 && std::abs(path[i].first-path[i-1].first)
                     + std::abs(path[i].second-path[i-1].second) != 1)
            return false;
    }
    tiles[3 * width + 3] = 0;
    return simulation::findRoadPath(tiles,width,height,road,0,3,6,3).empty();
}

struct ReplaySnapshot {
    struct Loan { int balance; int weeklyPayment; };
    DeterministicRng rng{8152026};
    std::uint64_t ticks = 0;
    int cash = 1800;
    int weeklyRevenue = 0;
    int lastRevenue = 0;
    int lastExpense = 0;
    float reputation = 64.0f;
    std::vector<simulation::GeneratedJob> jobs;
    std::vector<Loan> loans{{900, 120}, {200, 50}};
    simulation::RivalEconomyState rival{7200.0, 58.0, 0.73, 12, 0, {}};
};

ReplaySnapshot replayAtFrameRate(double frameSeconds, int frames) {
    constexpr int width = 40, height = 32, road = 1, tileSize = 16;
    std::vector<int> tiles(width * height, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (x % 8 == 0 || y % 8 == 0) tiles[y * width + x] = road;

    ReplaySnapshot state;
    for (int i = 0; i < 18; ++i)
        state.rival.lots.push_back({i % 5 == 0 ? 1 : -1, 420 + (i % 7) * 40, 1.0f});

    simulation::FixedStepClock clock;
    for (int frame = 0; frame < frames; ++frame) {
        const unsigned steps = clock.advance(frameSeconds);
        for (unsigned step = 0; step < steps; ++step) {
            ++state.ticks;
            if (state.ticks % 18 == 0) {
                auto job = simulation::generateJob(tiles, width, height, road, tileSize,
                    320.0f, 256.0f, 2, state.reputation, state.rng);
                if (job) state.jobs.push_back(*job);
            }
            if (state.ticks % 60 == 0) {
                const int revenue = 100 + static_cast<int>(state.rng.bounded(250));
                state.cash += revenue;
                state.weeklyRevenue += revenue;
            }
            if (state.ticks % 180 == 0) {
                simulation::applyRivalWeeklyTurn(state.rival, state.rng);
                simulation::WeeklyFinanceState finance{state.cash, state.weeklyRevenue,
                    state.lastRevenue, state.lastExpense, state.reputation, {}};
                for (const auto& loan : state.loans)
                    finance.loans.push_back({loan.balance, loan.weeklyPayment});
                simulation::applyWeeklyFinances(finance, 2, 3, 4, 1.15f, 130, 45, 15);
                state.cash = finance.cash;
                state.weeklyRevenue = finance.weeklyRevenueAccum;
                state.lastRevenue = finance.lastWeekRevenue;
                state.lastExpense = finance.lastWeekExpense;
                state.reputation = finance.reputation;
                state.loans.clear();
                for (const auto& loan : finance.loans)
                    state.loans.push_back({loan.balance, loan.weeklyPayment});
            }
        }
    }
    return state;
}

bool sameReplaySnapshot(const ReplaySnapshot& left, const ReplaySnapshot& right) {
    if (left.rng.state() != right.rng.state() || left.ticks != right.ticks
        || left.cash != right.cash || left.weeklyRevenue != right.weeklyRevenue
        || left.lastRevenue != right.lastRevenue || left.lastExpense != right.lastExpense
        || left.reputation != right.reputation || left.jobs.size() != right.jobs.size()
        || left.loans.size() != right.loans.size()
        || left.rival.cash != right.rival.cash
        || left.rival.reputation != right.rival.reputation
        || left.rival.aggression != right.rival.aggression
        || left.rival.maxLots != right.rival.maxLots
        || left.rival.personality != right.rival.personality
        || left.rival.lots.size() != right.rival.lots.size())
        return false;
    for (std::size_t i = 0; i < left.jobs.size(); ++i)
        if (!sameJob(left.jobs[i], right.jobs[i])) return false;
    for (std::size_t i = 0; i < left.loans.size(); ++i)
        if (left.loans[i].balance != right.loans[i].balance
            || left.loans[i].weeklyPayment != right.loans[i].weeklyPayment)
            return false;
    for (std::size_t i = 0; i < left.rival.lots.size(); ++i)
        if (left.rival.lots[i].owner != right.rival.lots[i].owner
            || left.rival.lots[i].baseRevenue != right.rival.lots[i].baseRevenue
            || left.rival.lots[i].incomeMultiplier != right.rival.lots[i].incomeMultiplier)
            return false;
    return true;
}

bool testFullStateReplay() {
    const auto thirtyHz = replayAtFrameRate(1.0 / 30.0, 300);
    const auto sixtyHz = replayAtFrameRate(1.0 / 60.0, 600);
    return thirtyHz.ticks == 600 && sameReplaySnapshot(thirtyHz, sixtyHz);
}

}

int main() {
    if (!testFixedTicks()) return 1;
    if (!testJobReplay()) return 2;
    if (!testWeeklyFinances()) return 3;
    if (!testRouting()) return 4;
    if (!testFullStateReplay()) return 5;
    if (!testLoanRepayment()) return 6;
    if (!testRoadsideServiceJobs()) return 7;
    if (!testCameraZoom()) return 8;
    return 0;
}
