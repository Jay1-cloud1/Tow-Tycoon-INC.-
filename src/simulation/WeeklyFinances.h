#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace simulation {

struct LoanBalance {
    int balance;
    int weeklyPayment;
};

struct WeeklyFinanceState {
    int cash;
    int weeklyRevenueAccum;
    int lastWeekRevenue;
    int lastWeekExpense;
    float reputation;
    std::vector<LoanBalance> loans;
};

inline int repayLoan(WeeklyFinanceState& state, std::size_t loanIndex, int requestedAmount) {
    if (loanIndex >= state.loans.size() || state.cash <= 0 || requestedAmount <= 0) return 0;
    auto& loan = state.loans[loanIndex];
    const int paid = std::min({state.cash, loan.balance, requestedAmount});
    state.cash -= paid;
    loan.balance -= paid;
    if (loan.balance == 0) state.loans.erase(state.loans.begin() + static_cast<std::ptrdiff_t>(loanIndex));
    return paid;
}

inline void applyWeeklyFinances(
    WeeklyFinanceState& state,
    int drivers,
    int truckCount,
    int ownedLots,
    float expenseMultiplier,
    int driverWage,
    int truckUpkeep,
    int lotUpkeep) {
    int loanPayments = 0;
    for (auto& loan : state.loans) {
        const int payment = std::min(loan.weeklyPayment, loan.balance);
        loan.balance -= payment;
        loanPayments += payment;
    }
    state.loans.erase(
        std::remove_if(state.loans.begin(), state.loans.end(),
            [](const LoanBalance& loan) { return loan.balance <= 0; }),
        state.loans.end());

    const int baseExpense = drivers * driverWage + ownedLots * lotUpkeep + truckCount * truckUpkeep;
    const int expense = static_cast<int>(std::round(baseExpense * expenseMultiplier)) + loanPayments;
    state.cash -= expense;
    state.lastWeekRevenue = state.weeklyRevenueAccum;
    state.lastWeekExpense = expense;
    state.weeklyRevenueAccum = 0;
    state.reputation = std::min(100.0f, state.reputation + 0.5f);
}

}
