#include "persistence/SaveGameCodec.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

std::string legacySave(int version) {
    std::string contents = std::to_string(version) + " ";
    if (version >= 5) contents += "1 ";
    contents += "1200 8 2 1 0 1 64 80 0 3 2 4 1 1.5 2 0 3 ";
    if (version >= 2) contents += "2 0 1.25 1 1.0 ";
    if (version >= 3) contents += "68 8000 60 240 300 120 1 500 75 ";
    if (version >= 4) contents += "1 12.5 1000 0.8 0.1 0 ";
    if (version >= 5) contents += "2 1 0 1 1 ";
    if (version >= 6) contents += "987654321 ";
    return contents;
}

bool testVersionedDecode() {
    for (int version = 1; version <= 6; ++version) {
        persistence::SaveGameData decoded;
        std::string error;
        if (!persistence::decodeSaveGame(legacySave(version), decoded, 80, 60, 16, error)) {
            std::cerr << "version " << version << ": " << error << '\n';
            return false;
        }
        if (decoded.cash != 1200 || decoded.tows != 8 || decoded.ownedTrucks.size() != 2
            || decoded.district != 1)
            {
                std::cerr << "decoded values differ for version " << version << '\n';
                return false;
            }
        if (version >= 3 && (decoded.reputation != 68 || decoded.loans.size() != 1
                             || decoded.loans[0].balance != 500))
            return false;
        if (version >= 5 && (decoded.rivalKind != 2 || decoded.difficulty != 1
                             || decoded.optHints || !decoded.optWeather || !decoded.optAutosave))
            return false;
        const std::uint64_t expectedRng = version >= 6 ? 987654321ULL
            : 1337ULL ^ 0x9e3779b97f4a7c15ULL;
        if (decoded.rngState != expectedRng) {
            std::cerr << "RNG differs for version " << version << ": " << decoded.rngState << '\n';
            return false;
        }
    }
    return true;
}

bool testRoundTripAndValidation() {
    persistence::SaveGameData data;
    data.district = 3;
    data.cash = 4500;
    data.tows = 19;
    data.truckX = 300;
    data.truckY = 400;
    data.ownedTrucks = {0, 4};
    data.lots = {{0, 1.5f}, {1, 1.0f}};
    data.reputation = 71.5f;
    data.rivalCash = 9200.25;
    data.rivalReputation = 62.75;
    data.loans = {{2500, 90}};
    data.rivalKind = 3;
    data.rngState = 0xfedcba9876543210ULL;

    std::string contents, error;
    if (!persistence::encodeSaveGame(data, 80, 60, 16, contents, error)) return false;
    persistence::SaveGameData decoded;
    if (!persistence::decodeSaveGame(contents, decoded, 80, 60, 16, error)) return false;
    if (decoded.district != data.district || decoded.cash != data.cash || decoded.tows != data.tows
        || decoded.ownedTrucks != data.ownedTrucks
        || !decoded.decodedLots || decoded.decodedLots->size() != data.lots.size()
        || decoded.rivalCash != data.rivalCash || decoded.rivalKind != data.rivalKind
        || decoded.loans.size() != data.loans.size() || decoded.rngState != data.rngState)
        {
            std::cerr << "round-trip data differs: " << error << '\n';
            return false;
        }

    data.truckX = -1;
    if (persistence::encodeSaveGame(data, 80, 60, 16, contents, error) || error.empty())
        return false;

    persistence::SaveGameData invalidCandidate;
    invalidCandidate.cash = 777;
    if (persistence::decodeSaveGame("6 1", invalidCandidate, 80, 60, 16, error)
        || error.empty() || invalidCandidate.cash != 777)
        return false;
    return true;
}

}

int main() {
    if (!testVersionedDecode()) return 1;
    if (!testRoundTripAndValidation()) return 2;
    return 0;
}
