#pragma once

namespace simulation {
inline constexpr int maxTrucks = 64;
inline constexpr int maxDrivers = 128;
inline constexpr int maxDistrict = 8;
inline constexpr int creditMonthsRequired = 6;

inline void observeCash(int cash, int& positiveMonths, bool& monthPositive) {
    if (cash <= 0) { positiveMonths = 0; monthPositive = false; }
}

inline void finishCreditMonth(int cash, int& positiveMonths, bool& monthPositive) {
    positiveMonths = monthPositive && cash > 0
        ? (positiveMonths < creditMonthsRequired ? positiveMonths + 1 : creditMonthsRequired) : 0;
    monthPositive = cash > 0;
}

inline const char* loanRejection(int cash, int tows, int positiveMonths,
                                int activeLoans, int operatingSurplus, int payment) {
    if (positiveMonths < creditMonthsRequired) return "Requires 6 consecutive positive-cash months";
    if (tows < 6) return "Complete at least 6 tows first";
    if (cash <= 0) return "Cash must be positive";
    if (activeLoans != 0) return "Repay your existing loan before borrowing again";
    if (payment <= 0 || operatingSurplus <= 0 || payment > operatingSurplus / 2)
        return "Payment exceeds 50% of last week's cash surplus";
    return nullptr;
}
}
