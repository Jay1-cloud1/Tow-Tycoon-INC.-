#pragma once

// Versioned save schema and validation, independent of SDL and game-state classes.

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace persistence {

struct SavedLot {
    int owner = -1;
    float incomeMultiplier = 1.0f;
};

struct SavedLoan {
    int balance = 0;
    int weeklyPayment = 0;
};

struct SaveGameData {
    int district = 1;
    int cash = 500;
    int tows = 0;
    int goal = 0;
    int drivers = 0;
    int drivenSlot = 0;
    bool marketplaceUnlocked = false;
    float truckX = 0.0f;
    float truckY = 0.0f;
    int facing = 0;
    int day = 1;
    int week = 1;
    int month = 1;
    int year = 1;
    float dayAccum = 0.0f;
    std::vector<int> ownedTrucks{0};
    std::vector<SavedLot> lots;
    std::optional<std::vector<SavedLot>> decodedLots;
    float reputation = 50.0f;
    double rivalCash = 8000.0;
    double rivalReputation = 60.0;
    int weeklyRevenueAccum = 0;
    int lastWeekRevenue = 0;
    int lastWeekExpense = 0;
    std::vector<SavedLoan> loans;
    bool isPublic = false;
    double sharePrice = 0.0;
    double sharesOut = 0.0;
    double youOwnFraction = 1.0;
    double rivalStake = 0.0;
    bool wonDistrict = false;
    int rivalKind = 0;
    int difficulty = 1;
    bool optHints = true;
    bool optWeather = true;
    bool optAutosave = true;
    std::uint64_t rngState = 0;
};

inline bool validateSaveGame(const SaveGameData& data, int mapWidth, int mapHeight, int tileSize,
                             std::string& error) {
    if (data.district < 1 || data.district > 8 || data.ownedTrucks.empty()
        || data.ownedTrucks.size() > 64 || data.drivenSlot < 0
        || data.drivenSlot >= static_cast<int>(data.ownedTrucks.size())
        || tileSize <= 0 || !std::isfinite(data.truckX) || !std::isfinite(data.truckY)
        || data.truckX < 0 || data.truckX >= mapWidth * tileSize
        || data.truckY < 0 || data.truckY >= mapHeight * tileSize
        || data.tows < 0 || data.goal < 0 || data.goal > 4 || data.drivers < 0 || data.drivers > 128
        || data.facing < 0 || data.facing > 3
        || data.day < 1 || data.day > 7 || data.week < 1 || data.week > 4
        || data.month < 1 || data.month > 12 || data.year < 1
        || !std::isfinite(data.dayAccum) || data.dayAccum < 0 || data.dayAccum >= 12.0f
        || !std::isfinite(data.reputation) || data.reputation < 0 || data.reputation > 100
        || !std::isfinite(data.rivalCash) || !std::isfinite(data.rivalReputation)
        || data.rivalReputation < 0 || data.rivalReputation > 100
        || data.weeklyRevenueAccum < 0 || data.lastWeekRevenue < 0 || data.lastWeekExpense < 0
        || !std::isfinite(data.sharePrice) || data.sharePrice < 0
        || !std::isfinite(data.sharesOut) || data.sharesOut < 0
        || !std::isfinite(data.youOwnFraction) || data.youOwnFraction < 0 || data.youOwnFraction > 1
        || !std::isfinite(data.rivalStake) || data.rivalStake < 0 || data.rivalStake > 1
        || data.rivalKind < 0 || data.rivalKind > 3 || data.difficulty < 0 || data.difficulty > 2
        || data.lots.size() > 10000 || data.loans.size() > 256) {
        error = "save data contains invalid values";
        return false;
    }
    for (int id : data.ownedTrucks) {
        if (id < 0 || id > 5) {
            error = "truck model is out of range";
            return false;
        }
    }
    for (const auto& lot : data.lots) {
        if (lot.owner < -1 || lot.owner > 1 || !std::isfinite(lot.incomeMultiplier)
            || lot.incomeMultiplier < 0 || lot.incomeMultiplier > 10) {
            error = "lot data contains invalid values";
            return false;
        }
    }
    for (const auto& loan : data.loans) {
        if (loan.balance <= 0 || loan.weeklyPayment <= 0) {
            error = "loan data contains invalid values";
            return false;
        }
    }
    error.clear();
    return true;
}

inline bool encodeSaveGame(const SaveGameData& data, int mapWidth, int mapHeight, int tileSize,
                           std::string& contents, std::string& error) {
    if (!validateSaveGame(data, mapWidth, mapHeight, tileSize, error)) return false;
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << 6 << ' ' << data.district << ' ' << data.cash << ' ' << data.tows << ' '
        << data.goal << ' ' << data.drivers << ' ' << data.drivenSlot << ' '
        << (data.marketplaceUnlocked ? 1 : 0) << ' ' << data.truckX << ' ' << data.truckY << ' '
        << data.facing << ' ' << data.day << ' ' << data.week << ' ' << data.month << ' '
        << data.year << ' ' << data.dayAccum << ' ' << data.ownedTrucks.size();
    for (int id : data.ownedTrucks) out << ' ' << id;
    out << ' ' << data.lots.size();
    for (const auto& lot : data.lots) out << ' ' << lot.owner << ' ' << lot.incomeMultiplier;
    out << ' ' << data.reputation << ' ' << data.rivalCash << ' ' << data.rivalReputation << ' '
        << data.weeklyRevenueAccum << ' ' << data.lastWeekRevenue << ' ' << data.lastWeekExpense
        << ' ' << data.loans.size();
    for (const auto& loan : data.loans) out << ' ' << loan.balance << ' ' << loan.weeklyPayment;
    out << ' ' << (data.isPublic ? 1 : 0) << ' ' << data.sharePrice << ' ' << data.sharesOut
        << ' ' << data.youOwnFraction << ' ' << data.rivalStake << ' ' << (data.wonDistrict ? 1 : 0)
        << ' ' << data.rivalKind << ' ' << data.difficulty << ' ' << (data.optHints ? 1 : 0)
        << ' ' << (data.optWeather ? 1 : 0) << ' ' << (data.optAutosave ? 1 : 0)
        << ' ' << data.rngState << '\n';
    if (!out) {
        error = "failed to serialize save data";
        return false;
    }
    contents = out.str();
    error.clear();
    return true;
}

inline bool decodeSaveGameCandidate(const std::string& contents, SaveGameData& candidate,
                                    int mapWidth, int mapHeight, int tileSize, std::string& error) {
    std::istringstream in(contents);
    int version = 0;
    if (!(in >> version) || version < 1 || version > 6) {
        error = "unsupported or invalid save version";
        return false;
    }
    if (version < 5) candidate.district = 1;
    if (version >= 5 && !(in >> candidate.district)) {
        error = "save file is truncated at district";
        return false;
    }
    if (candidate.district < 1 || candidate.district > 8) {
        error = "district value is out of range";
        return false;
    }

    int unlocked = 0, truckCount = 0;
    if (!(in >> candidate.cash >> candidate.tows >> candidate.goal >> candidate.drivers
          >> candidate.drivenSlot >> unlocked >> candidate.truckX >> candidate.truckY
          >> candidate.facing >> candidate.day >> candidate.week >> candidate.month
          >> candidate.year >> candidate.dayAccum >> truckCount)) {
        error = "save file is truncated in player state";
        return false;
    }
    if (truckCount < 0 || truckCount > 64) {
        error = "truck count is out of range";
        return false;
    }
    candidate.marketplaceUnlocked = unlocked != 0;
    candidate.ownedTrucks.clear();
    for (int i = 0; i < truckCount; ++i) {
        int id = 0;
        if (!(in >> id) || id < 0 || id > 5) {
            error = "truck list is truncated or contains an invalid model";
            return false;
        }
        candidate.ownedTrucks.push_back(id);
    }
    if (candidate.ownedTrucks.empty()) candidate.ownedTrucks.push_back(0);

    if (version >= 2) {
        int lotCount = 0;
        if (!(in >> lotCount)) {
            error = "save file is truncated at lot count";
            return false;
        }
        if (lotCount < 0 || lotCount > 10000) {
            error = "lot count is out of range";
            return false;
        }
        std::vector<SavedLot> savedLots;
        savedLots.reserve(static_cast<std::size_t>(lotCount));
        for (int i = 0; i < lotCount; ++i) {
            SavedLot lot;
            if (!(in >> lot.owner >> lot.incomeMultiplier)) {
                error = "save file is truncated in lot data";
                return false;
            }
            if (lot.owner < -1 || lot.owner > 1 || !std::isfinite(lot.incomeMultiplier)
                || lot.incomeMultiplier < 0 || lot.incomeMultiplier > 10) {
                error = "lot data contains invalid values";
                return false;
            }
            savedLots.push_back(lot);
        }
        candidate.decodedLots = std::move(savedLots);
    }

    if (version >= 3) {
        int loanCount = 0;
        if (!(in >> candidate.reputation >> candidate.rivalCash >> candidate.rivalReputation
              >> candidate.weeklyRevenueAccum >> candidate.lastWeekRevenue
              >> candidate.lastWeekExpense >> loanCount)) {
            error = "save file is truncated in finance state";
            return false;
        }
        if (loanCount < 0 || loanCount > 256) {
            error = "loan count is out of range";
            return false;
        }
        candidate.loans.clear();
        for (int i = 0; i < loanCount; ++i) {
            SavedLoan loan;
            if (!(in >> loan.balance >> loan.weeklyPayment)
                || loan.balance <= 0 || loan.weeklyPayment <= 0) {
                error = "loan data is truncated or contains invalid values";
                return false;
            }
            candidate.loans.push_back(loan);
        }
    }

    if (version >= 4) {
        int isPublic = 0, wonDistrict = 0;
        if (!(in >> isPublic >> candidate.sharePrice >> candidate.sharesOut
              >> candidate.youOwnFraction >> candidate.rivalStake >> wonDistrict)) {
            error = "save file is truncated in stock state";
            return false;
        }
        candidate.isPublic = isPublic != 0;
        candidate.wonDistrict = wonDistrict != 0;
    }
    if (version >= 5) {
        int optHints = 1, optWeather = 1, optAutosave = 1;
        if (!(in >> candidate.rivalKind >> candidate.difficulty >> optHints
              >> optWeather >> optAutosave)) {
            error = "save file is truncated in settings";
            return false;
        }
        candidate.optHints = optHints != 0;
        candidate.optWeather = optWeather != 0;
        candidate.optAutosave = optAutosave != 0;
    }
    if (version >= 6) {
        if (!(in >> candidate.rngState)) {
            error = "save file is truncated at RNG state";
            return false;
        }
    }

    std::string trailing;
    if (in >> trailing) {
        error = "save file contains unexpected trailing data";
        return false;
    }
    if (in.bad()) {
        error = "I/O error while reading save file";
        return false;
    }
    if (version < 6)
        candidate.rngState = 1337ULL
            ^ (static_cast<std::uint64_t>(candidate.district) * 0x9e3779b97f4a7c15ULL);
    if (!validateSaveGame(candidate, mapWidth, mapHeight, tileSize, error)) return false;
    return true;
}

inline bool decodeSaveGame(const std::string& contents, SaveGameData& candidate,
                           int mapWidth, int mapHeight, int tileSize, std::string& error) {
    SaveGameData parsed = candidate;
    parsed.decodedLots.reset();
    if (!decodeSaveGameCandidate(contents, parsed, mapWidth, mapHeight, tileSize, error))
        return false;
    candidate = std::move(parsed);
    return true;
}

}
