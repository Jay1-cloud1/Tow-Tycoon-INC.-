#pragma once

// Versioned save schema and validation, independent of SDL and game-state classes.

#include <cmath>
#include "../simulation/ProgressionRules.h"
#include <algorithm>
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
    // Placement, saved from version 8 so funded and dev-placed lots survive a reload.
    float cx = 0.0f, cy = 0.0f;
    int buildingType = 0, baseRevenue = 0;
};

struct SavedBuilding { int tx = 0, ty = 0, type = 0; };
struct SavedCity {
    std::string name;
    int cx = 0, cy = 0, radius = 0, population = 0, zone = 0, prosperity = 0, growthLevel = 0;
    int towsCompleted = 0, councilRating = 450;
    bool exclusiveContract = false;
};
// The live map (version 8+): tiles, buildings and towns change through council
// funding and developer tools, so they are saved instead of regenerated.
struct SavedWorld {
    std::vector<int> tiles;
    std::vector<SavedBuilding> buildings;
    std::vector<SavedCity> cities;
};
constexpr int maxSavedCities = 256;

struct SavedLoan {
    int balance = 0;
    int weeklyPayment = 0;
};

struct SavedJob {
    float x=0,y=0; int payout=0; bool active=true; int carType=0,angle=0,jclass=0,zone=0,jtype=0;
    std::string desc;
};
struct SavedCargo { int payout=0,carType=0,jclass=0,city=-1; }; // city: pickup municipality, -1 unknown
struct SavedTruck {
    bool active=false; int model=0; float x=0,y=0; int facing=2;
    std::vector<std::pair<int,int>> route; int ri=0,phase=0;
    int carType=0,payout=0,jclass=0,jobType=0,kind=-1,slot=-1,city=-1;
};
struct SavedRuntime {
    std::vector<SavedJob> jobs;
    std::vector<SavedCargo> carried;
    std::vector<SavedTruck> autoTrucks,rivalTrucks;
    int weather=0; float weatherTimer=30,spawnTimer=0,pulse=0;
    bool announcedMajority=false;
    double rivalAggression=0.6; int rivalMaxLots=40;
    int positiveMonths=0; bool monthPositive=true; int pendingDistrict=0;
};
struct SaveGameData {
    SavedRuntime runtime;
    bool hasRuntime = true;
    SavedWorld world;
    bool hasWorld = false; // true writes version 8; false keeps the version 7 layout
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
    const int effectiveMapW = std::max(mapWidth, 256);
    const int effectiveMapH = std::max(mapHeight, 192);
    if (data.district < 1 || data.district > simulation::maxDistrict || data.ownedTrucks.empty()
        || data.ownedTrucks.size() > simulation::maxTrucks || data.drivenSlot < 0
        || data.drivenSlot >= static_cast<int>(data.ownedTrucks.size())
        || tileSize <= 0 || !std::isfinite(data.truckX) || !std::isfinite(data.truckY)
        || data.truckX < 0 || data.truckX >= effectiveMapW * tileSize
        || data.truckY < 0 || data.truckY >= effectiveMapH * tileSize
        || data.tows < 0 || data.goal < 0 || data.goal > 4 || data.drivers < 0 || data.drivers > simulation::maxDrivers
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
    const auto& r=data.runtime;
    auto position=[&](float x,float y){return std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<effectiveMapW*tileSize&&y<effectiveMapH*tileSize;};
    auto payload=[](int pay,int car,int cls){return pay>=0&&pay<=1000000&&car>=0&&car<6&&cls>=0&&cls<3;};
    if(r.jobs.size()>512 || r.carried.size()>2 || r.autoTrucks.size()>=data.ownedTrucks.size()
       || r.autoTrucks.size()>static_cast<std::size_t>(data.drivers) || r.rivalTrucks.size()>3
       || r.weather<0 || r.weather>2 || !std::isfinite(r.weatherTimer) || r.weatherTimer<0
       || !std::isfinite(r.spawnTimer) || r.spawnTimer<0 || !std::isfinite(r.pulse) || r.pulse<0
       || !std::isfinite(r.rivalAggression) || r.rivalAggression<0 || r.rivalAggression>1
       || r.rivalMaxLots<0 || r.rivalMaxLots>10000 || r.positiveMonths<0 || r.positiveMonths>6 || r.pendingDistrict<0 || r.pendingDistrict>simulation::maxDistrict) {
        error="invalid runtime state"; return false;
    }
    const int capacity=(data.ownedTrucks[data.drivenSlot]==1||data.ownedTrucks[data.drivenSlot]==2)?2:1;
    if(r.carried.size()>static_cast<std::size_t>(capacity)){error="cargo exceeds truck capacity";return false;}
    for(const auto& j:r.jobs) if(!position(j.x,j.y)||!payload(j.payout,j.carType,j.jclass)
        ||j.angle<0||j.angle>270||j.angle%90!=0||j.zone<0||j.zone>2||j.jtype<0||j.jtype>3||j.desc.size()>512){error="invalid saved job";return false;}
    for(const auto& c:r.carried) if(!payload(c.payout,c.carType,c.jclass)){error="invalid cargo";return false;}
    std::vector<int> slots;
    std::size_t totalRoute=0;
    auto truckValid=[&](const SavedTruck& t,bool rival){
        totalRoute+=t.route.size();
        if(!t.active||t.model<0||t.model>5||!position(t.x,t.y)||t.facing<0||t.facing>3
           ||t.phase<0||t.phase>2||t.ri<0||static_cast<std::size_t>(t.ri)>t.route.size()
           ||!payload(t.payout,t.carType,t.jclass)||t.jobType<0||t.jobType>3||t.kind< -1||t.kind>3
           ||t.route.size()>static_cast<std::size_t>(effectiveMapW)*static_cast<std::size_t>(effectiveMapH)||totalRoute>50000) return false;
        if(!rival){
            if(t.slot<0||t.slot>=static_cast<int>(data.ownedTrucks.size())||t.slot==data.drivenSlot
               ||data.ownedTrucks[t.slot]!=t.model||std::find(slots.begin(),slots.end(),t.slot)!=slots.end())return false;
            slots.push_back(t.slot);
        }
        for(auto [x,y]:t.route) if(x<0||y<0||x>=effectiveMapW||y>=effectiveMapH)return false;
        return true;
    };
    for(const auto& t:r.autoTrucks) if(!truckValid(t,false)){error="invalid fleet assignment or route";return false;}
    for(const auto& t:r.rivalTrucks) if(!truckValid(t,true)){error="invalid rival route";return false;}
    const int cityLimit=data.hasWorld?static_cast<int>(data.world.cities.size()):maxSavedCities;
    auto cityValid=[&](int c){return c>=-1&&c<cityLimit;};
    for(const auto& c:r.carried) if(!cityValid(c.city)){error="invalid cargo municipality";return false;}
    for(const auto& t:r.autoTrucks) if(!cityValid(t.city)){error="invalid truck municipality";return false;}
    for(const auto& t:r.rivalTrucks) if(!cityValid(t.city)){error="invalid truck municipality";return false;}
    if(data.hasWorld){
        const auto& w=data.world;
        if(mapWidth<=0||mapHeight<=0||w.tiles.size()!=static_cast<std::size_t>(mapWidth)*static_cast<std::size_t>(mapHeight)
           ||w.buildings.size()>20000||w.cities.size()>static_cast<std::size_t>(maxSavedCities)){
            error="invalid saved map";return false;
        }
        for(int t:w.tiles) if(t<0||t>7){error="invalid saved map tile";return false;}
        for(const auto& b:w.buildings) if(b.tx<0||b.ty<0||b.tx>=mapWidth||b.ty>=mapHeight||b.type<0||b.type>5){error="invalid saved building";return false;}
        for(const auto& c:w.cities)
            if(c.name.empty()||c.name.size()>64||c.cx<0||c.cy<0||c.cx>=mapWidth||c.cy>=mapHeight||c.radius<0||c.radius>1000
               ||c.population<0||c.zone<0||c.zone>2||c.prosperity<0||c.prosperity>100||c.growthLevel<0||c.towsCompleted<0
               ||c.councilRating<-1000||c.councilRating>1000){error="invalid saved town";return false;}
        for(const auto& l:data.lots)
            if(!position(l.cx,l.cy)||l.buildingType<0||l.buildingType>5||l.baseRevenue<0||l.baseRevenue>1000000){error="invalid saved lot placement";return false;}
    }
    error.clear();
    return true;
}

inline bool encodeSaveGame(const SaveGameData& data, int mapWidth, int mapHeight, int tileSize,
                           std::string& contents, std::string& error) {
    if (!validateSaveGame(data, mapWidth, mapHeight, tileSize, error)) return false;
    const int version = data.hasWorld ? 8 : 7;
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << version << ' ' << data.district << ' ' << data.cash << ' ' << data.tows << ' '
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
        << ' ' << data.rngState;
    const auto& r=data.runtime;
    out << ' ' << r.weather << ' ' << r.weatherTimer << ' ' << r.spawnTimer << ' ' << r.pulse
        << ' ' << r.announcedMajority << ' ' << r.rivalAggression << ' ' << r.rivalMaxLots
        << ' ' << r.positiveMonths << ' ' << r.monthPositive << ' ' << r.pendingDistrict << ' ' << r.jobs.size();
    for(const auto& j:r.jobs) out << ' ' << j.x << ' ' << j.y << ' ' << j.payout << ' ' << j.active
        << ' ' << j.carType << ' ' << j.angle << ' ' << j.jclass << ' ' << j.zone << ' ' << j.jtype << ' ' << std::quoted(j.desc);
    out << ' ' << r.carried.size();
    for(const auto& c:r.carried){
        out << ' ' << c.payout << ' ' << c.carType << ' ' << c.jclass;
        if(version>=8) out << ' ' << c.city;
    }
    auto writeTrucks=[&](const std::vector<SavedTruck>& trucks){
        out << ' ' << trucks.size();
        for(const auto& t:trucks){
            out << ' ' << t.active << ' ' << t.model << ' ' << t.x << ' ' << t.y << ' ' << t.facing
                << ' ' << t.ri << ' ' << t.phase << ' ' << t.carType << ' ' << t.payout << ' ' << t.jclass
                << ' ' << t.jobType << ' ' << t.kind << ' ' << t.slot;
            if(version>=8) out << ' ' << t.city;
            out << ' ' << t.route.size();
            for(auto [x,y]:t.route) out << ' ' << x << ' ' << y;
        }
    };
    writeTrucks(r.autoTrucks); writeTrucks(r.rivalTrucks);
    if(version>=8){
        // Version 8 world section: run-length tiles, buildings, towns, lot placements.
        const auto& w=data.world;
        std::vector<std::pair<int,int>> runs;
        for(int t:w.tiles){ if(!runs.empty()&&runs.back().second==t) runs.back().first++; else runs.push_back({1,t}); }
        out << ' ' << runs.size();
        for(auto [n,t]:runs) out << ' ' << n << ' ' << t;
        out << ' ' << w.buildings.size();
        for(const auto& b:w.buildings) out << ' ' << b.tx << ' ' << b.ty << ' ' << b.type;
        out << ' ' << w.cities.size();
        for(const auto& c:w.cities) out << ' ' << std::quoted(c.name) << ' ' << c.cx << ' ' << c.cy << ' ' << c.radius
            << ' ' << c.population << ' ' << c.zone << ' ' << c.prosperity << ' ' << c.growthLevel
            << ' ' << c.towsCompleted << ' ' << c.councilRating << ' ' << (c.exclusiveContract ? 1 : 0);
        for(const auto& l:data.lots) out << ' ' << l.cx << ' ' << l.cy << ' ' << l.buildingType << ' ' << l.baseRevenue;
    }
    out << '\n';
    if (!out) {
        error = "failed to serialize save data";
        return false;
    }
    if(out.str().size()>1024*1024){error="save exceeds 1 MiB";return false;}
    contents = out.str();
    error.clear();
    return true;
}

inline bool decodeSaveGameCandidate(const std::string& contents, SaveGameData& candidate,
                                    int mapWidth, int mapHeight, int tileSize, std::string& error) {
    std::istringstream in(contents);
    int version = 0;
    if (!(in >> version) || version < 1 || version > 8) {
        error = "unsupported or invalid save version";
        return false;
    }
    if (version < 5) candidate.district = 1;
    if (version >= 5 && !(in >> candidate.district)) {
        error = "save file is truncated at district";
        return false;
    }
    if (candidate.district < 1 || candidate.district > simulation::maxDistrict) {
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
        candidate.lots=savedLots;
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

    candidate.hasRuntime=version>=7;
    candidate.runtime=SavedRuntime{};
    if(version>=7){
        auto& r=candidate.runtime;
        auto invalid=[&](){error="truncated or invalid runtime state";return false;};
        auto count=[&](int& n,int max){return static_cast<bool>(in>>n)&&n>=0&&n<=max;};
        int n=0;
        if(!(in>>r.weather>>r.weatherTimer>>r.spawnTimer>>r.pulse>>r.announcedMajority
             >>r.rivalAggression>>r.rivalMaxLots>>r.positiveMonths>>r.monthPositive>>r.pendingDistrict)||!count(n,512)) return invalid();
        for(int i=0;i<n;++i){SavedJob j;
            if(!(in>>j.x>>j.y>>j.payout>>j.active>>j.carType>>j.angle>>j.jclass>>j.zone>>j.jtype>>std::quoted(j.desc)))return invalid();
            r.jobs.push_back(std::move(j));
        }
        if(!count(n,2))return invalid();
        for(int i=0;i<n;++i){SavedCargo c;if(!(in>>c.payout>>c.carType>>c.jclass))return invalid();
            if(version>=8&&!(in>>c.city))return invalid();
            r.carried.push_back(c);}
        std::size_t totalRoute=0;
        auto readTrucks=[&](std::vector<SavedTruck>& trucks,int max){
            int size=0;if(!count(size,max))return false;
            for(int i=0;i<size;++i){SavedTruck t;int routeCount=0;
                if(!(in>>t.active>>t.model>>t.x>>t.y>>t.facing>>t.ri>>t.phase>>t.carType>>t.payout>>t.jclass
                     >>t.jobType>>t.kind>>t.slot)||(version>=8&&!(in>>t.city))||!count(routeCount,std::max(mapWidth,256)*std::max(mapHeight,192)))return false;
                totalRoute+=routeCount;if(totalRoute>50000)return false;
                for(int j=0;j<routeCount;++j){int x,y;if(!(in>>x>>y))return false;t.route.push_back({x,y});}
                trucks.push_back(std::move(t));
            }return true;
        };
        if(!readTrucks(r.autoTrucks,63)||!readTrucks(r.rivalTrucks,3))return invalid();
    }
    candidate.hasWorld=version>=8;
    candidate.world=SavedWorld{};
    if(version>=8){
        auto& w=candidate.world;
        auto invalid=[&](){error="truncated or invalid saved map";return false;};
        const std::size_t tileTotal=(mapWidth>0&&mapHeight>0)?static_cast<std::size_t>(mapWidth)*static_cast<std::size_t>(mapHeight):0;
        long long runCount=0;
        if(!(in>>runCount)||runCount<0||static_cast<std::size_t>(runCount)>tileTotal)return invalid();
        w.tiles.reserve(tileTotal);
        for(long long i=0;i<runCount;++i){
            long long n=0;int t=0;
            if(!(in>>n>>t)||n<=0||static_cast<std::size_t>(n)>tileTotal-w.tiles.size())return invalid();
            w.tiles.insert(w.tiles.end(),static_cast<std::size_t>(n),t);
        }
        int n=0;
        if(!(in>>n)||n<0||n>20000)return invalid();
        for(int i=0;i<n;++i){SavedBuilding b;if(!(in>>b.tx>>b.ty>>b.type))return invalid();w.buildings.push_back(b);}
        if(!(in>>n)||n<0||n>maxSavedCities)return invalid();
        for(int i=0;i<n;++i){SavedCity c;int exclusive=0;
            if(!(in>>std::quoted(c.name)>>c.cx>>c.cy>>c.radius>>c.population>>c.zone>>c.prosperity>>c.growthLevel
                 >>c.towsCompleted>>c.councilRating>>exclusive))return invalid();
            c.exclusiveContract=exclusive!=0;w.cities.push_back(std::move(c));}
        for(auto& l:candidate.lots) if(!(in>>l.cx>>l.cy>>l.buildingType>>l.baseRevenue))return invalid();
        if(candidate.decodedLots) *candidate.decodedLots=candidate.lots;
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



