#pragma once
#include "GameState.h"
#include "Engine.h"
#include "DeterministicRng.h"
#include "SaveStore.h"
#include "Version.h"
#include "simulation/JobGeneration.h"
#include "simulation/RoadsideJobs.h"
#include "simulation/RoadPathfinder.h"
#include "simulation/RivalEconomy.h"
#include "simulation/CameraZoom.h"
#include "simulation/TileIndex.h"
#include "persistence/SaveGameCodec.h"
#include "simulation/WeeklyFinances.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#if defined(TOWTYCOON_USE_TTF)
#include <SDL_ttf.h>
#endif

namespace tt {
enum Tile : int { GRASS=0, ROAD=1, WATER=2, SIDEWALK=3, DIRT=4, BUILDINGTILE=5, TREE=6, SAND=7 };
enum Facing : int { NORTH=0, EAST=1, SOUTH=2, WEST=3 };
enum JobClass : int { REGULAR=0, MEDIUM=1, HEAVY=2 };
enum Icon : int { IC_GAVEL=0, IC_DEAL=1, IC_MONEY=2, IC_BANK=3, IC_CHART=4, IC_TROPHY=5 };
enum Zone : int { Z_RES=0, Z_COM=1, Z_IND=2 };
}
inline const char* zoneName(int z){ static const char* w[]={"residential","commercial","industrial"}; return w[(z%3+3)%3]; }

// --------------------------- font + draw helpers ---------------------------
#if defined(TOWTYCOON_USE_TTF)
inline TTF_Font* getUiFont(Engine& e, int size = 28) {
    struct Cached { int size; TTF_Font* font; };
    static bool initDone=false, initOk=false; static std::vector<Cached> cache;
    if (!initDone){ initDone=true; if (TTF_Init()!=0) e.logInfo(std::string("TTF_Init failed: ")+TTF_GetError()); else initOk=true; }
    if (!initOk) return nullptr;
    for (auto& c:cache) if (c.size==size) return c.font;
    const std::string fp=e.assetPath("fonts/menu.ttf"); TTF_Font* f=TTF_OpenFont(fp.c_str(),size);
    if (!f) e.logInfo(std::string("TTF_OpenFont failed (")+fp+"): "+TTF_GetError());
    cache.push_back({size,f}); return f;
}
inline void drawText(Engine& e, const char* msg, int x, int y, SDL_Color color, int size=28, bool centerX=false){
    TTF_Font* font=getUiFont(e,size); if(!font)return;
    SDL_Surface* s=TTF_RenderUTF8_Blended(font,msg,color); if(!s)return;
    SDL_Texture* t=SDL_CreateTextureFromSurface(e.renderer(),s); int tw=s->w,th=s->h; SDL_FreeSurface(s); if(!t)return;
    SDL_Rect dst{centerX?x-tw/2:x,y,tw,th}; SDL_RenderCopy(e.renderer(),t,nullptr,&dst); SDL_DestroyTexture(t);
}
#else
inline void drawText(Engine&, const char*, int, int, SDL_Color, int=28, bool=false){}
#endif
inline void fillRect(Engine& e,int x,int y,int w,int h,Uint8 r,Uint8 g,Uint8 b,Uint8 a=255){ SDL_SetRenderDrawColor(e.renderer(),r,g,b,a); SDL_Rect rc{x,y,w,h}; SDL_RenderFillRect(e.renderer(),&rc); }
inline void strokeRect(Engine& e,int x,int y,int w,int h,Uint8 r,Uint8 g,Uint8 b,Uint8 a=255){ SDL_SetRenderDrawColor(e.renderer(),r,g,b,a); SDL_Rect rc{x,y,w,h}; SDL_RenderDrawRect(e.renderer(),&rc); }
inline void drawIcon(Engine& e,int idx,int cx,int cy,int size){ if(!e.iconsTex)return; SDL_Rect src{(idx%e.iconCount)*e.iconCell,0,e.iconCell,e.iconCell}; SDL_Rect dst{cx-size/2,cy-size/2,size,size}; SDL_RenderCopy(e.renderer(),e.iconsTex,&src,&dst); }
inline void drawGradient(Engine& e,SDL_Color top,SDL_Color bot){ const int h=e.logicalH(),w=e.logicalW(),b=72,bh=(h+b-1)/b;
    for(int i=0;i<b;++i){ float t=(float)i/(b-1); fillRect(e,0,i*bh,w,bh,(Uint8)(top.r+(bot.r-top.r)*t),(Uint8)(top.g+(bot.g-top.g)*t),(Uint8)(top.b+(bot.b-top.b)*t),255);} }

// =============================== MenuState =================================
class WorldState;
class MenuState : public GameState {
    friend struct WorldRegression;
public:
    const char* name() const override { return "MenuState"; }
    void handleEvent(Engine& e, SDL_Event& ev) override;
    void render(Engine& e) override;
private:
    enum ButtonId { B_CONTINUE, B_NEW, B_SETTINGS, B_QUIT };
    struct Button { SDL_Rect rect; const char* label; ButtonId id; };
    std::array<Button,4> buildButtons(Engine& e) const;
    bool hasSave(Engine& e) const;
    void activate(Engine& e, ButtonId id);
    void startGame(Engine& e, bool fresh);
    bool settingsOpen=false;
    bool confirmNew=false;
};
inline std::array<MenuState::Button,4> MenuState::buildButtons(Engine& e) const {
    const int bw=320,bh=58,sp=14,sx=(e.logicalW()-bw)/2;
    const int sy=std::max(80,e.logicalH()-360);
    return {{ {SDL_Rect{sx,sy,bw,bh},"Continue",B_CONTINUE},
              {SDL_Rect{sx,sy+(bh+sp),bw,bh},confirmNew?"Confirm New Game":"New Game",B_NEW},
              {SDL_Rect{sx,sy+2*(bh+sp),bw,bh},"Settings",B_SETTINGS},
              {SDL_Rect{sx,sy+3*(bh+sp),bw,bh},"Quit",B_QUIT} }};
}

// ----------------------------- shared catalog ------------------------------
struct TruckForSale { const char* name; int price; const char* cap; int maxClass; int capacity; };
inline const std::array<TruckForSale,6>& truckCatalog(){
    static const std::array<TruckForSale,6> c{{
        {"Used Wheel-Lift",   900, "reg x1",        tt::REGULAR, 1},
        {"Used Flatbed",     2500, "reg x2",        tt::REGULAR, 2},
        {"New Flatbed",      8000, "reg x2",        tt::REGULAR, 2},
        {"Medium Wrecker",  18000, "reg+med x1",    tt::MEDIUM,  1},
        {"Roadside Van",     6000, "reg x1",        tt::REGULAR, 1},
        {"Heavy Recovery",  45000, "reg+med+hvy x1",tt::HEAVY,   1},
    }};
    return c;
}
struct BidOpt { const char* name; float winBase; const char* note; };
inline const std::array<BidOpt,3>& bidOptions(){
    static const std::array<BidOpt,3> b{{
        {"High Chance", 0.85f, "Highest chance of winning the contract"},
        {"Balanced",    0.60f, "Moderate chance of winning"},
        {"Long Shot",   0.38f, "Lowest chance of winning"},
    }};
    return b;
}
struct LoanProduct { const char* name; int principal; float apr; int weeks; };
inline const std::array<LoanProduct,3>& loanCatalog(){
    static const std::array<LoanProduct,3> l{{
        {"Starter Loan",    5000, 0.10f, 52},
        {"Expansion Loan", 15000, 0.12f, 52},
        {"Fleet Loan",     30000, 0.15f, 78},
    }};
    return l;
}
inline const std::array<const char*,5>& goalText(){
    static const std::array<const char*,5> g{{
        "Goal: complete 6 tows to unlock the vehicle marketplace",
        "Goal: buy a 2nd truck, then hire a driver (Fleet = O)",
        "Goal: win your first contract lot (drive to a lot, press C)",
        "Goal: control 50% of the neighborhood",
        "Goal: dominate - take the whole neighborhood",
    }};
    return g;
}
inline const char* carWord(int t){ static const char* w[]={"red sedan","blue sedan","green SUV","pickup truck","delivery van","sports car"}; return w[(t%6+6)%6]; }
inline const char* classWord(int c){ static const char* w[]={"regular","medium","heavy"}; return w[(c%3+3)%3]; }
inline const char* streetWord(DeterministicRng& g){ static const char* s[]={"Main St","Oak Ave","2nd St","Elm Rd","Harbor Blvd","Pine St","Sunset Dr","Industrial Way"}; return s[g.bounded(8)]; }

// =============================== WorldState ================================
class WorldState : public GameState {
public:
    explicit WorldState(std::uint64_t randomSeed = 1337);
    const char* name() const override { return "WorldState"; }
    void handleEvent(Engine& e, SDL_Event& ev) override;
    void update(Engine& e, float dt) override;
    void render(Engine& e) override;
    // Skip loading the existing save; the next save overwrites it.
    void startFresh(){ freshStart=true; }
    bool prepareQuit(Engine& e) override;
    friend struct WorldRegression;
#ifdef TT_HARNESS
    void seedTest(){ marketplaceUnlocked=true; tows=6; cash=60000; ownedTrucks={0,3}; drivenSlot=0; drivers=1;
        for(size_t i=0;i<lots.size();++i) lots[i].owner = (i%3==0)?0 : (i%3==1)?1 : -1; }
    void test_open(int which){ seedTest(); reputation=72; lastWeekRevenue=1800; lastWeekExpense=540;
        if(which==4){ bidLot=-1; for(size_t i=0;i<lots.size();++i) if(lots[i].owner==-1){bidLot=(int)i;break;} bidSel=1; panel=P_BID; }
        else if(which==5){ loans.push_back({2600,50}); panel=P_BANK; }
        else if(which==6){ isPublic=true; sharesOut=1000000.0; sharePrice=(double)computeValuation()/sharesOut; rivalStake=0.3; panel=P_STOCK; }
        else panel=(which==2?P_TRUCKS: which==3?P_FLEET : P_MARKET); }
    void test_saveWrite(Engine& e){ cash=4242; ownedTrucks={0,3,5}; drivers=2; week=3; month=2; year=1; day=5; marketplaceUnlocked=true; reputation=77; loans.push_back({2000,50}); weeklyRevenueAccum=300; difficulty=2; rivalKind=RK_OPPORT; optHints=false; optWeather=false; optAutosave=true; if(!lots.empty()){lots[0].owner=0;lots[0].incomeMult=1.6f; if(lots.size()>1)lots[1].owner=1;} loadedOnce=true; saveGame(e); }
    void test_saveRead(Engine& e){ loadedOnce=true; loadGame(e); }
    int t_cash()const{return cash;} int t_fleet()const{return (int)ownedTrucks.size();} int t_drivers()const{return drivers;} int t_week()const{return week;} int t_month()const{return month;}
    int t_mylots()const{int n=0;for(auto&l:lots)if(l.owner==0)n++;return n;} int t_rep()const{return (int)reputation;} int t_loans()const{return (int)loans.size();}
    int t_diff()const{return difficulty;} int t_hints()const{return optHints?1:0;} int t_optw()const{return optWeather?1:0;}
    void test_weather(int w){ weather=w; weatherTimer=9999; }
    void test_autoSetup(){ drivers=1; ownedTrucks={0,3}; drivenSlot=0; cash=5000; marketplaceUnlocked=true; }
    int  test_path(int ax,int ay,int bx,int by){ return (int)findPath(ax,ay,bx,by).size(); }
    bool t_autoActive()const{return !autoTrucks.empty();}
    void test_rivalSetup(){ loadedOnce=true; drivers=1; ownedTrucks={0,3,5}; drivenSlot=0; cash=5000; reputation=80; marketplaceUnlocked=true;
        // force a few open jobs incl. one impound + one repo so the colored markers show
        jobs.clear();
        auto mk=[&](float tx,float ty,int jc,int jt,int pay){ Job j{(tx+0.5f)*tile,(ty+0.5f)*tile,pay,true,(int)rng.bounded(6),0,jc,tt::Z_COM,jt,""}; jobs.push_back(j); };
        mk(20,14,tt::REGULAR,JT_IMPOUND,210); mk(24,20,tt::MEDIUM,JT_REPO,375); mk(16,22,tt::REGULAR,JT_NORMAL,140); mk(28,16,tt::HEAVY,JT_NORMAL,800);
        // seed two rival trucks mid-drive (en route to jobs) so their sprites + routes render
        rivalTrucks.clear();
        { AutoTruck r; r.active=true; r.model=0; r.phase=1; r.route=findPath(8,8,20,14); r.ri=(int)r.route.size()/2;
          if(!r.route.empty()){ r.x=(r.route[r.ri].first+0.5f)*tile; r.y=(r.route[r.ri].second+0.5f)*tile; } else { r.x=rivalYardX; r.y=rivalYardY; }
          r.facing=tt::EAST; r.carType=2; r.payout=210; rivalTrucks.push_back(r); }
        { AutoTruck r; r.active=true; r.model=5; r.phase=1; r.route=findPath(10,20,24,20); r.ri=(int)r.route.size()/3;
          if(!r.route.empty()){ r.x=(r.route[r.ri].first+0.5f)*tile; r.y=(r.route[r.ri].second+0.5f)*tile; } else { r.x=rivalYardX; r.y=rivalYardY; }
          r.facing=tt::SOUTH; r.carType=4; r.payout=375; rivalTrucks.push_back(r); }
        truckX=(22+0.5f)*tile; truckY=(18+0.5f)*tile; // center camera between the action
    }
    int t_rivals()const{return (int)rivalTrucks.size();}
    void test_settings(){ loadedOnce=true; wonDistrict=true; panel=P_SETTINGS; }
    void test_district2(){ startDistrict(2); }
    int t_district()const{return district;}
    int t_rivalKind()const{return rivalKind;}
#endif
private:
    struct Building { int tx,ty,type; };
    enum JobType : int { JT_NORMAL=0, JT_IMPOUND=1, JT_REPO=2, JT_ROADSIDE=3 };
    using Job = persistence::SavedJob;
    using Carried = persistence::SavedCargo;
    // A*-routed self-driving truck - used for your company's auto truck AND rival trucks
    using AutoTruck = persistence::SavedTruck;
    struct Lot { float cx,cy; int owner; float incomeMult; int bType; int baseRev; }; // owner -1 open,0 you,1 rival
    struct Loan { int balance; int weeklyPayment; };
    enum Panel { P_NONE, P_MARKET, P_TRUCKS, P_FLEET, P_BID, P_BANK, P_STOCK, P_SETTINGS, P_TOWNS, P_PERF };
    // rival personalities - each bids and drives differently
    enum RivalKind { RK_AGGR=0, RK_RELIABLE=1, RK_OPPORT=2, RK_CORRUPT=3 };
    static const char* rivalKindName(int k){ return k==RK_AGGR?"Aggressive":k==RK_RELIABLE?"Reliable":k==RK_OPPORT?"Opportunistic":"Corrupt"; }
    static const char* rivalCompanyName(int k){ return k==RK_AGGR?"Hook & Chain Towing":k==RK_RELIABLE?"Steadfast Recovery":k==RK_OPPORT?"Vulture Auto Salvage":"Backroom Towing Co."; }
    const char* difficultyName() const { return difficulty==0?"Easy":difficulty==2?"Hard":"Normal"; }

    void generateCity(unsigned seed=99u, float revScale=1.0f);
    void configureRival();          // set rival personality + starting books
    void startDistrict(int n);      // expand into a fresh, tougher district
    float diffExpenseMult() const { return difficulty==0?0.85f:difficulty==2?1.20f:1.0f; }
    float diffBidBonus() const { return difficulty==0?0.10f:difficulty==2?-0.08f:0.0f; }
    float rivalBidPressure() const { return rivalKind==RK_CORRUPT?0.06f:0.0f; } // corrupt rival greases officials
    void update(Engine& e);
    void spawnJob();
    void drawWorld(Engine& e);
    void drawLots(Engine& e);
    void drawEntities(Engine& e);
    void drawCar(Engine& e, float wx, float wy, int type, int angleDeg, float worldSize);
    void drawHud(Engine& e);
    void drawPanels(Engine& e);
    int  nearestActiveJob() const;
    int  nearestOpenLot() const;
    int  tileAtWorld(float wx, float wy) const;
    void worldToScreen(Engine& e, float wx, float wy, int& sx, int& sy) const;
    void advanceGoals(Engine& e);
    void advanceClock(Engine& e, float dt);
    void rivalWeeklyTurn();          // adapted TowingCompetitorAI (decide_bids + end_week)
    void weeklyFinances(Engine& e);  // wages, upkeep, loan payments, P&L snapshot
    long computeValuation() const;
    bool saveGame(Engine& e, std::string* error = nullptr);
    bool loadGame(Engine& e, std::string* error = nullptr);
    void countLots(int& mine,int& rival,int& open) const { mine=rival=open=0; for(auto&l:lots){ if(l.owner==0)mine++; else if(l.owner==1)rival++; else open++; } }
    int  model() const { return ownedTrucks.empty()?0:ownedTrucks[drivenSlot]; }
    int  carryCap() const { return truckCatalog()[model()].capacity; }
    int  maxClass() const { return truckCatalog()[model()].maxClass; }
    int  fleetMaxClass() const {
        int m = maxClass();
        for(int t : ownedTrucks) {
            if(t >= 0 && t < (int)truckCatalog().size()) {
                m = std::max(m, truckCatalog()[t].maxClass);
            }
        }
        return m;
    }
    struct CityArea {
        std::string name;
        int cx, cy;
        int radius;
        int population;
        int zone;
        int prosperity;
        int growthLevel;
        int towsCompleted;
        int councilRating = 450;
        bool exclusiveContract = false;
    };
    const char* councilRatingName(int r) const {
        if(r >= 800) return "Outstanding";
        if(r >= 600) return "Very Good";
        if(r >= 400) return "Good";
        if(r >= 200) return "Mediocre";
        if(r >= 0) return "Poor";
        return "Appalling";
    }
    SDL_Color councilRatingColor(int r) const {
        if(r >= 800) return SDL_Color{80,240,120,255};
        if(r >= 600) return SDL_Color{140,240,100,255};
        if(r >= 400) return SDL_Color{100,220,240,255};
        if(r >= 200) return SDL_Color{240,220,90,255};
        if(r >= 0) return SDL_Color{240,140,60,255};
        return SDL_Color{240,70,70,255};
    }
    int townSel = 0;
    std::string newsHeadline;
    float newsTimer = 0.0f;
    SDL_Color newsColor{245,215,95,255};
    void postNews(const std::string& headline, SDL_Color color = {245,215,95,255}) {
        newsHeadline = headline; newsTimer = 7.5f; newsColor = color;
    }
    std::vector<CityArea> cities;
    int cityAtWorld(float wx, float wy) const;
    const std::string& cityNameAtWorld(float wx, float wy) const;

    // Developer mode (OpenTTD-style landscape controls)
    bool devMode = false;
    int devTool = 1;
    void setDevMode(bool on) { devMode = on; }
    void devBuildRoad(int tx, int ty);
    void devBulldoze(int tx, int ty);
    void devPlaceBuilding(int tx, int ty, int type = 0);
    void devPlacePark(int tx, int ty, int w = 4, int h = 4);
    void devPlantTrees(int tx, int ty);
    void devPlaceWater(int tx, int ty);
    void devPlaceSand(int tx, int ty);
    void devFoundTown(int tx, int ty, const std::string& name);
    void devSpawnJob(int tx = 10, int ty = 10, int jobType = 0, int jobClass = 0);
    bool applyDevTool(int tool, int tx, int ty);
    bool isBridge(int tx, int ty) const;
    void devHiggsErode(int tx, int ty);

    int  zoneAtTileX(int tx) const;
    std::vector<std::pair<int,int>> findPath(int sx,int sy,int gx,int gy) const; // A* over road tiles
    void updateAutoTruck(Engine& e, float dt);
    void stepTruck(AutoTruck& t, bool rival, float dt);   // shared A* drive+service loop
    void updateRivals(float dt);
    const char* jobTypeTag(int jt) const { return jt==JT_IMPOUND?"IMPOUND":jt==JT_REPO?"REPO":""; }
    std::string streetIntersectionAt(float wx,float wy) const;
    void drawMinimap(Engine& e,float targetX,float targetY,bool hasTarget) const;

    static constexpr int kRoadSpacing=8;
    int mapW=256,mapH=192,tile=16;
    std::vector<int> tiles; std::vector<Building> buildings; std::vector<Lot> lots;
    int yardImpoundCount=0;

    float truckX=0,truckY=0; int facing=tt::NORTH; float speed=190.0f; float truckWorldSize=34.0f;

    int cash=500, tows=0;
    std::vector<int> ownedTrucks{0};
    int drivenSlot=0; int drivers=0;
    std::size_t selectedLoan=0;
    bool marketplaceUnlocked=false;
    Panel panel=P_NONE; int truckSel=0; int bidLot=-1; int bidSel=1;
    float toastTimer=0; std::string toastMsg,toastSub;
    float flashTimer=0; std::string flashMsg;

    std::vector<Job> jobs; std::vector<Carried> carried;
    std::vector<AutoTruck> autoTrucks, rivalTrucks;
    float yardX=0,yardY=0, rivalYardX=0,rivalYardY=0; DeterministicRng rng; float pulse=0;
    int goal=0;

    // world simulation: weather + time-of-day drive call volume and tow speed
    enum Weather { W_CLEAR=0, W_RAIN=1, W_STORM=2 };
    int weather=W_CLEAR; float weatherTimer=30.0f; float spawnTimer=0;
    float weatherSpeedMult() const { if(!optWeather) return 1.0f; return weather==W_STORM?0.70f : weather==W_RAIN?0.85f : 1.0f; }
    const char* weatherName() const { return weather==W_STORM?"Storm" : weather==W_RAIN?"Rain" : "Clear"; }
    bool announcedMajority=false;

    // reputation + finances
    float reputation=50.0f;            // player, 0..100 - affects bid odds, grows with tows
    std::vector<Loan> loans;
    int weeklyRevenueAccum=0, lastWeekRevenue=0, lastWeekExpense=0;
    static constexpr int kDriverWageWk=130, kLotUpkeepWk=15, kTruckUpkeepWk=45;

    // AI competitor state (adapted from the shared TowingCompetitorAI)
    double rivalCash=8000.0, rivalRep=60.0, rivalAggression=0.6; int rivalMaxLots=40;
    int rivalKind=RK_AGGR;              // personality: drives bidding + truck behavior
    int district=1;                    // which district you're operating in
    // settings
    int difficulty=1;                  // 0 easy, 1 normal, 2 hard
    bool optHints=true, optWeather=true, optAutosave=true;

    // stock market / IPO / buyout
    bool isPublic=false, wonDistrict=false;
    double sharePrice=0, sharesOut=0, youOwnFrac=1.0, rivalStake=0.0;
    static constexpr long kIpoThreshold=90000;
    long rivalValuation() const { int mine,rv,op; countLots(mine,rv,op); return std::max(100L, (long)rivalCash + (long)rv*1500); }

    static constexpr float kSecondsPerDay = 12.0f;
    float dayAccum=0; int day=1, week=1, month=1, year=1;
    bool loadedOnce=false, saveProtected=false, preserveBackup=false, freshStart=false;
    int positiveMonths=0; bool monthPositive=true;
    int fleetPage=0, pendingDistrict=0;
    void switchTruck(int delta);
    void ensureLoaded(Engine& e);
    void observeCash(){simulation::observeCash(cash,positiveMonths,monthPositive);}
};

// ------------------------------ MenuState impl ------------------------------
inline void MenuState::handleEvent(Engine& e, SDL_Event& ev){
    if(settingsOpen){
        if(ev.type!=SDL_KEYDOWN)return;
        auto& p=e.preferences; auto k=ev.key.keysym.sym;
        if(k==SDLK_ESCAPE||k==SDLK_g){settingsOpen=false;return;}
        if(k==SDLK_f||k==SDLK_F11){e.toggleFullscreen();return;}
        if(k==SDLK_d)p.difficulty=(p.difficulty+1)%3;
        else if(k==SDLK_w)p.weather=!p.weather;
        else if(k==SDLK_h)p.hints=!p.hints;
        else if(k==SDLK_a)p.autosave=!p.autosave;
        else return;
        p.overrideSaved=true; return;
    }
    if (ev.type==SDL_KEYDOWN){ const SDL_Keycode k=ev.key.keysym.sym;
        if (k==SDLK_RETURN||k==SDLK_KP_ENTER){ if(confirmNew) activate(e,B_NEW); else if(hasSave(e)) activate(e,B_CONTINUE); else activate(e,B_NEW); return; }
        if (k==SDLK_n){ activate(e,B_NEW); return; }
        if (k==SDLK_s){ activate(e,B_SETTINGS); return; }
        if (k==SDLK_F11){ e.toggleFullscreen(); return; }
        if (k==SDLK_ESCAPE){ if(confirmNew) confirmNew=false; else e.requestQuit(); return; } }
    if (ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button==SDL_BUTTON_LEFT){
        int mx=e.logicalMouseX(),my=e.logicalMouseY();
        for (const auto& b:buildButtons(e))
            if (mx>=b.rect.x&&mx<=b.rect.x+b.rect.w&&my>=b.rect.y&&my<=b.rect.y+b.rect.h){ activate(e,b.id); return; } }
}
inline bool MenuState::hasSave(Engine& e) const {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(e.saveDir())/"savegame.tt",ec)
        || std::filesystem::exists(std::filesystem::path(e.baseDir())/"savegame.tt",ec);
}
inline void MenuState::startGame(Engine& e, bool fresh){
    auto world=std::make_unique<WorldState>();
    if(fresh) world->startFresh();
    e.changeState(std::move(world));
}
inline void MenuState::activate(Engine& e, ButtonId id){
    if(id!=B_NEW) confirmNew=false;
    switch(id){
        case B_CONTINUE: if(hasSave(e)) startGame(e,false); return;
        case B_NEW:
            // Starting over replaces the existing save, so ask once before doing it.
            if(hasSave(e) && !confirmNew){ confirmNew=true; return; }
            startGame(e,true); return;
        case B_SETTINGS: settingsOpen=true; return;
        case B_QUIT: e.requestQuit(); return;
    }
}
inline void MenuState::render(Engine& e){
    if (e.menuBg){ SDL_Rect d{0,0,e.logicalW(),e.logicalH()}; SDL_RenderCopy(e.renderer(),e.menuBg,nullptr,&d); fillRect(e,0,0,e.logicalW(),e.logicalH(),12,14,26,120); }
    else drawGradient(e,SDL_Color{18,22,38,255},SDL_Color{30,34,52,255});
    const int cx=e.logicalW()/2;
    auto bs=buildButtons(e);
    if (e.menuLogo&&e.menuLogoW>0){ int dh=std::min(300,std::max(80,bs[0].rect.y-48)); int dw=(int)std::round((double)dh*e.menuLogoW/e.menuLogoH); SDL_Rect d{cx-dw/2,28,dw,dh}; SDL_RenderCopy(e.renderer(),e.menuLogo,nullptr,&d); }
    else drawText(e,"TOW TRUCK TYCOON",cx,120,SDL_Color{235,200,90,255},48,true);
    const bool saveExists=hasSave(e);
    int mx=e.logicalMouseX(),my=e.logicalMouseY();
    for (auto&b:bs){ const bool enabled=(b.id!=B_CONTINUE||saveExists);
        bool h=!settingsOpen&&enabled&&(mx>=b.rect.x&&mx<=b.rect.x+b.rect.w&&my>=b.rect.y&&my<=b.rect.y+b.rect.h);
        const bool warn=(b.id==B_NEW&&confirmNew);
        if(!enabled) fillRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,52,58,72,255);
        else if(warn) fillRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,h?214:184,h?84:64,h?64:48,255);
        else fillRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,h?58:34,h?150:108,h?232:184,255);
        fillRect(e,b.rect.x,b.rect.y,b.rect.w,4,255,255,255,h?70:40); fillRect(e,b.rect.x,b.rect.y+b.rect.h-4,b.rect.w,4,0,0,0,60);
        strokeRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,12,20,34,255); if(h)strokeRect(e,b.rect.x-2,b.rect.y-2,b.rect.w+4,b.rect.h+4,235,200,90,255);
        drawText(e,b.label,b.rect.x+b.rect.w/2,b.rect.y+(b.rect.h-28)/2,enabled?SDL_Color{245,245,245,255}:SDL_Color{140,146,160,255},26,true); }
    const char* hint=confirmNew?"Your current save will be replaced  -  Enter or click again to confirm, Esc to cancel"
        :"Enter to play   N new game   S settings   F11 fullscreen   Esc quit";
    drawText(e,hint,cx,e.logicalH()-40,confirmNew?SDL_Color{240,190,150,255}:SDL_Color{190,198,214,255},18,true);
    drawText(e,"v" TOWTYCOON_VERSION,e.logicalW()-70,e.logicalH()-30,SDL_Color{150,158,176,255},16);
    if(settingsOpen){
        const int x=(e.logicalW()-660)/2,y=(e.logicalH()-330)/2;
        fillRect(e,x,y,660,330,18,20,30,250);strokeRect(e,x,y,660,330,235,200,90,255);
        drawText(e,"SETTINGS",cx,y+18,SDL_Color{235,200,90,255},28,true);
        auto& p=e.preferences;
        const std::array<std::string,4> lines{{std::string("[D] Difficulty: ")+(p.difficulty==0?"Easy":p.difficulty==2?"Hard":"Normal"),
            std::string("[W] Weather: ")+(p.weather?"On":"Off"),std::string("[H] HUD hints: ")+(p.hints?"On":"Off"),
            std::string("[A] Weekly autosave: ")+(p.autosave?"On":"Off")}};
        drawText(e,(std::string("[F] Fullscreen: ")+(e.isFullscreen()?"On":"Off")).c_str(),x+360,y+78,SDL_Color{220,230,240,255},22);
        for(int i=0;i<4;++i)drawText(e,lines[i].c_str(),x+40,y+78+i*42,SDL_Color{220,230,240,255},22);
        drawText(e,"Applies when you start or continue. Esc to return.",cx,y+286,SDL_Color{200,210,220,255},18,true);
    }
}

inline int WorldState::zoneAtTileX(int tx) const {
    int c = cityAtWorld((tx + 0.5f) * tile, (mapH * 0.5f) * tile);
    if(c >= 0 && c < (int)cities.size()) return cities[c].zone;
    return tx < mapW/3 ? tt::Z_RES : (tx < 2*mapW/3 ? tt::Z_COM : tt::Z_IND);
}
inline int WorldState::cityAtWorld(float wx, float wy) const {
    int tx = (int)(wx / tile), ty = (int)(wy / tile);
    int best = -1; float bestDist2 = 1e9f;
    for (size_t i = 0; i < cities.size(); ++i) {
        float dx = (float)(cities[i].cx - tx), dy = (float)(cities[i].cy - ty);
        float d2 = dx * dx + dy * dy;
        if (d2 <= (float)(cities[i].radius * cities[i].radius)) return (int)i;
        if (d2 < bestDist2) { bestDist2 = d2; best = (int)i; }
    }
    return best >= 0 ? best : 0;
}
inline const std::string& WorldState::cityNameAtWorld(float wx, float wy) const {
    if (cities.empty()) { static const std::string def = "Towburg Central"; return def; }
    int idx = cityAtWorld(wx, wy);
    return cities[idx].name;
}

inline void WorldState::devBuildRoad(int tx, int ty){
    if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) {
        bool wasWater = (tiles[simulation::tileIndex(tx, ty, mapW)] == tt::WATER);
        tiles[simulation::tileIndex(tx, ty, mapW)] = tt::ROAD;
        flashMsg = wasWater ? "Dev: Built Road Bridge at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")"
                            : "Dev: Built Road at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
        flashTimer = 1.5f;
    }
}
inline bool WorldState::isBridge(int tx, int ty) const {
    if (tx < 0 || tx >= mapW || ty < 0 || ty >= mapH) return false;
    if (tiles[simulation::tileIndex(tx, ty, mapW)] != tt::ROAD) return false;
    bool waterH = (tx > 0 && tiles[simulation::tileIndex(tx - 1, ty, mapW)] == tt::WATER) || (tx + 1 < mapW && tiles[simulation::tileIndex(tx + 1, ty, mapW)] == tt::WATER);
    bool waterV = (ty > 0 && tiles[simulation::tileIndex(tx, ty - 1, mapW)] == tt::WATER) || (ty + 1 < mapH && tiles[simulation::tileIndex(tx, ty + 1, mapW)] == tt::WATER);
    return waterH || waterV;
}
inline void WorldState::devHiggsErode(int tx, int ty){
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int x = tx + dx, y = ty + dy;
            if (x >= 0 && x < mapW && y >= 0 && y < mapH) {
                if (tiles[simulation::tileIndex(x, y, mapW)] == tt::GRASS) {
                    bool nearWater = (x > 0 && tiles[simulation::tileIndex(x - 1, y, mapW)] == tt::WATER) ||
                                     (x + 1 < mapW && tiles[simulation::tileIndex(x + 1, y, mapW)] == tt::WATER) ||
                                     (y > 0 && tiles[simulation::tileIndex(x, y - 1, mapW)] == tt::WATER) ||
                                     (y + 1 < mapH && tiles[simulation::tileIndex(x, y + 1, mapW)] == tt::WATER);
                    if (nearWater) tiles[simulation::tileIndex(x, y, mapW)] = tt::SAND;
                }
            }
        }
    }
    flashMsg = "Dev: Applied Higgs Field Relaxation at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
    flashTimer = 1.5f;
}
inline void WorldState::devBulldoze(int tx, int ty){
    if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) {
        tiles[simulation::tileIndex(tx, ty, mapW)] = tt::GRASS;
        buildings.erase(std::remove_if(buildings.begin(), buildings.end(), [&](const Building& b){
            return tx >= b.tx && tx < b.tx + 3 && ty >= b.ty && ty < b.ty + 3;
        }), buildings.end());
        flashMsg = "Dev: Bulldozed tile (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
        flashTimer = 1.5f;
    }
}
inline void WorldState::devPlaceBuilding(int tx, int ty, int type){
    if (tx >= 0 && tx + 2 < mapW && ty >= 0 && ty + 3 < mapH) {
        int bType = std::clamp(type, 0, 5);
        buildings.push_back({tx, ty, bType});
        for (int x = tx; x < tx + 3; ++x) tiles[simulation::tileIndex(x, ty + 3, mapW)] = tt::SIDEWALK;
        lots.push_back({(tx + 1.5f) * tile, (ty + 4.4f) * tile, -1, 1.0f, bType, 500 + bType * 60});
        flashMsg = "Dev: Placed Building at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
        flashTimer = 1.5f;
    }
}
inline void WorldState::devPlacePark(int tx, int ty, int w, int h){
    for (int y = ty; y < ty + h && y < mapH; ++y) {
        for (int x = tx; x < tx + w && x < mapW; ++x) {
            if (x == tx || x == tx + w - 1 || y == ty || y == ty + h - 1) {
                tiles[simulation::tileIndex(x, y, mapW)] = tt::SIDEWALK;
            } else if (x == tx + w / 2 && y == ty + h / 2) {
                tiles[simulation::tileIndex(x, y, mapW)] = tt::WATER;
            } else {
                tiles[simulation::tileIndex(x, y, mapW)] = ((x + y) % 2 == 0) ? tt::TREE : tt::GRASS;
            }
        }
    }
    flashMsg = "Dev: Created Recreational Park at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
    flashTimer = 1.5f;
}
inline void WorldState::devPlantTrees(int tx, int ty){
    if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) {
        tiles[simulation::tileIndex(tx, ty, mapW)] = tt::TREE;
        flashMsg = "Dev: Planted Trees at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
        flashTimer = 1.5f;
    }
}
inline void WorldState::devPlaceWater(int tx, int ty){
    if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) {
        tiles[simulation::tileIndex(tx, ty, mapW)] = tt::WATER;
        flashMsg = "Dev: Placed Water at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
        flashTimer = 1.5f;
    }
}
inline void WorldState::devPlaceSand(int tx, int ty){
    if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) {
        tiles[simulation::tileIndex(tx, ty, mapW)] = tt::SAND;
        flashMsg = "Dev: Placed Sand at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
        flashTimer = 1.5f;
    }
}
inline void WorldState::devFoundTown(int tx, int ty, const std::string& name){
    cities.push_back({name, tx, ty, 20, 6000, tt::Z_COM, 70, 1, 0});
    flashMsg = "Dev: Founded Town '" + name + "' at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
    flashTimer = 2.5f;
}
inline void WorldState::devSpawnJob(int tx, int ty, int jobType, int jobClass){
    if (jobs.size() < 512) {
        int car = (int)rng.bounded(6);
        jobs.push_back({(tx + 0.5f) * tile, (ty + 0.5f) * tile, 200 + jobClass * 150, true, car, 0, jobClass, zoneAtTileX(tx), jobType, "Dev Call: " + std::string(carWord(car))});
        flashMsg = "Dev: Spawned Call at (" + std::to_string(tx) + ", " + std::to_string(ty) + ")";
        flashTimer = 1.5f;
    }
}
inline bool WorldState::applyDevTool(int tool, int tx, int ty){
    switch(tool){
        case 0: devHiggsErode(tx, ty); return true;
        case 1: devBuildRoad(tx, ty); return true;
        case 2: devBulldoze(tx, ty); return true;
        case 3: devPlaceBuilding(tx, ty, (int)rng.bounded(6)); return true;
        case 4: devPlacePark(tx, ty, 4, 4); return true;
        case 5: devPlantTrees(tx, ty); return true;
        case 6: devPlaceWater(tx, ty); return true;
        case 7: devPlaceSand(tx, ty); return true;
        case 8: devFoundTown(tx, ty, "Town " + std::to_string(cities.size() + 1)); return true;
        case 9: devSpawnJob(tx, ty, (int)rng.bounded(4), (int)rng.bounded(3)); return true;
        default: return false;
    }
}

// ------------------------------ WorldState impl -----------------------------
inline WorldState::WorldState(std::uint64_t randomSeed):rng(randomSeed){
    tiles.assign(simulation::tileCount(mapW, mapH),tt::GRASS); generateCity();
    int ix=48, iy=32;
    truckX=(ix+0.5f)*tile; truckY=(iy+0.5f)*tile; yardX=truckX; yardY=(24+0.5f)*tile;
    rivalYardX=(8+0.5f)*tile; rivalYardY=(8+0.5f)*tile;
    configureRival();
    for(int i=0,placed=0;i<(int)lots.size()&&placed<4;i+=9){ lots[i].owner=1; lots[i].incomeMult=1.0f; placed++; }
    for(int i=0;i<3;++i) spawnJob();
}
// Pick a rival personality and set its books/aggression (difficulty scales its cash).
inline void WorldState::configureRival(){
    rivalKind=(int)rng.bounded(4);
    switch(rivalKind){
        case RK_AGGR:    rivalAggression=0.88; rivalRep=58; break;
        case RK_RELIABLE:rivalAggression=0.45; rivalRep=72; break;
        case RK_OPPORT:  rivalAggression=0.65; rivalRep=60; break;
        default:         rivalAggression=0.70; rivalRep=52; break; // corrupt
    }
    rivalCash = difficulty==0?6000.0 : difficulty==2?12000.0 : 8000.0;
}
inline void WorldState::generateCity(unsigned seed, float revScale){
    std::mt19937 g(seed);
    tiles.assign(simulation::tileCount(mapW, mapH), tt::GRASS);
    buildings.clear();
    lots.clear();

    cities.clear();
    cities.push_back({"Towburg Central", 48, 32, 24, 14500, tt::Z_COM, 75, 2, 0});
    cities.push_back({"Pinehurst Valley", 120, 32, 20, 8800, tt::Z_RES, 70, 1, 0});
    cities.push_back({"Oakridge Heights", 192, 40, 22, 12400, tt::Z_RES, 74, 2, 0});
    cities.push_back({"Port Towton", 48, 136, 22, 9600, tt::Z_IND, 66, 1, 0});
    cities.push_back({"Sunnyside Commercial", 120, 136, 20, 10800, tt::Z_COM, 72, 2, 0});
    cities.push_back({"Ironwood Industrial", 192, 136, 24, 13500, tt::Z_IND, 64, 1, 0});

    // 1. Regional Arterial Highway Grid (OpenTTD-style inter-city road corridors)
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            bool isArterial = (y == 32 || y == 80 || y == 136 || x == 48 || x == 120 || x == 192);
            if (isArterial) {
                tiles[simulation::tileIndex(x, y, mapW)] = tt::ROAD;
            }
        }
    }

    // 2. City Road Grids with Sidewalks
    for (const auto& c : cities) {
        for (int y = std::max(0, c.cy - c.radius); y <= std::min(mapH - 1, c.cy + c.radius); ++y) {
            for (int x = std::max(0, c.cx - c.radius); x <= std::min(mapW - 1, c.cx + c.radius); ++x) {
                int dx = x - c.cx, dy = y - c.cy;
                if (dx * dx + dy * dy <= c.radius * c.radius) {
                    if (x % kRoadSpacing == 0 || y % kRoadSpacing == 0) {
                        tiles[simulation::tileIndex(x, y, mapW)] = tt::ROAD;
                    } else if (x % kRoadSpacing == 1 || x % kRoadSpacing == kRoadSpacing - 1 ||
                               y % kRoadSpacing == 1 || y % kRoadSpacing == kRoadSpacing - 1) {
                        if (tiles[simulation::tileIndex(x, y, mapW)] != tt::ROAD) {
                            tiles[simulation::tileIndex(x, y, mapW)] = tt::SIDEWALK;
                        }
                    }
                }
            }
        }
    }

    // 3. Natural Waterways & Harbor (OpenTTD-style coastal harbor and scenic lakes)
    for (int y = 108; y <= 164 && y < mapH; ++y) {
        for (int x = 0; x < 18 && x < mapW; ++x) {
            tiles[simulation::tileIndex(x, y, mapW)] = tt::WATER;
        }
        if (18 < mapW && tiles[simulation::tileIndex(18, y, mapW)] == tt::GRASS) {
            tiles[simulation::tileIndex(18, y, mapW)] = tt::SAND;
        }
    }
    int lx = (seed % 2 == 0) ? 80 : 152;
    int ly = (seed % 3 == 0) ? 76 : 84;
    for (int y = ly; y < ly + 6 && y < mapH; ++y) {
        for (int x = lx; x < lx + 10 && x < mapW; ++x) {
            tiles[simulation::tileIndex(x, y, mapW)] = tt::WATER;
        }
    }
    for (int y = ly - 1; y <= ly + 6 && y < mapH; ++y) {
        for (int x = lx - 1; x <= lx + 10 && x < mapW; ++x) {
            if (x >= 0 && y >= 0 && tiles[simulation::tileIndex(x, y, mapW)] == tt::GRASS) {
                tiles[simulation::tileIndex(x, y, mapW)] = tt::SAND;
            }
        }
    }

    // 4. Urban Dense Blocks & Dedicated Recreational Parks
    for (const auto& c : cities) {
        int parkBx = c.cx - kRoadSpacing;
        int parkBy = c.cy - kRoadSpacing;
        for (int by = c.cy - c.radius + 4; by <= c.cy + c.radius - 8; by += kRoadSpacing) {
            for (int bx = c.cx - c.radius + 4; bx <= c.cx + c.radius - 8; bx += kRoadSpacing) {
                if (bx < 4 || by < 4 || bx + 7 >= mapW || by + 7 >= mapH) continue;
                int dx = bx - c.cx, dy = by - c.cy;
                if (dx * dx + dy * dy > (c.radius - 2) * (c.radius - 2)) continue;

                // Dedicated Recreational Park
                if (bx == parkBx && by == parkBy) {
                    for (int py = by + 1; py <= by + 6; ++py) {
                        for (int px = bx + 1; px <= bx + 6; ++px) {
                            if (px == bx + 1 || px == bx + 6 || py == by + 1 || py == by + 6) {
                                tiles[simulation::tileIndex(px, py, mapW)] = tt::SIDEWALK;
                            } else {
                                tiles[simulation::tileIndex(px, py, mapW)] = tt::GRASS;
                            }
                        }
                    }
                    tiles[simulation::tileIndex(bx + 3, by + 3, mapW)] = tt::WATER;
                    tiles[simulation::tileIndex(bx + 4, by + 3, mapW)] = tt::SIDEWALK;
                    tiles[simulation::tileIndex(bx + 3, by + 4, mapW)] = tt::SIDEWALK;
                    tiles[simulation::tileIndex(bx + 2, by + 2, mapW)] = tt::TREE;
                    tiles[simulation::tileIndex(bx + 5, by + 2, mapW)] = tt::TREE;
                    tiles[simulation::tileIndex(bx + 2, by + 5, mapW)] = tt::TREE;
                    tiles[simulation::tileIndex(bx + 5, by + 5, mapW)] = tt::TREE;
                    continue;
                }

                // Dense Urban Building Block: Wall-to-wall buildings with NO gaps!
                if (g() % 100 < 85) {
                    int type1 = (int)(g() % 6);
                    int tx1 = bx + 1, ty1 = by + 2;
                    buildings.push_back({tx1, ty1, type1});
                    for (int xx = tx1; xx < tx1 + 3; ++xx) {
                        tiles[simulation::tileIndex(xx, ty1 + 3, mapW)] = tt::SIDEWALK;
                    }
                    lots.push_back({(tx1 + 1.5f) * tile, (ty1 + 4.4f) * tile, -1, 1.0f, type1, (int)((400 + type1 * 60) * revScale)});

                    int type2 = (int)(g() % 6);
                    int tx2 = bx + 4, ty2 = by + 2;
                    buildings.push_back({tx2, ty2, type2});
                    for (int xx = tx2; xx < tx2 + 3; ++xx) {
                        tiles[simulation::tileIndex(xx, ty2 + 3, mapW)] = tt::SIDEWALK;
                    }
                    lots.push_back({(tx2 + 1.5f) * tile, (ty2 + 4.4f) * tile, -1, 1.0f, type2, (int)((400 + type2 * 60) * revScale)});
                } else {
                    for (int py = by + 1; py <= by + 6; ++py) {
                        for (int px = bx + 1; px <= bx + 6; ++px) {
                            tiles[simulation::tileIndex(px, py, mapW)] = (px % 2 == 0 && py % 2 == 0) ? tt::TREE : tt::SIDEWALK;
                        }
                    }
                }
            }
        }
    }

    // 5. Countryside nature (scattered tree clumps outside city boundaries)
    for (int k = 0; k < 120; ++k) {
        int tx = 8 + (int)(g() % (mapW - 16));
        int ty = 8 + (int)(g() % (mapH - 16));
        if (tiles[simulation::tileIndex(tx, ty, mapW)] == tt::GRASS) {
            tiles[simulation::tileIndex(tx, ty, mapW)] = tt::TREE;
            if (tx + 1 < mapW && tiles[simulation::tileIndex(tx + 1, ty, mapW)] == tt::GRASS && (g() % 2 == 0)) {
                tiles[simulation::tileIndex(tx + 1, ty, mapW)] = tt::TREE;
            }
        }
    }
}
// Expand into a fresh, tougher district. Keeps your company (cash, fleet, drivers,
// reputation, loans, tows); regenerates the map and installs a new rival.
inline void WorldState::startDistrict(int n){
    if(n<1||n>simulation::maxDistrict){flashMsg="Final district reached - continue free play here";flashTimer=4;return;}
    if(!carried.empty()||std::any_of(autoTrucks.begin(),autoTrucks.end(),[](const auto& t){return t.phase!=0;})){
        pendingDistrict=n;flashMsg="Expansion queued - fleet finishing deliveries; deliver your cargo";flashTimer=5;return;
    }
    autoTrucks.clear();pendingDistrict=0;
    district=n;
    tiles.assign(simulation::tileCount(mapW, mapH),tt::GRASS); buildings.clear(); lots.clear();
    generateCity(99u + (unsigned)n*7u, 1.0f + 0.25f*(n-1));
    int ix=48, iy=32;
    truckX=(ix+0.5f)*tile; truckY=(iy+0.5f)*tile; yardX=truckX; yardY=(24+0.5f)*tile;
    rivalYardX=(8+0.5f)*tile; rivalYardY=(8+0.5f)*tile;
    wonDistrict=false; announcedMajority=false; rivalStake=0.0;
    configureRival();
    rivalCash *= (1.0 + 0.6*(n-1));                 // new rival starts richer each district
    rivalMaxLots=40; rivalTrucks.clear();
    int foot=3+2*(n-1); for(int i=0,placed=0;i<(int)lots.size()&&placed<foot;i+=7){ lots[i].owner=1; lots[i].incomeMult=1.0f; placed++; }
    jobs.clear(); carried.clear(); for(int i=0;i<3;++i) spawnJob();
    toastMsg=std::string("Welcome to District ")+std::to_string(n)+"!";
    toastSub=std::string(rivalCompanyName(rivalKind))+" ("+rivalKindName(rivalKind)+") runs this turf - take it from them."; toastTimer=8.0f;
}
inline int WorldState::tileAtWorld(float wx,float wy) const { int tx=(int)std::floor(wx/tile),ty=(int)std::floor(wy/tile); if(tx<0||tx>=mapW||ty<0||ty>=mapH)return -1; return tiles[simulation::tileIndex(tx, ty, mapW)]; }
inline void WorldState::worldToScreen(Engine& e,float wx,float wy,int& sx,int& sy) const { float z=e.camera.zoom; sx=(int)std::round((wx-e.camera.x)*z+e.logicalW()*0.5f); sy=(int)std::round((wy-e.camera.y)*z+e.logicalH()*0.5f); }
inline std::string WorldState::streetIntersectionAt(float wx,float wy) const {
    static const std::array<const char*,16> eastWest{{
        "Main St","Oak Ave","Pine St","Maple Ave","Harbor Blvd","Elm Rd",
        "Cedar St","Sunset Dr","2nd St","Industrial Way","Bayview Ave",
        "Broadway","Foundry Way","Highland Blvd","River Rd","Parkway"
    }};
    static const std::array<const char*,16> northSouth{{
        "1st Ave","Market St","River Rd","Broadway","Hill St","Union Ave",
        "Lake Dr","Depot St","Port Way","Commerce Blvd","Summit Ave",
        "Timberline Rd","Center St","Exchange St","Factory Rd","Grand Ave"
    }};
    const int xRoads=(mapW-1)/kRoadSpacing+1, yRoads=(mapH-1)/kRoadSpacing+1;
    const int xIndex=std::clamp((int)std::round(wx/(tile*kRoadSpacing)),0,xRoads-1);
    const int yIndex=std::clamp((int)std::round(wy/(tile*kRoadSpacing)),0,yRoads-1);
    std::string loc = std::string(eastWest[yIndex % eastWest.size()])
        +" & "+northSouth[xIndex % northSouth.size()];
    int c = cityAtWorld(wx, wy);
    if(c >= 0 && c < (int)cities.size()) {
        loc += " (" + cities[c].name + ")";
    }
    return loc;
}
inline void WorldState::drawMinimap(Engine& e,float targetX,float targetY,bool hasTarget) const {
    const int panelW=216,panelH=176,panelX=e.logicalW()-panelW-10,panelY=54;
    fillRect(e,panelX,panelY,panelW,panelH,12,16,24,235);
    strokeRect(e,panelX,panelY,panelW,panelH,80,140,180,240);
    drawText(e,"REGIONAL RADAR",panelX+10,panelY+4,SDL_Color{220,230,245,255},12);
    const int mapX=panelX+10,mapY=panelY+20,mapWpx=196,mapHpx=126;
    fillRect(e,mapX,mapY,mapWpx,mapHpx,24,32,32,255);

    for(int py=0; py<mapHpx; py+=2){
        int ty = py * mapH / mapHpx;
        for(int px=0; px<mapWpx; px+=2){
            int tx = px * mapW / mapWpx;
            int tileType = tiles[simulation::tileIndex(tx, ty, mapW)];
            if(tileType == tt::ROAD){
                fillRect(e, mapX+px, mapY+py, 2, 2, 130, 138, 145, 255);
            } else if(tileType == tt::WATER){
                fillRect(e, mapX+px, mapY+py, 2, 2, 45, 100, 160, 255);
            } else if(tileType == tt::SAND){
                fillRect(e, mapX+px, mapY+py, 2, 2, 180, 170, 120, 255);
            }
        }
    }
    for(const auto& c : cities){
        int cx = mapX + std::clamp((int)(c.cx * (float)tile / (mapW * tile) * mapWpx), 0, mapWpx - 1);
        int cy = mapY + std::clamp((int)(c.cy * (float)tile / (mapH * tile) * mapHpx), 0, mapHpx - 1);
        fillRect(e, cx - 2, cy - 2, 4, 4, 255, 205, 70, 255);
    }

    auto marker=[&](float wx,float wy,int size,SDL_Color color){
        const int px=mapX+std::clamp((int)(wx/(mapW*(float)tile)*mapWpx),0,mapWpx-1);
        const int py=mapY+std::clamp((int)(wy/(mapH*(float)tile)*mapHpx),0,mapHpx-1);
        fillRect(e,px-size/2,py-size/2,size,size,color.r,color.g,color.b,color.a);
    };
    for(const auto& t : autoTrucks) if(t.active) marker(t.x, t.y, 4, SDL_Color{80,230,120,255});
    for(const auto& r : rivalTrucks) if(r.active) marker(r.x, r.y, 4, SDL_Color{240,90,90,255});
    if(hasTarget) marker(targetX,targetY,7,SDL_Color{255,80,75,255});
    marker(truckX,truckY,6,SDL_Color{255,235,100,255});
    strokeRect(e,mapX,mapY,mapWpx,mapHpx,150,180,200,255);

    fillRect(e,panelX+12,panelY+154,6,6,255,235,100,255);
    drawText(e,"YOU",panelX+22,panelY+151,SDL_Color{220,228,240,255},11);
    fillRect(e,panelX+60,panelY+154,6,6,255,80,75,255);
    drawText(e,"TARGET",panelX+70,panelY+151,SDL_Color{220,228,240,255},11);
    fillRect(e,panelX+126,panelY+154,6,6,80,230,120,255);
    drawText(e,"FLEET",panelX+136,panelY+151,SDL_Color{220,228,240,255},11);
}
inline int WorldState::nearestActiveJob() const { int best=-1; float bd=1e18f; for(size_t i=0;i<jobs.size();++i){ if(!jobs[i].active)continue; float dx=jobs[i].x-truckX,dy=jobs[i].y-truckY,d=dx*dx+dy*dy; if(d<bd){bd=d;best=(int)i;} } return best; }
inline int WorldState::nearestOpenLot() const { int best=-1; float bd=56.0f*56.0f; for(size_t i=0;i<lots.size();++i){ if(lots[i].owner!=-1)continue; float dx=lots[i].cx-truckX,dy=lots[i].cy-truckY,d=dx*dx+dy*dy; if(d<bd){bd=d;best=(int)i;} } return best; }
inline void WorldState::spawnJob(){
    if(jobs.size()>=512)return;
    const bool hasRoadsideVan=std::find(ownedTrucks.begin(),ownedTrucks.end(),4)!=ownedTrucks.end();
    std::optional<simulation::GeneratedJob> job;
    if(hasRoadsideVan && rng.bounded(4)==0)
        job=simulation::generateRoadsideJob(tiles,mapW,mapH,tt::ROAD,tile,truckX,truckY,rng);
    else
        job=simulation::generateJob(
            tiles,mapW,mapH,tt::ROAD,tile,truckX,truckY,fleetMaxClass(),reputation,rng);
    if(!job) return;
    jobs.push_back({job->x,job->y,job->payout,true,job->carType,job->angle,
                    job->jobClass,job->zone,job->jobType,job->description});
}
// A* over road tiles. Snaps start/goal to nearest road. Returns tile coords start->goal.
inline std::vector<std::pair<int,int>> WorldState::findPath(int sx,int sy,int gx,int gy) const {
    return simulation::findRoadPath(tiles,mapW,mapH,tt::ROAD,sx,sy,gx,gy);
}
// One idle+staffed truck drives itself: A* to a job, then to the yard, earning on delivery.
// Shared drive loop: seek a serviceable job, A* to it, A* to home yard, earn on delivery.
inline void WorldState::stepTruck(AutoTruck& t, bool rival, float dt){
    // personality tweaks how fast rivals drive and which jobs they chase
    float kindSpeed = 1.0f; bool chaseValue=false;
    if(rival){ if(t.kind==RK_AGGR){kindSpeed=1.15f;chaseValue=true;} else if(t.kind==RK_OPPORT){chaseValue=true;} else if(t.kind==RK_CORRUPT){kindSpeed=1.10f;} }
    const float asp=150.0f*weatherSpeedMult()*kindSpeed;
    const float homeX=rival?rivalYardX:yardX, homeY=rival?rivalYardY:yardY;
    const int amax = rival ? tt::MEDIUM : truckCatalog()[t.model].maxClass;
    auto startRoute=[&](float gx,float gy){ t.route=findPath((int)(t.x/tile),(int)(t.y/tile),(int)(gx/tile),(int)(gy/tile)); t.ri=0; };
    if(t.phase==0){
        int best=-1; float bestScore=-1e18f;
        for(size_t i=0;i<jobs.size();++i){ if(!jobs[i].active||jobs[i].jclass>amax)continue;
            if(jobs[i].jtype==JT_ROADSIDE && (rival || t.model!=4)) continue;
            float dx=jobs[i].x-t.x,dy=jobs[i].y-t.y,d2=dx*dx+dy*dy;
            // chaseValue rivals grab the fattest payout; others take the nearest call
            float score = chaseValue ? ((float)jobs[i].payout - 0.00004f*d2) : (-d2);
            if(score>bestScore){bestScore=score;best=(int)i;} }
        if(best>=0){ t.carType=jobs[best].carType; t.payout=jobs[best].payout; t.jclass=jobs[best].jclass; t.jobType=jobs[best].jtype; startRoute(jobs[best].x,jobs[best].y); if(!t.route.empty()){jobs.erase(jobs.begin()+best);t.phase=1;} }
        return;
    }
    if(t.ri<(int)t.route.size()){ float tx=(t.route[t.ri].first+0.5f)*tile, ty=(t.route[t.ri].second+0.5f)*tile; float dx=tx-t.x,dy=ty-t.y,dist=std::sqrt(dx*dx+dy*dy);
        if(dist<3.0f){ t.ri++; } else { float ux=dx/dist,uy=dy/dist; t.x+=ux*asp*dt; t.y+=uy*asp*dt; t.facing=(std::abs(ux)>std::abs(uy))?(ux>0?tt::EAST:tt::WEST):(uy>0?tt::SOUTH:tt::NORTH); }
    } else {
        if(t.phase==1 && t.jobType==JT_ROADSIDE){
            if(!rival){
                cash+=t.payout; weeklyRevenueAccum+=t.payout; reputation=std::min(100.0f,reputation+0.25f);
                int cIdx = cityAtWorld(t.x, t.y);
                if(cIdx >= 0 && cIdx < (int)cities.size()) cities[cIdx].towsCompleted++;
            }
            else rivalCash+=t.payout;
            t.phase=0;
            spawnJob();
        } else if(t.phase==1){ startRoute(homeX,homeY); t.phase=2; if(t.route.empty())t.phase=0; }
        else {
            if(rival) rivalCash+=t.payout;
            else {
                cash+=t.payout; weeklyRevenueAccum+=t.payout; tows++; reputation=std::min(100.0f,reputation+0.5f);
                yardImpoundCount++;
                int cIdx = cityAtWorld(t.x, t.y);
                if(cIdx >= 0 && cIdx < (int)cities.size()) cities[cIdx].towsCompleted++;
            }
            t.phase=0;
            spawnJob();
        }
    }
}
inline void WorldState::updateAutoTruck(Engine&, float dt){
    // Inventory slots distinguish identical models and reserve each vehicle once.
    std::vector<int> candidates;
    for(int i=0;i<(int)ownedTrucks.size();++i)if(i!=drivenSlot)candidates.push_back(i);
    std::stable_sort(candidates.begin(),candidates.end(),[&](int a,int b){return (ownedTrucks[a]==4)>(ownedTrucks[b]==4);});
    if(pendingDistrict==0)for(auto& t:autoTrucks){
        if(t.phase!=0||t.model==4)continue;
        for(int slot:candidates)if(ownedTrucks[slot]==4 && std::none_of(autoTrucks.begin(),autoTrucks.end(),[&](const auto& other){return other.slot==slot;})){
            t=AutoTruck{};t.active=true;t.slot=slot;t.model=4;t.x=yardX;t.y=yardY;break;
        }
    }
    for(int slot:candidates){
        if(pendingDistrict || (int)autoTrucks.size()>=drivers)break;
        if(slot==drivenSlot||std::any_of(autoTrucks.begin(),autoTrucks.end(),[&](const auto& t){return t.slot==slot;}))continue;
        AutoTruck t; t.active=true;t.slot=slot;t.model=ownedTrucks[slot];t.x=yardX;t.y=yardY;
        autoTrucks.push_back(t);
    }
    for(auto& t:autoTrucks)if(pendingDistrict==0||t.phase!=0)stepTruck(t,false,dt);
}
inline void WorldState::switchTruck(int delta){
    if(!carried.empty()){flashMsg="Deliver your cargo before changing trucks";flashTimer=3;return;}
    const int count=(int)ownedTrucks.size();
    for(int offset=1;offset<count;++offset){
        int target=(drivenSlot+delta*offset+count)%count;
        auto it=std::find_if(autoTrucks.begin(),autoTrucks.end(),[&](const auto& t){return t.slot==target;});
        if(it!=autoTrucks.end()&&it->phase!=0)continue;
        if(it!=autoTrucks.end())autoTrucks.erase(it);
        drivenSlot=target;return;
    }
    flashMsg="Other trucks are working deliveries";flashTimer=3;
}
inline void WorldState::updateRivals(float dt){
    if(wonDistrict){ rivalTrucks.clear(); return; }
    int fleet = rivalKind==RK_AGGR?3:2;   // aggressive rival runs a bigger fleet
    if((int)rivalTrucks.size()!=fleet){
        rivalTrucks.clear();
        const int mdl[3]={0,5,2};
        for(int i=0;i<fleet;++i){ AutoTruck r; r.active=true; r.model=mdl[i%3]; r.kind=rivalKind; r.x=rivalYardX; r.y=rivalYardY; r.phase=0; rivalTrucks.push_back(r); }
    }
    for(auto& r:rivalTrucks) stepTruck(r,true,dt);
}
inline void WorldState::advanceGoals(Engine&){
    if(tows>=6&&!marketplaceUnlocked){marketplaceUnlocked=true;toastTimer=7;toastMsg="Business Milestone: 6 Successful Tows!";toastSub="Vehicle marketplace unlocked - press M";}
    int mine,rival,open; countLots(mine,rival,open);
    auto met=[&](int gi)->bool{ switch(gi){case 0:return tows>=6;case 1:return (int)ownedTrucks.size()>=2&&drivers>=1;case 2:return mine>=1;case 3:return mine*2>(mine+rival+open);default:return false;} };
    while (goal<4 && met(goal)){ goal++; flashMsg="Goal complete!"; flashTimer=3.0f; }
}
inline void WorldState::advanceClock(Engine& e, float dt){
    if (newsTimer > 0.0f) newsTimer = std::max(0.0f, newsTimer - dt);
    if (panel!=P_NONE) return;
    dayAccum += dt;
    while (dayAccum>=kSecondsPerDay){ dayAccum-=kSecondsPerDay; day++;
        if (day>7){ day=1; week++;
            for(auto& c : cities){
                c.population += 100 + (int)rng.bounded(200);
                if(c.towsCompleted >= 2){
                    c.prosperity = std::min(100, c.prosperity + 3);
                    c.councilRating = std::min(1000, c.councilRating + 25);
                    c.towsCompleted = 0;
                    if(c.growthLevel < 5 && c.prosperity >= 65 + c.growthLevel * 7){
                        c.growthLevel++;
                        c.population += 400 + (int)rng.bounded(600);
                        for(auto& l : lots){
                            if(cityAtWorld(l.cx, l.cy) == (&c - &cities[0])){
                                l.incomeMult = std::min(2.5f, l.incomeMult + 0.10f);
                            }
                        }
                        postNews("TOWN EXPANSION: " + c.name + " upgraded to Tier " + std::to_string(c.growthLevel) + "! (" + std::to_string(c.population) + " residents)", SDL_Color{80,240,120,255});
                    }
                } else {
                    if(c.councilRating > 450) c.councilRating -= 15;
                }
            }
            rivalWeeklyTurn();      // AI competitor bids/claims lots and runs its books
            weeklyFinances(e);      // your wages, lot upkeep, loan payments, P&L
            if (week>4){ week=1; simulation::finishCreditMonth(cash,positiveMonths,monthPositive); month++; if(month>12){month=1;year++;} }
            std::string saveError;
            const bool saved=!optAutosave||saveProtected||saveGame(e,&saveError);
            flashMsg = saveProtected ? "Save protected - restore your save before continuing" : !optAutosave ? "New week: rival moved, bills paid"
                : saved ? "New week: rival moved, bills paid - autosaved"
                : "Autosave failed: "+saveError;
            flashTimer=2.6f;
        }
    }
}
// Adapted from the shared TowingCompetitorAI: bid high-value open lots, be
// aggressive when flush, cautious when low on cash; then run weekly economics.
inline void WorldState::rivalWeeklyTurn(){
    simulation::RivalEconomyState state{rivalCash,rivalRep,rivalAggression,rivalMaxLots,rivalKind,{}};
    state.lots.reserve(lots.size());
    for(const auto& lot:lots)
        state.lots.push_back({lot.owner,lot.baseRev,lot.incomeMult});
    simulation::applyRivalWeeklyTurn(state,rng);
    rivalCash=state.cash;
    rivalRep=state.reputation;
    for(std::size_t i=0;i<lots.size();++i){
        lots[i].owner=state.lots[i].owner;
        lots[i].incomeMult=state.lots[i].incomeMultiplier;
    }
}
inline void WorldState::weeklyFinances(Engine&){
    int mine,rival,open; countLots(mine,rival,open);
    simulation::WeeklyFinanceState finance{cash,weeklyRevenueAccum,lastWeekRevenue,lastWeekExpense,reputation,{}};
    finance.loans.reserve(loans.size());
    for(const auto& loan:loans) finance.loans.push_back({loan.balance,loan.weeklyPayment});
    simulation::applyWeeklyFinances(finance,drivers,(int)ownedTrucks.size(),mine,
        diffExpenseMult(),kDriverWageWk,kTruckUpkeepWk,kLotUpkeepWk);
    cash=finance.cash;
    weeklyRevenueAccum=finance.weeklyRevenueAccum;
    lastWeekRevenue=finance.lastWeekRevenue;
    lastWeekExpense=finance.lastWeekExpense;
    reputation=finance.reputation;
    loans.clear();
    loans.reserve(finance.loans.size());
    for(const auto& loan:finance.loans) loans.push_back({loan.balance,loan.weeklyPayment});
    if(isPublic && sharesOut>0){ double net=lastWeekRevenue-lastWeekExpense; double sentiment = net>0?1.01:0.985;
        sharePrice = std::max(0.01, ((double)computeValuation()/sharesOut)*sentiment); }
    observeCash();
    if(cash<0){ flashMsg="Cash is negative - cut costs or earn more!"; flashTimer=3.0f; }
}
inline long WorldState::computeValuation() const {
    const auto& cat=truckCatalog(); long trucksResale=0; for(int id:ownedTrucks) trucksResale+=(long)(cat[id].price*0.5);
    int mine,rival,open; countLots(mine,rival,open); long lotsValue=(long)mine*1500, debt=0; for(auto&L:loans) debt+=L.balance;
    return (long)cash + trucksResale + lotsValue - debt;
}
inline bool WorldState::saveGame(Engine& e,std::string* error){
    if(e.saveDir().empty()){
        if(error) *error="OS user-data directory is unavailable";
        e.logInfo("Save failed: OS user-data directory is unavailable");
        return false;
    }
    const std::filesystem::path path=std::filesystem::path(e.saveDir())/"savegame.tt";
    auto fail=[&](const std::string& detail){
        if(error) *error=detail;
        e.logInfo("Save failed ("+path.string()+"): "+detail);
        return false;
    };
    if(saveProtected){
        std::error_code checkError;
        const bool original=std::filesystem::exists(path,checkError);
        if(checkError)return fail("Cannot inspect protected save: "+checkError.message());
        const bool backup=std::filesystem::exists(path.string()+".bak",checkError);
        if(checkError)return fail("Cannot inspect protected backup: "+checkError.message());
        if(original||backup)return fail("Existing save could not be read; restore it or move it aside before saving");
        saveProtected=false;
    }
    observeCash();
    persistence::SaveGameData data;
    data.runtime.jobs=jobs;data.runtime.carried=carried;data.runtime.autoTrucks=autoTrucks;data.runtime.rivalTrucks=rivalTrucks;
    data.runtime.weather=weather;data.runtime.weatherTimer=weatherTimer;data.runtime.spawnTimer=spawnTimer;data.runtime.pulse=pulse;
    data.runtime.announcedMajority=announcedMajority;data.runtime.rivalAggression=rivalAggression;data.runtime.rivalMaxLots=rivalMaxLots;
    data.runtime.positiveMonths=positiveMonths;data.runtime.monthPositive=monthPositive;data.runtime.pendingDistrict=pendingDistrict;
    data.district=district; data.cash=cash; data.tows=tows; data.goal=goal; data.drivers=drivers;
    data.drivenSlot=drivenSlot; data.marketplaceUnlocked=marketplaceUnlocked;
    data.truckX=truckX; data.truckY=truckY; data.facing=facing;
    data.day=day; data.week=week; data.month=month; data.year=year; data.dayAccum=dayAccum;
    data.ownedTrucks=ownedTrucks;
    for(const auto& lot:lots) data.lots.push_back({lot.owner,lot.incomeMult});
    data.reputation=reputation; data.rivalCash=rivalCash; data.rivalReputation=rivalRep;
    data.weeklyRevenueAccum=weeklyRevenueAccum; data.lastWeekRevenue=lastWeekRevenue;
    data.lastWeekExpense=lastWeekExpense;
    for(const auto& loan:loans) data.loans.push_back({loan.balance,loan.weeklyPayment});
    data.isPublic=isPublic; data.sharePrice=sharePrice; data.sharesOut=sharesOut;
    data.youOwnFraction=youOwnFrac; data.rivalStake=rivalStake; data.wonDistrict=wonDistrict;
    data.rivalKind=rivalKind; data.difficulty=difficulty; data.optHints=optHints;
    data.optWeather=optWeather; data.optAutosave=optAutosave; data.rngState=rng.state();
    std::string serialized, codecError;
    if(!persistence::encodeSaveGame(data,mapW,mapH,tile,serialized,codecError))
        return fail(codecError);
    std::string storageError;
    if(!SaveStore::writeAtomically(path,serialized,storageError,preserveBackup)) return fail(storageError);
    if(!storageError.empty()) e.logInfo("Save completed with cleanup warning: "+storageError);
    if(error) error->clear();
    preserveBackup=false;
    e.logInfo("Game saved: "+path.string());
    return true;
}
inline bool WorldState::loadGame(Engine& e,std::string* error){
    if(e.saveDir().empty()){
        if(error) *error="OS user-data directory is unavailable";
        e.logInfo("Load failed: OS user-data directory is unavailable");
        return false;
    }
    const std::filesystem::path path=std::filesystem::path(e.saveDir())/"savegame.tt";
    auto fail=[&](const std::string& detail){
        if(error) *error=detail;
        e.logInfo("Load failed ("+path.string()+"): "+detail);
        return false;
    };
    std::string contents,firstError;
    persistence::SaveGameData decodedSave;
    std::filesystem::path loadedFromPath;
    const std::filesystem::path backupPath=path.string()+".bak";
    std::vector<std::filesystem::path> candidates{path,backupPath};
    std::error_code ec;
    bool mainExists=std::filesystem::exists(path,ec);
    if(ec)return fail("cannot inspect save: "+ec.message());
    bool backupExists=std::filesystem::exists(backupPath,ec);
    if(ec)return fail("cannot inspect backup: "+ec.message());
    if(!mainExists && !backupExists){
        const auto legacy=std::filesystem::path(e.baseDir())/"savegame.tt";
        if(legacy!=path)candidates.push_back(legacy);
    }
    for(const auto& candidate:candidates){
        bool exists=std::filesystem::exists(candidate,ec);
        if(ec)return fail("cannot inspect save: "+ec.message());
        if(!exists)continue;
        std::string detail;
        if(SaveStore::readAll(candidate,contents,detail)
           && persistence::decodeSaveGame(contents,decodedSave,mapW,mapH,tile,detail)){
            loadedFromPath=candidate;break;
        }
        if(firstError.empty())firstError=detail;
    }
    if(loadedFromPath.empty()){
        if(!firstError.empty())return fail(firstError);
        if(error)error->clear();
        return false;
    }
    WorldState loaded=*this;
    loaded.preserveBackup=loadedFromPath==backupPath;
    loaded.district=decodedSave.district;
    loaded.tiles.assign(simulation::tileCount(loaded.mapW, loaded.mapH),tt::GRASS);
    loaded.buildings.clear();
    loaded.lots.clear();
    const unsigned citySeed=loaded.district==1?99u:99u+(unsigned)loaded.district*7u;
    loaded.generateCity(citySeed,1.0f+0.25f*(loaded.district-1));
    loaded.cash=decodedSave.cash; loaded.tows=decodedSave.tows; loaded.goal=decodedSave.goal;
    loaded.drivers=decodedSave.drivers; loaded.drivenSlot=decodedSave.drivenSlot;
    loaded.marketplaceUnlocked=decodedSave.marketplaceUnlocked; loaded.truckX=decodedSave.truckX;
    loaded.truckY=decodedSave.truckY; loaded.facing=decodedSave.facing;
    loaded.day=decodedSave.day; loaded.week=decodedSave.week; loaded.month=decodedSave.month;
    loaded.year=decodedSave.year; loaded.dayAccum=decodedSave.dayAccum;
    loaded.ownedTrucks=decodedSave.ownedTrucks; loaded.reputation=decodedSave.reputation;
    loaded.rivalCash=decodedSave.rivalCash; loaded.rivalRep=decodedSave.rivalReputation;
    loaded.weeklyRevenueAccum=decodedSave.weeklyRevenueAccum;
    loaded.lastWeekRevenue=decodedSave.lastWeekRevenue; loaded.lastWeekExpense=decodedSave.lastWeekExpense;
    loaded.loans.clear();
    for(const auto& loan:decodedSave.loans) loaded.loans.push_back({loan.balance,loan.weeklyPayment});
    loaded.isPublic=decodedSave.isPublic; loaded.sharePrice=decodedSave.sharePrice;
    loaded.sharesOut=decodedSave.sharesOut; loaded.youOwnFrac=decodedSave.youOwnFraction;
    loaded.rivalStake=decodedSave.rivalStake; loaded.wonDistrict=decodedSave.wonDistrict;
    loaded.rivalKind=decodedSave.rivalKind; loaded.difficulty=decodedSave.difficulty;
    loaded.optHints=decodedSave.optHints; loaded.optWeather=decodedSave.optWeather;
    loaded.optAutosave=decodedSave.optAutosave; loaded.rng.setState(decodedSave.rngState);
    if(decodedSave.decodedLots){
        if(decodedSave.decodedLots->size()==loaded.lots.size()){
            for(std::size_t i=0;i<loaded.lots.size();++i){
                loaded.lots[i].owner=(*decodedSave.decodedLots)[i].owner;
                loaded.lots[i].incomeMult=(*decodedSave.decodedLots)[i].incomeMultiplier;
            }
        }else return fail("Saved city does not match this build; original save has been preserved");
    }

    loaded.rivalTrucks.clear();loaded.autoTrucks.clear();loaded.carried.clear();loaded.jobs.clear();
    if(decodedSave.hasRuntime){
        const auto& r=decodedSave.runtime;
        loaded.jobs=r.jobs;loaded.carried=r.carried;loaded.autoTrucks=r.autoTrucks;loaded.rivalTrucks=r.rivalTrucks;
        loaded.weather=r.weather;loaded.weatherTimer=r.weatherTimer;loaded.spawnTimer=r.spawnTimer;loaded.pulse=r.pulse;
        loaded.announcedMajority=r.announcedMajority;loaded.rivalAggression=r.rivalAggression;loaded.rivalMaxLots=r.rivalMaxLots;
        loaded.positiveMonths=r.positiveMonths;loaded.monthPositive=r.monthPositive;loaded.pendingDistrict=r.pendingDistrict;
    }else{
        loaded.pendingDistrict=0;loaded.positiveMonths=0;loaded.monthPositive=loaded.cash>0 && loaded.day==1 && loaded.week==1 && loaded.dayAccum==0;
        loaded.weather=W_CLEAR;loaded.weatherTimer=30;loaded.spawnTimer=0;loaded.pulse=0;
        loaded.rivalAggression=loaded.rivalKind==RK_AGGR?0.88:loaded.rivalKind==RK_RELIABLE?0.45:loaded.rivalKind==RK_OPPORT?0.65:0.70;
        for(int i=0;i<3;++i)loaded.spawnJob();
    }
    loaded.saveProtected=false;loaded.flashTimer=0;loaded.toastTimer=0;loaded.panel=P_NONE;loaded.bidLot=-1;
    loaded.loadedOnce=true;
    *this=std::move(loaded);
    if(error) error->clear();
    if(loadedFromPath!=path && !preserveBackup){
        std::string migrationError;
        if(SaveStore::writeAtomically(path,contents,migrationError))
            e.logInfo("Migrated legacy save to "+path.string());
        else
            e.logInfo("Legacy save loaded, but migration failed: "+migrationError);
    }
    e.logInfo("Game loaded: "+loadedFromPath.string());
    return true;
}
inline void WorldState::ensureLoaded(Engine& e){
    if (!loadedOnce){
        loadedOnce=true;
        std::string loadError;
        if(freshStart){ /* new game requested from the menu */ }
        else if(loadGame(e,&loadError)){ toastMsg=preserveBackup?"Recovered backup save":"Welcome back"; toastSub="Continued from your last save - F5 save, F9 load"; toastTimer=4.0f; }
        else if(!loadError.empty()){ saveProtected=true; panel=P_SETTINGS; toastMsg="Save could not be loaded"; toastSub=loadError; toastTimer=5.0f; }
        if(e.preferences.overrideSaved){
            difficulty=e.preferences.difficulty;optHints=e.preferences.hints;optWeather=e.preferences.weather;optAutosave=e.preferences.autosave;
            e.preferences.overrideSaved=false;
        }
    }
}
inline void WorldState::update(Engine& e,float dt){
    ensureLoaded(e);
    observeCash();
    if (panel!=P_NONE) return;
    pulse+=dt; if(toastTimer>0)toastTimer-=dt; if(flashTimer>0)flashTimer-=dt;
    {
        const Uint8* ks=SDL_GetKeyboardState(nullptr); float vx=0,vy=0;
        if(ks[SDL_SCANCODE_W]||ks[SDL_SCANCODE_UP]){vy-=1;facing=tt::NORTH;}
        if(ks[SDL_SCANCODE_S]||ks[SDL_SCANCODE_DOWN]){vy+=1;facing=tt::SOUTH;}
        if(ks[SDL_SCANCODE_A]||ks[SDL_SCANCODE_LEFT]){vx-=1;facing=tt::WEST;}
        if(ks[SDL_SCANCODE_D]||ks[SDL_SCANCODE_RIGHT]){vx+=1;facing=tt::EAST;}
        float sp=speed*weatherSpeedMult();
        if(vx||vy){ float l=std::sqrt(vx*vx+vy*vy); vx/=l; vy/=l; float nx=truckX+vx*sp*dt,ny=truckY+vy*sp*dt,m=truckWorldSize*0.3f;
            if(tileAtWorld(nx,truckY)!=tt::WATER)truckX=nx;
            if(tileAtWorld(truckX,ny)!=tt::WATER)truckY=ny;
            truckX=std::min(std::max(truckX,m),mapW*(float)tile-m); truckY=std::min(std::max(truckY,m),mapH*(float)tile-m); }
    }
    e.camera.x=truckX; e.camera.y=truckY;
    {
        // weather cycles; storms spike accident calls, rain/storm slow trucks
        if(!optWeather){ weather=W_CLEAR; }
        else { weatherTimer-=dt;
            if(weatherTimer<=0){ int r=(int)rng.bounded(100); int nw=r<55?W_CLEAR:(r<85?W_RAIN:W_STORM);
                if(nw==W_STORM && weather!=W_STORM){ flashMsg="Storm rolling in - accident calls rising"; flashTimer=3.0f; }
                weather=nw; weatherTimer=kSecondsPerDay*(1.5f+(float)rng.bounded(150)/100.0f); } }
        // dynamic call volume: scales with fleet, drivers, lots, weather, and time of day
        float frac=dayAccum/kSecondsPerDay; int hour=(int)(frac*24);
        int mine,rivalLots,openLots; countLots(mine,rivalLots,openLots);
        int baseOrders = 8;
        int fleetOrders = (int)ownedTrucks.size() * 2 + drivers * 2;
        int lotOrders = mine * 2;
        int rushOrders = (hour>=7&&hour<9 || hour>=16&&hour<18) ? 4 : 0;
        int weatherOrders = (weather==W_STORM ? 5 : (weather==W_RAIN ? 2 : 0));
        int target = std::clamp(baseOrders + fleetOrders + lotOrders + rushOrders + weatherOrders, 10, 48);
        int activeCnt=0; for(auto&j:jobs) if(j.active) activeCnt++;
        spawnTimer-=dt; if(spawnTimer<=0){ spawnTimer=(weather==W_STORM?1.2f:1.8f); if(activeCnt<target) spawnJob(); }
    }
    updateAutoTruck(e,dt); updateRivals(dt);
    if(pendingDistrict && carried.empty() && std::all_of(autoTrucks.begin(),autoTrucks.end(),[](const auto& t){return t.phase==0;}))startDistrict(pendingDistrict);
    advanceClock(e,dt); advanceGoals(e);
    // majority announcement
    if(!announcedMajority){ int mine,rival,open; countLots(mine,rival,open); if((mine+rival+open)>0 && mine*2>(mine+rival+open)){ announcedMajority=true; toastMsg="You control the neighborhood!"; toastSub="Majority of lots are yours - keep pushing to take it all"; toastTimer=6.0f; } }
}
inline void WorldState::handleEvent(Engine& e, SDL_Event& ev){
    ensureLoaded(e);
    observeCash();
    if(ev.type==SDL_KEYDOWN && ev.key.repeat)return;
    if(saveProtected && ev.type==SDL_KEYDOWN && ev.key.keysym.sym==SDLK_F9){
        std::string error;if(!loadGame(e,&error)){flashMsg="Load failed: "+error;flashTimer=6;}return;
    }
    if (ev.type==SDL_MOUSEWHEEL && panel==P_NONE){ e.camera.zoom=simulation::adjustCameraZoom(e.camera.zoom,ev.wheel.y); return; }
    if (ev.type==SDL_MOUSEBUTTONDOWN){
        int mx=e.logicalMouseX(), my=e.logicalMouseY();
        if(panel==P_NONE){
            const int H=e.logicalH(), W=e.logicalW();
            if(my>=H-38 && my<=H-6){
                int dockW=9*108, dockX=(W-dockW)/2;
                if(mx>=dockX && mx<dockX+dockW){
                    int btn = (mx - dockX) / 108;
                    if(btn==0 && marketplaceUnlocked){ panel=P_MARKET; return; }
                    if(btn==1){ panel=P_FLEET; return; }
                    if(btn==2){ panel=P_BANK; return; }
                    if(btn==3){ panel=P_STOCK; return; }
                    if(btn==4){ panel=P_TOWNS; return; }
                    if(btn==5){ panel=P_PERF; return; }
                    if(btn==6){ int l=nearestOpenLot(); if(l>=0){ bidLot=l; bidSel=1; panel=P_BID; } return; }
                    if(btn==7){ panel=P_SETTINGS; return; }
                    if(btn==8){ devMode=!devMode; flashMsg=devMode?"Developer Mode ON: OpenTTD Landscape Tools [1-9]":"Developer Mode OFF"; flashTimer=2.5f; return; }
                }
            }
            if(devMode){
                if(my>=54 && my<=88){
                    int tbx = 16;
                    for(int i=0;i<10;++i){
                        int rx = tbx + 4 + i * 72;
                        if(mx>=rx && mx<=rx+68){
                            devTool = (i == 9 ? 0 : i + 1);
                            static const char* toolNames[] = {"Higgs Relax/Smooth", "Build Road/Bridge", "Bulldoze/Clear", "Place Building", "Place Park", "Plant Trees", "Place Water", "Place Sand", "Found City", "Spawn Call"};
                            flashMsg = std::string("Tool: ") + toolNames[devTool % 10];
                            flashTimer = 2.0f;
                            return;
                        }
                    }
                }
                float wx = (mx - e.logicalW() * 0.5f) / e.camera.zoom + e.camera.x;
                float wy = (my - e.logicalH() * 0.5f) / e.camera.zoom + e.camera.y;
                int tx = (int)std::floor(wx / tile);
                int ty = (int)std::floor(wy / tile);
                if(tx >= 0 && tx < mapW && ty >= 0 && ty < mapH){
                    if(ev.button.button == SDL_BUTTON_LEFT){
                        applyDevTool(devTool, tx, ty);
                    } else if(ev.button.button == SDL_BUTTON_RIGHT){
                        devBulldoze(tx, ty);
                    }
                    return;
                }
            }
        }
    }
    if (ev.type!=SDL_KEYDOWN) return;
    SDL_Keycode k=ev.key.keysym.sym;
    if (panel==P_TRUCKS){ const auto& cat=truckCatalog();
        if(k==SDLK_ESCAPE||k==SDLK_b){ panel=P_MARKET; return; }
        if(k==SDLK_UP||k==SDLK_w){ truckSel=(truckSel+(int)cat.size()-1)%(int)cat.size(); return; }
        if(k==SDLK_DOWN||k==SDLK_s){ truckSel=(truckSel+1)%(int)cat.size(); return; }
        if(k>=SDLK_1&&k<=SDLK_6){ truckSel=(int)(k-SDLK_1); return; }
        if(k==SDLK_RETURN||k==SDLK_KP_ENTER){ const auto& tk=cat[truckSel];
            if(ownedTrucks.size()>=simulation::maxTrucks){flashMsg="Fleet limit reached (64 trucks)";flashTimer=3;return;}
            if(cash>=tk.price){ cash-=tk.price; ownedTrucks.push_back(truckSel); if(carried.empty())drivenSlot=(int)ownedTrucks.size()-1; flashMsg=std::string("Bought ")+tk.name+" - now driving it.  -$"+std::to_string(tk.price); flashTimer=3.5f; advanceGoals(e); }
            else { flashMsg="Not enough cash for that truck"; flashTimer=2.5f; } return; }
        return; }
    if (panel==P_MARKET){ if(k==SDLK_ESCAPE||k==SDLK_m){panel=P_NONE;return;} if(k==SDLK_b){panel=P_TRUCKS;return;} return; }
    if (panel==P_FLEET){
        if(k==SDLK_ESCAPE||k==SDLK_o){ panel=P_NONE; return; }
        if(k==SDLK_LEFTBRACKET){ switchTruck(-1); return; }
        if(k==SDLK_RIGHTBRACKET){ switchTruck(1); return; }
        if(k==SDLK_PAGEUP){fleetPage=std::max(0,fleetPage-1);return;}
        if(k==SDLK_PAGEDOWN){fleetPage++;return;}
        if(k==SDLK_h){ if(drivers>=simulation::maxDrivers || drivers>=(int)ownedTrucks.size()-1){flashMsg="Buy a spare truck before hiring another driver";flashTimer=3;return;} const int HIRE=800; if(cash>=HIRE){cash-=HIRE;drivers++;flashMsg="Hired a driver (-$800). Idle trucks now earn.";flashTimer=3.0f;advanceGoals(e);} else {flashMsg="Not enough cash to hire ($800)";flashTimer=2.5f;} return; }
        return; }
    if (panel==P_BID){
        const auto& opt=bidOptions();
        if(k==SDLK_ESCAPE){ panel=P_NONE; bidLot=-1; return; }
        if(k==SDLK_UP||k==SDLK_LEFT){ bidSel=(bidSel+2)%3; return; }
        if(k==SDLK_DOWN||k==SDLK_RIGHT){ bidSel=(bidSel+1)%3; return; }
        if(k>=SDLK_1&&k<=SDLK_3){ bidSel=(int)(k-SDLK_1); return; }
        if(k==SDLK_RETURN||k==SDLK_KP_ENTER){
            if(bidLot>=0 && bidLot<(int)lots.size() && lots[bidLot].owner==-1){
                int mine,rival,open; countLots(mine,rival,open); float rivalShare=(mine+rival+open)?(float)rival/(mine+rival+open):0;
                float repBonus=(reputation-50.0f)/200.0f; float rivalPenalty=(float)((rivalRep-50.0)/300.0);
                float chance=opt[bidSel].winBase + repBonus - rivalPenalty - 0.2f*rivalShare + diffBidBonus() - rivalBidPressure(); chance=std::min(0.95f,std::max(0.08f,chance));
                float roll=(float)rng.bounded(1000)/1000.0f;
                if(roll<chance){ lots[bidLot].owner=0; lots[bidLot].incomeMult=1.0f; reputation=std::min(100.0f,reputation+2.0f); flashMsg="Contract WON! Your company now controls this lot."; flashTimer=3.5f; }
                else { lots[bidLot].owner=1; lots[bidLot].incomeMult=1.0f; flashMsg="Rival outbid you for this lot."; flashTimer=3.0f; }
                advanceGoals(e);
            }
            panel=P_NONE; bidLot=-1; return;
        }
        return;
    }
    if (panel==P_BANK){ const auto& lc=loanCatalog();
        if(k==SDLK_ESCAPE||k==SDLK_f){ panel=P_NONE; return; }
        if(k==SDLK_UP){ if(!loans.empty()) selectedLoan=(selectedLoan+loans.size()-1)%loans.size(); return; }
        if(k==SDLK_DOWN){ if(!loans.empty()) selectedLoan=(selectedLoan+1)%loans.size(); return; }
        if(k==SDLK_r){
            if(selectedLoan>=loans.size()){ flashMsg="No active loan selected"; flashTimer=2.2f; return; }
            simulation::WeeklyFinanceState finance{cash,weeklyRevenueAccum,lastWeekRevenue,lastWeekExpense,reputation,{}};
            for(const auto& loan:loans) finance.loans.push_back({loan.balance,loan.weeklyPayment});
            const int requested=(ev.key.keysym.mod & KMOD_SHIFT)?cash:500;
            const int paid=simulation::repayLoan(finance,selectedLoan,requested);
            cash=finance.cash;
            loans.clear(); for(const auto& loan:finance.loans) loans.push_back({loan.balance,loan.weeklyPayment});
            if(selectedLoan>=loans.size() && !loans.empty()) selectedLoan=loans.size()-1;
            flashMsg=paid>0?"Paid $"+std::to_string(paid)+" toward loan principal":"No cash available to repay loan";
            flashTimer=2.8f; return;
        }
        if(k>=SDLK_1&&k<=SDLK_3){ const auto& lp=lc[(int)(k-SDLK_1)];
            int total=(int)std::round(lp.principal*(1.0+lp.apr*lp.weeks/52.0)); int wk=(total+lp.weeks-1)/lp.weeks;
            if(const char* reason=simulation::loanRejection(cash,tows,positiveMonths,(int)loans.size(),lastWeekRevenue-lastWeekExpense,wk)){flashMsg=reason;flashTimer=3.5f;return;}
            loans.push_back({total,wk}); selectedLoan=loans.size()-1; cash+=lp.principal;
            flashMsg=std::string("Took ")+lp.name+": +$"+std::to_string(lp.principal)+" (pay $"+std::to_string(wk)+"/wk)"; flashTimer=3.2f; return; }
        return;
    }
    if (panel==P_STOCK){
        long val=computeValuation();
        if(k==SDLK_ESCAPE||k==SDLK_t){ panel=P_NONE; return; }
        if(k==SDLK_i && !isPublic && val>=kIpoThreshold){ sharesOut=1000000.0; long raise=(long)(val*0.25); cash+=raise; youOwnFrac=0.75; isPublic=true; sharePrice=(double)val/sharesOut;
            flashMsg=std::string("IPO! Raised $")+std::to_string(raise)+" - you keep 75%"; flashTimer=3.6f; return; }
        if(k==SDLK_s && isPublic){
            if(youOwnFrac>=0.05){
                long proceeds=std::max(100L, (long)std::round(sharesOut*0.05*sharePrice));
                cash+=proceeds; youOwnFrac=std::max(0.0, youOwnFrac-0.05);
                flashMsg=std::string("Sold 5% of company shares for $")+std::to_string(proceeds); flashTimer=3.0f;
            } else { flashMsg="You don't own enough shares to sell"; flashTimer=2.5f; }
            return;
        }
        if(k==SDLK_b && isPublic){
            long cost=std::max(100L, (long)std::round(sharesOut*0.05*sharePrice));
            if(cash>=cost && youOwnFrac<=0.95){
                cash-=cost; youOwnFrac=std::min(1.0, youOwnFrac+0.05);
                flashMsg=std::string("Bought back 5% of company shares for $")+std::to_string(cost); flashTimer=3.0f;
            } else if(cash<cost){ flashMsg="Not enough cash to buy back shares"; flashTimer=2.5f; }
            else { flashMsg="You already own 100% of the company"; flashTimer=2.5f; }
            return;
        }
        if(k==SDLK_r){ long rv=rivalValuation(); long cost=(long)(rv*0.10);
            if(rivalStake<0.999 && cash>=cost && tows>=6){ cash-=cost; rivalStake=std::min(1.0,rivalStake+0.10); flashMsg=std::string("Bought 10% of the rival for $")+std::to_string(cost); flashTimer=3.0f; }
            else if(tows<6){flashMsg="Complete 6 tows before buying rival shares";flashTimer=3;}
            else if(cash<cost){flashMsg="Not enough cash to buy rival shares";flashTimer=2.5f;}
            return;
        }
        if(k==SDLK_u && rivalStake>=0.5 && !wonDistrict){
            for(auto&l:lots) if(l.owner==1){ l.owner=0; if(l.incomeMult<1.0f)l.incomeMult=1.0f; }
            rivalCash=0; rivalStake=1.0; wonDistrict=true;
            toastMsg="DISTRICT WON - you acquired your rival!"; toastSub="Their lots are now yours. Press N to expand to the next district."; toastTimer=8.0f; advanceGoals(e); return; }
        if(k==SDLK_n && wonDistrict){ panel=P_NONE; startDistrict(district+1); return; }
        return;
    }
    if (panel==P_SETTINGS){
        if(k==SDLK_ESCAPE||k==SDLK_g){ panel=P_NONE; return; }
        if(k==SDLK_d){ difficulty=(difficulty+1)%3; flashMsg=std::string("Difficulty: ")+difficultyName(); flashTimer=2.0f; return; }
        if(k==SDLK_h){ optHints=!optHints; return; }
        if(k==SDLK_w){ optWeather=!optWeather; if(!optWeather){weather=W_CLEAR;} return; }
        if(k==SDLK_a){ optAutosave=!optAutosave; return; }
        if(k==SDLK_s){ std::string error; flashMsg=saveGame(e,&error)?"Game saved":"Save failed: "+error; flashTimer=3.0f; return; }
        if(k==SDLK_z){ e.camera.zoom=2.0f; return; }
        if(k==SDLK_q){
            std::string error;
            if(!saveGame(e,&error)){ flashMsg="Save failed: "+error; flashTimer=3.0f; return; }
            e.preferences={difficulty,optHints,optWeather,optAutosave,false};
            e.changeState(std::make_unique<MenuState>());
            return;
        }
        if(k==SDLK_n && wonDistrict){ panel=P_NONE; startDistrict(district+1); return; }
        return;
    }
    if (panel==P_TOWNS){
        if(k==SDLK_ESCAPE||k==SDLK_h||k==SDLK_y){ panel=P_NONE; return; }
        if(k==SDLK_UP){ townSel=(townSel+(int)cities.size()-1)%(int)cities.size(); return; }
        if(k==SDLK_DOWN){ townSel=(townSel+1)%(int)cities.size(); return; }
        if(k>=SDLK_1&&k<=SDLK_6){ int idx=(int)(k-SDLK_1); if(idx<(int)cities.size()) townSel=idx; return; }
        if(townSel>=0&&townSel<(int)cities.size()){
            auto& c=cities[townSel];
            if(k==SDLK_b){
                if(cash>=2500){
                    cash-=2500; c.population+=150; c.councilRating=std::min(1000,c.councilRating+80); c.prosperity=std::min(100,c.prosperity+3);
                    devPlaceBuilding(c.cx+2, c.cy+2, (int)rng.bounded(6));
                    flashMsg="Funded new construction in "+c.name+"! (+80 Council Rating)"; flashTimer=3.0f;
                    postNews("NEW CONSTRUCTION: "+c.name+" expands with new commercial lots funded by TowTycoon!", SDL_Color{80,240,120,255});
                } else { flashMsg="Need $2,500 to fund buildings"; flashTimer=2.5f; }
                return;
            }
            if(k==SDLK_r){
                if(cash>=1200){
                    cash-=1200; c.councilRating=std::min(1000,c.councilRating+100); c.prosperity=std::min(100,c.prosperity+5);
                    flashMsg="Sponsored road resurfacing in "+c.name+"! (+100 Council Rating)"; flashTimer=3.0f;
                    postNews("INFRASTRUCTURE: TowTycoon sponsors municipal road repairs in "+c.name, SDL_Color{100,220,240,255});
                } else { flashMsg="Need $1,200 to sponsor road works"; flashTimer=2.5f; }
                return;
            }
            if(k==SDLK_x){
                if(c.exclusiveContract){ flashMsg="Already hold exclusive contract with "+c.name; flashTimer=2.5f; return; }
                if(c.councilRating<600){ flashMsg="Council requires 'Very Good' rating (600+) for exclusive rights"; flashTimer=3.0f; return; }
                if(cash>=3000){
                    cash-=3000; c.exclusiveContract=true;
                    flashMsg="Awarded Exclusive Towing Contract with "+c.name+"! (+25% Payouts)"; flashTimer=3.5f;
                    postNews("EXCLUSIVE CONTRACT: "+c.name+" awards exclusive municipal rights to TowTycoon!", SDL_Color{255,220,80,255});
                } else { flashMsg="Need $3,000 for exclusive contract"; flashTimer=2.5f; }
                return;
            }
            if(k==SDLK_c){
                if(cash>=5000){
                    cash-=5000;
                    if(rng.bounded(100)<75){
                        c.councilRating=950; flashMsg="Council accepted civic contribution! Standing is now Outstanding (+950)"; flashTimer=3.5f;
                    } else {
                        c.councilRating=-500; int fine=std::min(cash,2000); cash-=fine;
                        flashMsg="CAUGHT! Council whistleblower leaked the bribe! Standing plunged to Appalling (-500) & fined $"+std::to_string(fine);
                        flashTimer=4.0f;
                        postNews("SCANDAL: Municipal ethics probe launched against TowTycoon in "+c.name+"!", SDL_Color{240,70,70,255});
                    }
                } else { flashMsg="Need $5,000 to bribe the council"; flashTimer=2.5f; }
                return;
            }
        }
        return;
    }
    if (panel==P_PERF){
        if(k==SDLK_ESCAPE||k==SDLK_p){ panel=P_NONE; return; }
        return;
    }
    // P_NONE
    if(k==SDLK_F1||k==SDLK_BACKQUOTE){ devMode=!devMode; flashMsg=devMode?"Developer Mode ON: OpenTTD Landscape Tools [1-9] active":"Developer Mode OFF"; flashTimer=2.5f; return; }
    if(devMode && k>=SDLK_0 && k<=SDLK_9){
        devTool = (k == SDLK_0) ? 0 : (int)(k - SDLK_0);
        static const char* toolNames[] = {"Higgs Relax/Smooth", "Build Road/Bridge", "Bulldoze/Clear", "Place Building", "Place Park", "Plant Trees", "Place Water", "Place Sand", "Found City", "Spawn Call"};
        flashMsg = std::string("Tool: ") + toolNames[devTool % 10];
        flashTimer = 2.0f;
        return;
    }
    if(k==SDLK_ESCAPE||k==SDLK_g){ panel=P_SETTINGS; return; }
    if(k==SDLK_f){ panel=P_BANK; return; }
    if(k==SDLK_t){ panel=P_STOCK; return; }
    if(k==SDLK_h||k==SDLK_y){ panel=P_TOWNS; return; }
    if(k==SDLK_p){ panel=P_PERF; return; }
    if(k==SDLK_F5){ std::string error; flashMsg=saveGame(e,&error)?"Game saved":"Save failed: "+error; flashTimer=3.0f; return; }
    if(k==SDLK_F9){
        std::string error;
        flashMsg=loadGame(e,&error)?"Game loaded":error.empty()?"No save file found":"Load failed: "+error;
        flashTimer=3.0f;
        return;
    }
    if(k==SDLK_m && marketplaceUnlocked){ panel=P_MARKET; return; }
    if(k==SDLK_o){ panel=P_FLEET; return; }
    if(k==SDLK_c){ int l=nearestOpenLot(); if(l>=0){ bidLot=l; bidSel=1; panel=P_BID; } else { flashMsg="Drive next to an open lot (cordoned pad) to bid"; flashTimer=2.5f; } return; }
    if(k==SDLK_LEFTBRACKET){ switchTruck(-1); return; }
    if(k==SDLK_RIGHTBRACKET){ switchTruck(1); return; }
    if(k==SDLK_EQUALS||k==SDLK_PLUS||k==SDLK_KP_PLUS){ e.camera.zoom=simulation::adjustCameraZoom(e.camera.zoom,1); return; }
    if(k==SDLK_MINUS||k==SDLK_KP_MINUS){ e.camera.zoom=simulation::adjustCameraZoom(e.camera.zoom,-1); return; }
    if(k==SDLK_e){
        const float R=48.0f; int b=nearestActiveJob(); float dYard; { float dx=yardX-truckX,dy=yardY-truckY; dYard=dx*dx+dy*dy; }
        if(!carried.empty() && dYard<R*R){
            int sum=0; for(auto&c:carried)sum+=c.payout; int n=(int)carried.size();
            int cIdx=cityAtWorld(truckX,truckY);
            if(cIdx>=0&&cIdx<(int)cities.size()) {
                cities[cIdx].towsCompleted+=n;
                cities[cIdx].councilRating=std::min(1000, cities[cIdx].councilRating + 20 * n);
                if(cities[cIdx].exclusiveContract) sum=(int)(sum * 1.25f);
            }
            cash+=sum; tows+=n; weeklyRevenueAccum+=sum;
            reputation=std::min(100.0f,reputation+1.0f*n); yardImpoundCount+=n;
            carried.clear(); for(int i=0;i<n;++i)spawnJob();
            flashMsg=std::string("Delivered ")+std::to_string(n)+" vehicle(s)!  +$"+std::to_string(sum);
            if(cIdx>=0&&cIdx<(int)cities.size()&&cities[cIdx].exclusiveContract) flashMsg += " (+25% Municipal Bonus)";
            flashTimer=3.0f;
            if(tows>=6&&!marketplaceUnlocked){ marketplaceUnlocked=true; toastTimer=7.0f; toastMsg="Business Milestone: 6 Successful Tows!"; toastSub="Vehicle marketplace unlocked - press M"; } advanceGoals(e); return; }
        if(b>=0){ float dx=jobs[b].x-truckX,dy=jobs[b].y-truckY; if(dx*dx+dy*dy<R*R){
            if(jobs[b].jtype==JT_ROADSIDE){
                if(model()!=4){ flashMsg="Roadside service calls require the Roadside Van"; flashTimer=2.8f; return; }
                const int payout=jobs[b].payout; const std::string service=jobs[b].desc;
                jobs.erase(jobs.begin()+b); cash+=payout; weeklyRevenueAccum+=payout;
                reputation=std::min(100.0f,reputation+0.25f);
                flashMsg="Completed "+service+"  +$"+std::to_string(payout); flashTimer=3.0f;
                spawnJob(); return;
            }
            if(jobs[b].jclass>maxClass()){ flashMsg=std::string("That ")+classWord(jobs[b].jclass)+" job needs a bigger truck"; flashTimer=3.0f; return; }
            if((int)carried.size()>=carryCap()){ flashMsg=std::string("Truck full (capacity ")+std::to_string(carryCap())+") - deliver first"; flashTimer=3.0f; return; }
            carried.push_back({jobs[b].payout,jobs[b].carType,jobs[b].jclass}); jobs.erase(jobs.begin()+b);
            flashMsg=(carryCap()>1)?("Loaded ("+std::to_string((int)carried.size())+"/"+std::to_string(carryCap())+") - to YARD when full"):"Hooked up - haul it to the YARD"; flashTimer=2.5f; return; } }
    }
}
inline void WorldState::drawCar(Engine& e,float wx,float wy,int type,int angleDeg,float worldSize){
    const float z=e.camera.zoom; int sx,sy; worldToScreen(e,wx,wy,sx,sy);
    if(e.carsTex){ float ch=worldSize; float cw=worldSize*(float)e.carCellW/(float)e.carCellH; SDL_Rect src{(type%e.carCount)*e.carCellW,0,e.carCellW,e.carCellH};
        SDL_Rect dst{sx-(int)std::round(cw*z/2),sy-(int)std::round(ch*z/2),(int)std::round(cw*z),(int)std::round(ch*z)}; SDL_RenderCopyEx(e.renderer(),e.carsTex,&src,&dst,(double)angleDeg,nullptr,SDL_FLIP_NONE); }
    else { int w=(int)std::round(worldSize*z); fillRect(e,sx-w/2,sy-w/2,w,w,70,120,200,255); }
}
inline void WorldState::drawLots(Engine& e){
    const float z=e.camera.zoom; int nearOpen=nearestOpenLot();
    for(int i=0;i<(int)lots.size();++i){ const Lot& l=lots[i]; int sx,sy; worldToScreen(e,l.cx,l.cy,sx,sy);
        int w=(int)std::round(2.4f*tile*z), h=(int)std::round(2.4f*tile*z);
        if(l.owner==-1){ // open: cordoned pad
            if(e.lotTex){ float aspect=e.lotH?(float)e.lotW/e.lotH:1.0f; int dw=w, dh=(int)std::round(w/aspect); SDL_Rect dst{sx-dw/2,sy-dh/2,dw,dh}; SDL_SetTextureAlphaMod(e.lotTex, i==nearOpen?255:200); SDL_RenderCopy(e.renderer(),e.lotTex,nullptr,&dst); SDL_SetTextureAlphaMod(e.lotTex,255); }
            else { fillRect(e,sx-w/2,sy-h/2,w,h,120,110,70,150); }
            if(i==nearOpen){ drawIcon(e,tt::IC_GAVEL,sx,sy-h/2-(int)(14*z),(int)std::round(28*z)); }
        } else if(l.owner==0){ // yours
            fillRect(e,sx-w/2,sy-h/2,w,h,60,160,90,70); strokeRect(e,sx-w/2,sy-h/2,w,h,120,230,150,220);
            drawIcon(e,tt::IC_MONEY,sx,sy,(int)std::round(22*z));
        } else { // rival
            fillRect(e,sx-w/2,sy-h/2,w,h,170,70,70,70); strokeRect(e,sx-w/2,sy-h/2,w,h,220,120,120,200);
            if(e.rivalTrucksTex){ int ts=(int)std::round(28*z); SDL_Rect src{(l.bType%e.truckCount)*e.truckCellW,0,e.truckCellW,e.truckCellH}; SDL_Rect dst{sx-ts/2,sy-ts/2,ts,ts}; SDL_RenderCopy(e.renderer(),e.rivalTrucksTex,&src,&dst); }
        }
    }
}
inline void WorldState::drawWorld(Engine& e){
    const float zoom=e.camera.zoom; const float hw=e.logicalW()/(2.0f*zoom),hh=e.logicalH()/(2.0f*zoom);
    int sX=std::max(0,(int)std::floor((e.camera.x-hw)/tile)),sY=std::max(0,(int)std::floor((e.camera.y-hh)/tile));
    int eX=std::min(mapW-1,(int)std::floor((e.camera.x+hw)/tile)),eY=std::min(mapH-1,(int)std::floor((e.camera.y+hh)/tile));
    int ds=std::max(1,(int)std::round(tile*zoom));
    for(int ty=sY;ty<=eY;++ty)for(int tx=sX;tx<=eX;++tx){
        int id=tiles[simulation::tileIndex(tx, ty, mapW)];
        if(id<0)continue;
        SDL_Rect src{(id%e.tilesetCols)*tile,(id/e.tilesetCols)*tile,tile,tile};
        int xx,yy; worldToScreen(e,(float)tx*tile,(float)ty*tile,xx,yy);
        SDL_Rect dst{xx,yy,ds,ds};
        SDL_RenderCopy(e.renderer(),e.tileset,&src,&dst);

        // Higgsfield Landscape Shading & Architectural Details
        if(id == tt::WATER){
            float wave = std::sin(pulse * 3.2f + tx * 0.85f + ty * 0.65f);
            if(wave > 0.45f){
                int rw = std::max(2, ds / 3);
                int rh = std::max(1, ds / 6);
                int ox = (int)((wave - 0.45f) * 1.5f * (ds - rw));
                fillRect(e, xx + ox, yy + ds / 3, rw, rh, 180, 230, 255, 75);
            }
            bool isFountain = (tx > 0 && tiles[simulation::tileIndex(tx - 1, ty, mapW)] != tt::WATER) &&
                              (tx + 1 < mapW && tiles[simulation::tileIndex(tx + 1, ty, mapW)] != tt::WATER);
            if(isFountain){
                float sp = std::sin(pulse * 5.5f);
                int cr = std::max(2, (int)std::round((3.0f + sp) * zoom));
                fillRect(e, xx + ds / 2 - cr / 2, yy + ds / 2 - cr / 2, cr, cr, 230, 245, 255, 190);
            }
        } else if(id == tt::SAND){
            bool nearWater = (tx > 0 && tiles[simulation::tileIndex(tx - 1, ty, mapW)] == tt::WATER) ||
                             (tx + 1 < mapW && tiles[simulation::tileIndex(tx + 1, ty, mapW)] == tt::WATER) ||
                             (ty > 0 && tiles[simulation::tileIndex(tx, ty - 1, mapW)] == tt::WATER) ||
                             (ty + 1 < mapH && tiles[simulation::tileIndex(tx, ty + 1, mapW)] == tt::WATER);
            if(nearWater){
                float foam = std::sin(pulse * 2.8f + (tx + ty) * 0.5f);
                if(foam > 0.2f){
                    strokeRect(e, xx + 1, yy + 1, ds - 2, ds - 2, 245, 250, 255, (Uint8)(70 + 60 * foam));
                }
            }
        } else if(id == tt::ROAD){
            bool waterH = (tx > 0 && tiles[simulation::tileIndex(tx - 1, ty, mapW)] == tt::WATER) || (tx + 1 < mapW && tiles[simulation::tileIndex(tx + 1, ty, mapW)] == tt::WATER);
            bool waterV = (ty > 0 && tiles[simulation::tileIndex(tx, ty - 1, mapW)] == tt::WATER) || (ty + 1 < mapH && tiles[simulation::tileIndex(tx, ty + 1, mapW)] == tt::WATER);
            if(waterH && !waterV){
                int rw = std::max(1, (int)std::round(2.0f * zoom));
                fillRect(e, xx, yy, rw, ds, 175, 165, 155, 255);
                fillRect(e, xx + ds - rw, yy, rw, ds, 175, 165, 155, 255);
            } else if(waterV && !waterH){
                int rh = std::max(1, (int)std::round(2.0f * zoom));
                fillRect(e, xx, yy, ds, rh, 175, 165, 155, 255);
                fillRect(e, xx, yy + ds - rh, ds, rh, 175, 165, 155, 255);
            } else if(waterH && waterV){
                strokeRect(e, xx, yy, ds, ds, 175, 165, 155, 255);
            } else {
                if(tx % 8 == 0 && ty % 2 == 0){
                    int lw = std::max(1, (int)std::round(1.0f * zoom));
                    int lh = std::max(2, ds / 2);
                    fillRect(e, xx + ds / 2 - lw / 2, yy + ds / 4, lw, lh, 240, 235, 210, 140);
                } else if(ty % 8 == 0 && tx % 2 == 0){
                    int lh = std::max(1, (int)std::round(1.0f * zoom));
                    int lw = std::max(2, ds / 2);
                    fillRect(e, xx + ds / 4, yy + ds / 2 - lh / 2, lw, lh, 240, 235, 210, 140);
                }
            }
        }
    }
    if(e.buildingsTex){ const float fw=3.0f*tile,fh=fw*(float)e.buildingCellH/(float)e.buildingCellW;
        for(auto&b:buildings){ float wx=b.tx*(float)tile,wy=(b.ty+3)*(float)tile-fh; int xx,yy; worldToScreen(e,wx,wy,xx,yy); SDL_Rect src{b.type*e.buildingCellW,0,e.buildingCellW,e.buildingCellH}; SDL_Rect dst{xx,yy,(int)std::round(fw*zoom),(int)std::round(fh*zoom)}; SDL_RenderCopy(e.renderer(),e.buildingsTex,&src,&dst); } }
    if(devMode){
        int mx = e.logicalMouseX(), my = e.logicalMouseY();
        float wx = (mx - e.logicalW() * 0.5f) / e.camera.zoom + e.camera.x;
        float wy = (my - e.logicalH() * 0.5f) / e.camera.zoom + e.camera.y;
        int tx = (int)std::floor(wx / tile);
        int ty = (int)std::floor(wy / tile);
        if(tx >= 0 && tx < mapW && ty >= 0 && ty < mapH){
            int sx, sy; worldToScreen(e, (float)tx * tile, (float)ty * tile, sx, sy);
            strokeRect(e, sx, sy, ds, ds, 255, 235, 60, 255);
        }
    }
}
inline void WorldState::drawEntities(Engine& e){
    const float zoom=e.camera.zoom;
    { int sx,sy; worldToScreen(e,yardX,yardY,sx,sy); int r=(int)std::round(26*zoom); fillRect(e,sx-r,sy-r,r*2,r*2,40,44,56,180); strokeRect(e,sx-r,sy-r,r*2,r*2,235,200,90,255); drawText(e,"YARD",sx,sy-10,SDL_Color{235,200,90,255},18,true); }
    float blink=std::sin(pulse*6.0f)*0.5f+0.5f;
    for(auto&j:jobs){ if(!j.active)continue; float sizeW=j.jclass==tt::HEAVY?30.0f:j.jclass==tt::MEDIUM?24.0f:18.0f; drawCar(e,j.x,j.y,j.carType,j.angle,sizeW);
        int sx,sy; worldToScreen(e,j.x,j.y,sx,sy); int rr=(int)std::round((sizeW*0.9f)*zoom);
        SDL_Color ring=j.jclass==tt::HEAVY?SDL_Color{190,90,235,255}:j.jclass==tt::MEDIUM?SDL_Color{240,150,60,255}:SDL_Color{240,90,60,255};
        if(j.jtype==JT_IMPOUND) ring=SDL_Color{80,150,255,255}; else if(j.jtype==JT_REPO) ring=SDL_Color{255,140,40,255}; else if(j.jtype==JT_ROADSIDE) ring=SDL_Color{70,220,210,255};
        strokeRect(e,sx-rr,sy-rr,rr*2,rr*2,ring.r,ring.g,ring.b,(Uint8)(120+135*blink));
        std::ostringstream o; o<<"$"<<j.payout; const char* jtag=jobTypeTag(j.jtype);
        if(j.jtype==JT_ROADSIDE) o<<"  SERVICE";
        else if(jtag[0])o<<"  "<<jtag; else if(j.jclass!=tt::REGULAR)o<<"  "<<(j.jclass==tt::HEAVY?"HEAVY":"MED");
        SDL_Color lblC=j.jtype==JT_IMPOUND?SDL_Color{150,200,255,255}:j.jtype==JT_REPO?SDL_Color{255,190,120,255}:j.jtype==JT_ROADSIDE?SDL_Color{100,235,225,255}:SDL_Color{255,230,120,255};
        drawText(e,o.str().c_str(),sx,sy-rr-(int)(20*zoom),lblC,16,true); }
    for(size_t i=0;i<carried.size();++i){ float back=20.0f+i*16.0f; float ox=0,oy=0; if(facing==tt::NORTH)oy=back; else if(facing==tt::SOUTH)oy=-back; else if(facing==tt::EAST)ox=-back; else ox=back;
        float sw=carried[i].jclass==tt::HEAVY?26.0f:carried[i].jclass==tt::MEDIUM?22.0f:16.0f; drawCar(e,truckX+ox,truckY+oy,carried[i].carType,facing*90,sw); }
    // self-driving company truck + its A* route
    for(const auto& autoT:autoTrucks){
        for(int i=autoT.ri;i<(int)autoT.route.size();++i){ int sx,sy; worldToScreen(e,(autoT.route[i].first+0.5f)*tile,(autoT.route[i].second+0.5f)*tile,sx,sy); int r=std::max(1,(int)std::round(2*zoom)); fillRect(e,sx-r,sy-r,r*2,r*2,235,210,90,150); }
        if(autoT.phase==2){ float back=20.0f,ox=0,oy=0; if(autoT.facing==tt::NORTH)oy=back; else if(autoT.facing==tt::SOUTH)oy=-back; else if(autoT.facing==tt::EAST)ox=-back; else ox=back; drawCar(e,autoT.x+ox,autoT.y+oy,autoT.carType,autoT.facing*90,16.0f); }
        int sx,sy; worldToScreen(e,autoT.x,autoT.y,sx,sy); int size=(int)std::round(30*zoom); SDL_Rect dst{sx-size/2,sy-size/2,size,size};
        if(e.trucksTex){ SDL_Rect src{(autoT.model%e.truckCount)*e.truckCellW,0,e.truckCellW,e.truckCellH}; SDL_RenderCopyEx(e.renderer(),e.trucksTex,&src,&dst,(double)(autoT.facing*90),nullptr,SDL_FLIP_NONE); }
        drawText(e,"AUTO",sx,sy-(int)std::round(22*zoom),SDL_Color{235,210,120,255},14,true);
    }
    // rival company trucks (dark livery) physically driving their A* routes
    for(auto& r:rivalTrucks){ if(!r.active)continue;
        for(int i=r.ri;i<(int)r.route.size();++i){ int sx,sy; worldToScreen(e,(r.route[i].first+0.5f)*tile,(r.route[i].second+0.5f)*tile,sx,sy); int rp=std::max(1,(int)std::round(2*zoom)); fillRect(e,sx-rp,sy-rp,rp*2,rp*2,235,90,90,120); }
        if(r.phase==2){ float back=20.0f,ox=0,oy=0; if(r.facing==tt::NORTH)oy=back; else if(r.facing==tt::SOUTH)oy=-back; else if(r.facing==tt::EAST)ox=-back; else ox=back; drawCar(e,r.x+ox,r.y+oy,r.carType,r.facing*90,16.0f); }
        int sx,sy; worldToScreen(e,r.x,r.y,sx,sy); int size=(int)std::round(30*zoom); SDL_Rect dst{sx-size/2,sy-size/2,size,size};
        SDL_Texture* rt=e.rivalTrucksTex?e.rivalTrucksTex:e.trucksTex;
        if(rt){ SDL_Rect src{(r.model%e.truckCount)*e.truckCellW,0,e.truckCellW,e.truckCellH}; SDL_RenderCopyEx(e.renderer(),rt,&src,&dst,(double)(r.facing*90),nullptr,SDL_FLIP_NONE); } else fillRect(e,dst.x,dst.y,dst.w,dst.h,60,60,70,255);
        drawText(e,"RIVAL",sx,sy-(int)std::round(22*zoom),SDL_Color{240,110,110,255},14,true);
    }
    { int sx,sy; worldToScreen(e,truckX,truckY,sx,sy); int size=(int)std::round(truckWorldSize*zoom); SDL_Rect dst{sx-size/2,sy-size/2,size,size};
      if(e.trucksTex){ SDL_Rect src{(model()%e.truckCount)*e.truckCellW,0,e.truckCellW,e.truckCellH}; SDL_RenderCopyEx(e.renderer(),e.trucksTex,&src,&dst,(double)(facing*90),nullptr,SDL_FLIP_NONE); } else fillRect(e,dst.x,dst.y,dst.w,dst.h,235,140,40,255); }
}
inline void WorldState::drawHud(Engine& e){
    const int W=e.logicalW(), H=e.logicalH();
    fillRect(e,0,0,W,46,14,18,26,235); fillRect(e,0,46,W,2,45,120,180,200);

    drawText(e,"TowTycoon v" TOWTYCOON_VERSION,16,10,SDL_Color{245,215,95,255},20);
    if(isPublic){
        std::ostringstream sp; sp<<"PUBLIC $"<<std::fixed<<std::setprecision(2)<<sharePrice;
        fillRect(e,168,11,118,24,25,65,40,240); strokeRect(e,168,11,118,24,75,210,120,255);
        drawText(e,sp.str().c_str(),174,13,SDL_Color{120,240,150,255},13);
    } else {
        fillRect(e,168,11,78,24,55,45,25,240); strokeRect(e,168,11,78,24,210,160,60,255);
        drawText(e,"PRIVATE",174,13,SDL_Color{240,180,80,255},13);
    }

    std::ostringstream c; c<<"$"<<cash; drawText(e,c.str().c_str(),298,9,SDL_Color{120,240,140,255},23);
    std::ostringstream fl; fl<<"Fleet: "<<ownedTrucks.size(); drawText(e,fl.str().c_str(),430,11,SDL_Color{195,215,240,255},18);
    std::ostringstream dr; dr<<"Drivers: "<<drivers; drawText(e,dr.str().c_str(),530,11,SDL_Color{195,215,240,255},18);
    std::ostringstream rp; rp<<"★ "<<(int)reputation<<"%"; drawText(e,rp.str().c_str(),645,11,SDL_Color{255,215,75,255},18);

    int curCity = cityAtWorld(truckX, truckY);
    std::string curCityName = (curCity >= 0 && curCity < (int)cities.size()) ? cities[curCity].name : "Regional District";
    int pop = (curCity >= 0 && curCity < (int)cities.size()) ? cities[curCity].population : 12000;
    std::ostringstream cb; cb<<"📍 "<<curCityName<<" (Pop "<<pop<<")";
    drawText(e,cb.str().c_str(),730,11,SDL_Color{90,225,245,255},17);

    float frac=dayAccum/kSecondsPerDay; int hh=(int)(frac*24); int mm=(int)((frac*24-hh)*60);
    std::ostringstream dt; dt<<weatherName()<<" "<<std::setw(2)<<std::setfill('0')<<hh<<":"<<std::setw(2)<<std::setfill('0')<<mm<<" Yr "<<year<<" M"<<month<<" W"<<week;
    SDL_Color wc = weather==W_STORM?SDL_Color{160,180,245,255}:weather==W_RAIN?SDL_Color{150,220,245,255}:SDL_Color{255,220,100,255};
    drawText(e,dt.str().c_str(),W-260,12,wc,16);

    if(devMode){
        int tbx=16, tby=52, tbh=32;
        fillRect(e,tbx,tby,10*70+4,tbh,18,22,32,245);
        strokeRect(e,tbx,tby,10*70+4,tbh,240,190,60,255);
        static const char* toolLabels[]={"1:Bridge","2:Clear","3:Build","4:Park","5:Tree","6:Water","7:Sand","8:Town","9:Call","0:Higgs"};
        for(int i=0;i<10;++i){
            int btnTool=(i==9?0:i+1); bool active=(devTool==btnTool); int rx=tbx+2+i*70;
            fillRect(e,rx,tby+2,66,tbh-4,active?55:28,active?95:34,active?135:44,255);
            if(active) strokeRect(e,rx,tby+2,66,tbh-4,245,210,80,255);
            drawText(e,toolLabels[i],rx+6,tby+6,active?SDL_Color{255,235,120,255}:SDL_Color{190,205,225,255},13);
        }
    }

    int mine,rival,open; countLots(mine,rival,open); int total=mine+rival+open;
    {
        int bx=16, by=devMode?88:52, bw=W-240, bh=13;
        if(bw>80){
            fillRect(e,bx,by,bw,bh,24,28,38,220);
            if(total>0){
                int wy=(int)std::round(bw*(float)mine/total); int wr=(int)std::round(bw*(float)rival/total);
                fillRect(e,bx,by,wy,bh,60,180,95,255); fillRect(e,bx+wy,by,wr,bh,210,75,75,255);
            }
            strokeRect(e,bx,by,bw,bh,80,95,115,255);
            std::ostringstream m; m<<"Market Control  You "<<(total?mine*100/total:0)<<"%   Rival "<<(total?rival*100/total:0)<<"%   Open Lots "<<open;
            drawText(e,m.str().c_str(),bx+bw/2,by-1,SDL_Color{220,230,245,255},12,true);
        }
    }

    std::string obj; int nol=nearestOpenLot(), activeJob=nearestActiveJob();
    float targetX=truckX,targetY=truckY; bool hasTarget=false;
    if(!carried.empty()){
        targetX=yardX; targetY=yardY; hasTarget=true;
        obj="Deliver "+std::to_string((int)carried.size())+"/"+std::to_string(carryCap())
            +" cargo to YARD near "+streetIntersectionAt(yardX,yardY)+" [E]";
    } else if(nol>=0){
        targetX=lots[nol].cx; targetY=lots[nol].cy; hasTarget=true;
        obj="Open contract lot near "+streetIntersectionAt(targetX,targetY)+" [C to bid]";
    } else if(activeJob>=0){
        const Job& job=jobs[activeJob]; targetX=job.x; targetY=job.y; hasTarget=true;
        if(job.jtype==JT_ROADSIDE)
            obj="Service call: "+job.desc+" near "+streetIntersectionAt(job.x,job.y)+" [Van + E]";
        else
            obj=std::string("Dispatch: ")+classWord(job.jclass)+" "+job.desc+" near "+streetIntersectionAt(job.x,job.y)+" [E]";
    } else obj="No pending calls — exploring regional routes";

    drawMinimap(e,targetX,targetY,hasTarget);

    int cw=std::min(780, W-32), ch=40, cx=16, cy=H-82;
    fillRect(e,cx,cy,cw,ch,16,20,30,225); strokeRect(e,cx,cy,cw,ch,70,120,170,220);
    drawText(e,obj.c_str(),cx+10,cy+10,SDL_Color{230,240,255,255},16);
    std::ostringstream cg; cg<<"Cargo: "<<carried.size()<<"/"<<carryCap();
    drawText(e,cg.str().c_str(),cx+cw-130,cy+10,carried.empty()?SDL_Color{150,165,185,255}:SDL_Color{255,215,80,255},16);

    if(newsTimer > 0.0f && !newsHeadline.empty()){
        int nw = std::min(860, W - 32), nh = 28;
        int nx = 16, ny = devMode ? 90 : 52;
        fillRect(e, nx, ny, nw, nh, 16, 22, 34, 245);
        strokeRect(e, nx, ny, nw, nh, newsColor.r, newsColor.g, newsColor.b, 255);
        drawText(e, ("OPEN-TTD NEWSWIRE: " + newsHeadline).c_str(), nx + 12, ny + 5, newsColor, 15);
    }

    int dockY=H-36, dockW=9*108, dockX=(W-dockW)/2;
    fillRect(e,0,H-38,W,38,12,16,24,240); fillRect(e,0,H-38,W,1,45,110,160,200);
    static const struct { const char* key; const char* label; } dockBtns[] = {
        {"[M]", "Market"}, {"[O]", "Fleet"}, {"[F]", "Banking"}, {"[T]", "Stocks"},
        {"[H]", "Towns"}, {"[P]", "Rating"}, {"[C]", "Bid Lot"}, {"[G]", "Settings"}, {"[F1]", "Dev"}
    };
    for(int i=0; i<9; ++i){
        int bx = dockX + i * 108;
        fillRect(e, bx + 2, dockY - 1, 104, 30, 24, 30, 42, 255);
        strokeRect(e, bx + 2, dockY - 1, 104, 30, 80, 110, 145, 255);
        std::string txt = std::string(dockBtns[i].key) + " " + dockBtns[i].label;
        drawText(e, txt.c_str(), bx + 52, dockY + 5, SDL_Color{210, 225, 245, 255}, 14, true);
    }

    if(flashTimer>0){ int bw=620,bh=40,x=(W-bw)/2,y=devMode?110:72; fillRect(e,x,y,bw,bh,18,26,34,245); strokeRect(e,x,y,bw,bh,100,210,240,255); drawText(e,flashMsg.c_str(),W/2,y+8,SDL_Color{180,240,255,255},20,true); }
    if(toastTimer>0){ int bw=740,bh=70,x=(W-bw)/2,y=devMode?150:114; fillRect(e,x,y,bw,bh,18,26,36,248); strokeRect(e,x,y,bw,bh,245,200,80,255); drawText(e,toastMsg.c_str(),W/2,y+10,SDL_Color{255,215,90,255},24,true); drawText(e,toastSub.c_str(),W/2,y+42,SDL_Color{220,235,245,255},18,true); }
}
inline void WorldState::drawPanels(Engine& e){
    const int W=e.logicalW(); const auto& cat=truckCatalog();
    if(panel==P_MARKET){
        int pw=760,ph=300,x=(W-pw)/2,y=(e.logicalH()-ph)/2; fillRect(e,x,y,pw,ph,18,20,30,242); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawText(e,"VEHICLE MARKETPLACE",W/2,y+16,SDL_Color{235,200,90,255},28,true);
        drawText(e,"Buy      - pay cash up front, you own it outright",x+34,y+66,SDL_Color{230,230,230,255},20);
        drawText(e,"Finance  - deposit plus monthly payments  (Phase 2)",x+34,y+100,SDL_Color{190,196,210,255},20);
        drawText(e,"Lease    - low upfront, fixed monthly cost  (Phase 2)",x+34,y+134,SDL_Color{190,196,210,255},20);
        int bx=x+pw/2-150,by=y+ph-86; fillRect(e,bx,by,300,46,40,120,70,255); strokeRect(e,bx,by,300,46,150,230,160,255);
        drawText(e,"Press B - Browse Trucks for Sale",x+pw/2,by+12,SDL_Color{235,245,235,255},20,true);
        drawText(e,"M to close",W/2,y+ph-24,SDL_Color{200,160,80,255},16,true);
    } else if(panel==P_TRUCKS){
        int pw=820,ph=430,x=(W-pw)/2,y=(e.logicalH()-ph)/2; fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawText(e,"TRUCKS FOR SALE",W/2,y+12,SDL_Color{235,200,90,255},28,true);
        std::ostringstream w; w<<"Your cash: $"<<cash<<"      Fleet: "<<ownedTrucks.size(); drawText(e,w.str().c_str(),W/2,y+48,SDL_Color{160,230,160,255},20,true);
        int rowY=y+84,rowH=46;
        for(int i=0;i<(int)cat.size();++i){ int ry=rowY+i*rowH; bool sel=(i==truckSel); bool afford=cash>=cat[i].price;
            fillRect(e,x+20,ry,pw-40,rowH-6,sel?42:26,sel?52:30,sel?70:40,255); if(sel)strokeRect(e,x+20,ry,pw-40,rowH-6,235,200,90,255);
            if(e.trucksTex){ SDL_Rect src{(i%e.truckCount)*e.truckCellW,0,e.truckCellW,e.truckCellH}; SDL_Rect dst{x+28,ry+3,34,34}; SDL_RenderCopy(e.renderer(),e.trucksTex,&src,&dst); }
            std::ostringstream nm; nm<<(i+1)<<". "<<cat[i].name<<"  ["<<cat[i].cap<<"]"; drawText(e,nm.str().c_str(),x+72,ry+9,SDL_Color{235,235,235,255},19);
            if(i==model()) drawText(e,"DRIVING",x+pw-320,ry+11,SDL_Color{150,200,255,255},15);
            std::ostringstream pr; pr<<"$"<<cat[i].price; drawText(e,pr.str().c_str(),x+pw-150,ry+9,afford?SDL_Color{150,230,150,255}:SDL_Color{220,120,120,255},19); }
        drawText(e,"Up/Down or 1-6  -  Enter to BUY  -  B/Esc back",W/2,y+ph-28,SDL_Color{190,200,220,255},18,true);
    } else if(panel==P_FLEET){
        int pw=820,ph=440,x=(W-pw)/2,y=(e.logicalH()-ph)/2; fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawText(e,"FLEET & OPERATIONS",W/2,y+12,SDL_Color{235,200,90,255},28,true);
        std::ostringstream w; w<<"Cash: $"<<cash<<"    Drivers: "<<drivers
            <<"    AI dispatch: "<<(!autoTrucks.empty()?"working calls":"needs a driver and spare truck");
        drawText(e,w.str().c_str(),W/2,y+48,SDL_Color{160,230,160,255},20,true);
        int rowY=y+86,rowH=42;
        const int rows=7;fleetPage=std::clamp(fleetPage,0,std::max(0,((int)ownedTrucks.size()-1)/rows));
        for(int i=fleetPage*rows;i<std::min((int)ownedTrucks.size(),(fleetPage+1)*rows);++i){ int ry=rowY+(i%rows)*rowH; const auto& tk=cat[ownedTrucks[i]]; fillRect(e,x+20,ry,pw-40,rowH-6,26,30,40,255);
            if(e.trucksTex){ SDL_Rect src{(ownedTrucks[i]%e.truckCount)*e.truckCellW,0,e.truckCellW,e.truckCellH}; SDL_Rect dst{x+26,ry+2,34,34}; SDL_RenderCopy(e.renderer(),e.trucksTex,&src,&dst); }
            drawText(e,tk.name,x+70,ry+8,SDL_Color{235,235,235,255},19);
            const bool dispatched=std::any_of(autoTrucks.begin(),autoTrucks.end(),[&](const auto& t){return t.slot==i;});
            const char* status=(i==drivenSlot)?"DRIVING (you)":dispatched?"AI DISPATCH":"idle - needs driver";
            SDL_Color sc=(i==drivenSlot)?SDL_Color{150,200,255,255}:dispatched?SDL_Color{150,230,150,255}:SDL_Color{200,170,120,255};
            drawText(e,status,x+pw-360,ry+8,sc,18);
            if(ownedTrucks[i]==4) drawText(e,"Roadside services",x+pw-180,ry+8,SDL_Color{100,235,225,255},16); }
        drawText(e,"H hire ($800)   [ ] change truck   PgUp/PgDn fleet pages   O/Esc close",W/2,y+ph-28,SDL_Color{190,200,220,255},18,true);
    } else if(panel==P_BID){
        const auto& opt=bidOptions();
        int pw=720,ph=360,x=(W-pw)/2,y=(e.logicalH()-ph)/2; fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawIcon(e,tt::IC_GAVEL,x+54,y+40,48);
        drawText(e,"CONTRACT BID",W/2,y+16,SDL_Color{235,200,90,255},28,true);
        int mine,rival,open; countLots(mine,rival,open); int total=mine+rival+open;
        std::ostringstream sub; sub<<"Win this building's tow calls.  Rival controls "<<(total?rival*100/total:0)<<"% of lots.   Your rep: "<<(int)reputation;
        drawText(e,sub.str().c_str(),W/2,y+54,SDL_Color{200,210,230,255},18,true);
        float rivalShare=total?(float)rival/total:0;
        float repBonus=(reputation-50.0f)/200.0f, rivalPenalty=(float)((rivalRep-50.0)/300.0);
        int rowY=y+96,rowH=64;
        for(int i=0;i<3;++i){ int ry=rowY+i*rowH; bool sel=(i==bidSel); fillRect(e,x+30,ry,pw-60,rowH-10,sel?42:26,sel?52:30,sel?70:40,255); if(sel)strokeRect(e,x+30,ry,pw-60,rowH-10,235,200,90,255);
            float chance=std::min(0.95f,std::max(0.08f,opt[i].winBase+repBonus-rivalPenalty-0.2f*rivalShare+diffBidBonus()-rivalBidPressure()));
            std::ostringstream nm; nm<<(i+1)<<".  "<<opt[i].name<<"   (win ~"<<(int)std::round(chance*100)<<"%)";
            drawText(e,nm.str().c_str(),x+48,ry+6,SDL_Color{235,235,235,255},20);
            drawText(e,opt[i].note,x+48,ry+32,SDL_Color{180,190,210,255},15); }
        drawText(e,"Up/Down or 1-3  -  Enter to submit bid  -  Esc cancel",W/2,y+ph-28,SDL_Color{190,200,220,255},18,true);
    } else if(panel==P_BANK){
        const int pw=920,ph=532,x=(W-pw)/2,y=std::max(0,(e.logicalH()-ph-28)/2);
        fillRect(e,x,y,pw,ph,18,20,30,245);strokeRect(e,x,y,pw,ph,235,200,90,255);
        if(!loans.empty())selectedLoan=std::min(selectedLoan,loans.size()-1);
        drawText(e,"FINANCES & LOANS",W/2,y+10,SDL_Color{235,200,90,255},26,true);
        long debt=0;for(const auto& l:loans)debt+=l.balance;
        std::ostringstream a;a<<"Cash $"<<cash<<"   Debt $"<<debt<<"   Reputation "<<(int)reputation<<"   Value $"<<computeValuation();
        drawText(e,a.str().c_str(),x+28,y+48,SDL_Color{200,220,245,255},19);
        std::ostringstream b;b<<"Last week: revenue $"<<lastWeekRevenue<<"   cash out $"<<lastWeekExpense<<"   surplus $"<<lastWeekRevenue-lastWeekExpense;
        drawText(e,b.str().c_str(),x+28,y+76,SDL_Color{200,220,230,255},18);
        const std::string eligibility="Credit history: "+std::to_string(positiveMonths)+"/6 positive months. One active loan; payment <= 50% of surplus.";
        drawText(e,eligibility.c_str(),x+28,y+104,SDL_Color{220,205,160,255},17);
        drawText(e,"ACTIVE LOANS  (Up/Down to select)",x+28,y+136,SDL_Color{220,225,235,255},18);
        const std::size_t first=loans.empty()?0:(selectedLoan/3)*3;
        if(loans.empty())drawText(e,"No active loans",x+38,y+168,SDL_Color{180,190,210,255},18);
        for(std::size_t row=0;row<3&&first+row<loans.size();++row){
            const auto& loan=loans[first+row];int ry=y+164+(int)row*30;
            fillRect(e,x+28,ry,pw-56,27,first+row==selectedLoan?44:28,42,56,255);
            std::ostringstream l;l<<(first+row+1)<<". Balance $"<<loan.balance<<"   payment $"<<loan.weeklyPayment<<"/week";
            drawText(e,l.str().c_str(),x+38,ry+4,SDL_Color{225,230,240,255},18);
        }
        if(!loans.empty()){
            std::string selection="Selected "+std::to_string(selectedLoan+1)+" / "+std::to_string(loans.size());
            drawText(e,selection.c_str(),x+28,y+260,SDL_Color{190,220,245,255},17);
        }
        drawText(e,"R: repay $500    Shift+R: repay as much as possible",x+28,y+286,SDL_Color{150,230,170,255},17);
        drawText(e,"BORROW  (locked products explain why when selected)",x+28,y+324,SDL_Color{220,225,235,255},18);
        const auto& lc=loanCatalog();
        for(int i=0;i<(int)lc.size();++i){
            int total=(int)std::round(lc[i].principal*(1.0+lc[i].apr*lc[i].weeks/52.0));int wk=(total+lc[i].weeks-1)/lc[i].weeks;
            bool allowed=!simulation::loanRejection(cash,tows,positiveMonths,(int)loans.size(),lastWeekRevenue-lastWeekExpense,wk);
            int ry=y+354+i*40;fillRect(e,x+28,ry,pw-56,34,30,34,46,255);
            std::ostringstream line;line<<(i+1)<<". "<<lc[i].name<<"  +$"<<lc[i].principal<<"   "<<(int)std::round(lc[i].apr*100)<<"% flat annual rate";
            drawText(e,line.str().c_str(),x+40,ry+6,allowed?SDL_Color{235,235,235,255}:SDL_Color{150,155,170,255},18);
            std::ostringstream pay;pay<<"$"<<wk<<"/wk, up to "<<lc[i].weeks<<" wk";
            drawText(e,pay.str().c_str(),x+pw-265,ry+6,SDL_Color{180,195,215,255},17);
        }
        drawText(e,"1-3 apply for a loan   F/Esc close   Payments include prepaid interest",W/2,y+ph-28,SDL_Color{190,200,220,255},17,true);
    } else if(panel==P_STOCK){
        int pw=840,ph=std::min(500, e.logicalH()-40),x=(W-pw)/2,y=(e.logicalH()-ph)/2;
        fillRect(e,x,y,pw,ph,16,20,32,248); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawIcon(e,tt::IC_CHART,x+54,y+40,48);
        drawText(e,"STOCK MARKET & CAPITALIZATION",W/2,y+14,SDL_Color{235,200,90,255},26,true);
        long val=computeValuation(); long rv=rivalValuation();
        // Your company
        drawText(e,"YOUR COMPANY",x+40,y+56,SDL_Color{140,200,255,255},20);
        std::ostringstream a; a<<"Valuation: $"<<val<<"     Status: "<<(isPublic?"PUBLIC (Listed)":"PRIVATE (Unlisted)");
        drawText(e,a.str().c_str(),x+40,y+82,SDL_Color{220,232,248,255},18);
        if(isPublic){
            std::ostringstream s; s<<"Share price $"<<std::fixed<<std::setprecision(3)<<sharePrice<<"   You own "<<(int)std::round(youOwnFrac*100)<<"%   Shares "<<(long)sharesOut;
            drawText(e,s.str().c_str(),x+40,y+106,SDL_Color{140,240,170,255},17);
        } else {
            std::ostringstream s; s<<"Go public to raise ~$"<<(long)(std::max(0L,val)*0.25)<<" (sell 25%). Needs valuation $"<<kIpoThreshold<<"+";
            drawText(e,s.str().c_str(),x+40,y+106,(val>=kIpoThreshold)?SDL_Color{160,240,180,255}:SDL_Color{220,180,140,255},17);
        }
        // Rival company
        drawText(e,"RIVAL COMPANY",x+40,y+136,SDL_Color{240,150,150,255},20);
        std::ostringstream b; b<<"Valuation: $"<<rv<<"     You own "<<(int)std::round(rivalStake*100)<<"% of the rival";
        drawText(e,b.str().c_str(),x+40,y+162,SDL_Color{240,220,220,255},18);
        long blk=(long)(rv*0.10);
        std::ostringstream bb; bb<<"Buy 10% block of rival for $"<<blk<<". Reach 50% to take over.";
        drawText(e,bb.str().c_str(),x+40,y+186,SDL_Color{220,210,210,255},16);
        // Actions
        int ay=y+214;
        auto action=[&](const char* key,const char* label,bool enabled){
            SDL_Color col=enabled?SDL_Color{245,245,245,255}:SDL_Color{130,135,145,255};
            fillRect(e,x+40,ay,pw-80,34,enabled?32:22,enabled?40:26,enabled?56:32,255);
            strokeRect(e,x+40,ay,pw-80,34,enabled?140:70,enabled?170:85,enabled?210:105,255);
            drawText(e,(std::string("[")+key+"]  "+label).c_str(),x+56,ay+6,col,18);
            ay+=40;
        };
        long sellVal = isPublic ? (long)std::round(sharesOut * 0.05 * sharePrice) : 0;
        std::string sellTxt = "Sell 5% of your shares (raise $" + std::to_string(sellVal) + " cash)";
        action("S", isPublic ? sellTxt.c_str() : "Sell Shares (Go public first)", isPublic && youOwnFrac >= 0.05);
        long buyVal = isPublic ? (long)std::round(sharesOut * 0.05 * sharePrice) : 0;
        std::string buyTxt = "Buy back 5% shares (costs $" + std::to_string(buyVal) + ")";
        action("B", isPublic ? buyTxt.c_str() : "Buy Back Shares (Go public first)", isPublic && cash >= buyVal && youOwnFrac <= 0.95);
        action("I", isPublic?"Already public":"Go Public (IPO) - sell 25% for cash", !isPublic && val>=kIpoThreshold);
        action("R","Buy 10% of the rival", rivalStake<0.999 && cash>=blk && tows>=6);
        action("U", wonDistrict?"Rival acquired":"Acquire the rival (needs 50% stake)", rivalStake>=0.5 && !wonDistrict);
        action("N", (std::string("Expand to District ")+std::to_string(district+1)).c_str(), wonDistrict && district<simulation::maxDistrict);
        if(wonDistrict) drawIcon(e,tt::IC_TROPHY,x+pw-70,y+60,48);
        drawText(e,"S sell shares  B buy back  I / R / U / N to act  -  T/Esc to close",W/2,y+ph-24,SDL_Color{190,200,220,255},16,true);
    } else if(panel==P_SETTINGS){
        int pw=760,ph=486,x=(W-pw)/2,y=(e.logicalH()-ph)/2; fillRect(e,x,y,pw,ph,18,20,30,248); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawText(e,"SETTINGS  /  PAUSE",W/2,y+12,SDL_Color{235,200,90,255},28,true);
        std::ostringstream info; info<<"District "<<district<<"     Rival: "<<rivalCompanyName(rivalKind)<<" ("<<rivalKindName(rivalKind)<<")";
        drawText(e,info.str().c_str(),W/2,y+48,SDL_Color{200,210,235,255},18,true);
        int ry=y+84, rh=40;
        auto row=[&](const char* key,const std::string& label,const std::string& val,SDL_Color vc){
            fillRect(e,x+34,ry,pw-68,rh-8,28,32,44,255); strokeRect(e,x+34,ry,pw-68,rh-8,90,110,140,255);
            drawText(e,(std::string("[")+key+"]  "+label).c_str(),x+50,ry+8,SDL_Color{230,232,240,255},19);
            drawText(e,val.c_str(),x+pw-230,ry+8,vc,19); ry+=rh; };
        SDL_Color on{150,230,160,255}, off{200,160,120,255};
        row("D","Difficulty",difficultyName(), difficulty==2?SDL_Color{230,160,150,255}:difficulty==0?on:SDL_Color{220,220,180,255});
        row("W","Weather & seasons",optWeather?"On":"Off", optWeather?on:off);
        row("H","HUD hints",optHints?"On":"Off", optHints?on:off);
        row("A","Weekly autosave",optAutosave?"On":"Off", optAutosave?on:off);
        row("Z","Reset camera zoom","2.0x", SDL_Color{200,210,235,255});
        if(wonDistrict && district<simulation::maxDistrict) row("N",std::string("Expand to District ")+std::to_string(district+1),"Ready!", on);
        row("S","Save now","F5", SDL_Color{200,210,235,255});
        row("Q","Save & quit to menu","", SDL_Color{230,180,180,255});
        drawText(e,saveProtected?"Save protected: restore file + F9, or move aside + S. Close window to exit safely.":"Press a key to change   -   G/Esc to resume",W/2,y+ph-26,SDL_Color{190,200,220,255},16,true);
    } else if(panel==P_TOWNS){
        int pw=880,ph=std::min(510, e.logicalH()-20),x=(W-pw)/2,y=(e.logicalH()-ph)/2;
        fillRect(e,x,y,pw,ph,18,22,34,248); strokeRect(e,x,y,pw,ph,100,200,240,255);
        if(e.openttdGuiTex){ SDL_Rect src{192, 0, 16, 16}; SDL_Rect dst{x+24, y+14, 28, 28}; SDL_RenderCopy(e.renderer(), e.openttdGuiTex, &src, &dst); }
        drawText(e,"TOWN DIRECTORY & LOCAL AUTHORITY",W/2,y+14,SDL_Color{100,220,240,255},26,true);
        drawText(e,"OpenTTD Civic Service Network - Deliver tows to raise council standing",W/2,y+42,SDL_Color{180,200,225,255},15,true);

        int lx = x + 24, ly = y + 68, lw = 320;
        for(int i=0; i<(int)cities.size(); ++i){
            const auto& c = cities[i];
            bool sel = (i == townSel);
            int cy = ly + i * 56;
            fillRect(e, lx, cy, lw, 50, sel?45:26, sel?60:32, sel?85:42, 255);
            strokeRect(e, lx, cy, lw, 50, sel?245:80, sel?210:95, sel?70:120, 255);
            std::string tTitle = std::to_string(i+1) + ". " + c.name;
            drawText(e, tTitle.c_str(), lx + 12, cy + 8, sel?SDL_Color{255,235,120,255}:SDL_Color{230,235,245,255}, 16);
            std::string sub = "Pop: " + std::to_string(c.population) + " | Rating: " + councilRatingName(c.councilRating);
            drawText(e, sub.c_str(), lx + 12, cy + 28, councilRatingColor(c.councilRating), 13);
            if(c.exclusiveContract){
                fillRect(e, lx + lw - 36, cy + 8, 26, 16, 220, 180, 40, 255);
                drawText(e, "EX", lx + lw - 32, cy + 9, SDL_Color{20,20,20,255}, 11);
            }
        }

        if(townSel >= 0 && townSel < (int)cities.size()){
            const auto& c = cities[townSel];
            int rx = lx + lw + 24, ry = ly, rw = pw - lw - 72;
            fillRect(e, rx, ry, rw, 336, 24, 28, 40, 255);
            strokeRect(e, rx, ry, rw, 336, 90, 110, 140, 255);

            drawText(e, c.name.c_str(), rx + 16, ry + 12, SDL_Color{255,230,110,255}, 22);
            std::string zname = (c.zone == tt::Z_RES) ? "Residential Zone" : (c.zone == tt::Z_COM) ? "Commercial Zone" : "Industrial Zone";
            drawText(e, zname.c_str(), rx + 16, ry + 38, SDL_Color{160,200,230,255}, 15);

            std::string popStr = "Population: " + std::to_string(c.population) + " residents  (Tier " + std::to_string(c.growthLevel) + " Town)";
            drawText(e, popStr.c_str(), rx + 16, ry + 64, SDL_Color{220,225,235,255}, 15);

            std::string towStr = "Company Tows Completed: " + std::to_string(c.towsCompleted);
            drawText(e, towStr.c_str(), rx + 16, ry + 86, SDL_Color{220,225,235,255}, 15);

            std::string crat = "Council Standing: " + std::string(councilRatingName(c.councilRating)) + " (" + (c.councilRating >= 0 ? "+" : "") + std::to_string(c.councilRating) + "/1000)";
            drawText(e, crat.c_str(), rx + 16, ry + 112, councilRatingColor(c.councilRating), 16);

            std::string cstat = c.exclusiveContract ? "Contract Status: EXCLUSIVE TOWING RIGHTS ACTIVE (+25% BONUS)"
                                                   : "Contract Status: Open / Competitive Bidding";
            drawText(e, cstat.c_str(), rx + 16, ry + 136, c.exclusiveContract?SDL_Color{255,215,90,255}:SDL_Color{180,185,195,255}, 14);

            drawText(e, "TOWN COUNCIL ACTIONS:", rx + 16, ry + 168, SDL_Color{130,210,245,255}, 15);
            auto drawAct = [&](int actY, const char* key, const char* label, const char* cost, bool avail, SDL_Color col){
                fillRect(e, rx + 16, actY, rw - 32, 34, 32, 38, 52, 255);
                strokeRect(e, rx + 16, actY, rw - 32, 34, avail?col.r:100, avail?col.g:100, avail?col.b:100, 255);
                std::string kstr = std::string("[") + key + "] " + label;
                drawText(e, kstr.c_str(), rx + 24, actY + 7, avail?col:SDL_Color{130,135,145,255}, 14);
                drawText(e, cost, rx + rw - 130, actY + 7, avail?SDL_Color{240,230,140,255}:SDL_Color{120,120,120,255}, 14);
            };
            drawAct(ry + 192, "B", "Fund New Town Buildings", "$2,500", cash >= 2500, SDL_Color{100,240,150,255});
            drawAct(ry + 232, "R", "Sponsor Municipal Road Works", "$1,200", cash >= 1200, SDL_Color{100,220,240,255});
            drawAct(ry + 272, "X", "Sign Exclusive Service Contract", "$3,000", cash >= 3000 && c.councilRating >= 600 && !c.exclusiveContract, SDL_Color{255,215,80,255});
            drawAct(ry + 312, "C", "Bribe the Town Council (75% Win)", "$5,000", cash >= 5000, SDL_Color{240,130,100,255});
        }

        drawText(e, "1-6 or UP/DOWN to Select Town  |  B / R / X / C to Act  |  H or ESC to Close", W/2, y+ph-20, SDL_Color{170,185,210,255}, 15, true);
    } else if(panel==P_PERF){
        int pw=820,ph=std::min(490, e.logicalH()-20),x=(W-pw)/2,y=(e.logicalH()-ph)/2;
        fillRect(e,x,y,pw,ph,16,20,32,248); strokeRect(e,x,y,pw,ph,235,200,90,255);
        if(e.openttdGuiTex){ SDL_Rect src{144, 0, 16, 16}; SDL_Rect dst{x+24, y+14, 28, 28}; SDL_RenderCopy(e.renderer(), e.openttdGuiTex, &src, &dst); }
        drawText(e,"COMPANY DETAILED PERFORMANCE RATING",W/2,y+14,SDL_Color{245,215,95,255},26,true);
        drawText(e,"OpenTTD Tycoon Evaluation Matrix - Maximum 1,000 Points",W/2,y+42,SDL_Color{180,200,225,255},15,true);

        int mineLots, rivalLots, openLots; countLots(mineLots, rivalLots, openLots);
        int vScore = std::min(120, (int)ownedTrucks.size() * 20);
        int lScore = std::min(120, mineLots * 30);
        int dScore = std::min(150, tows * 5);
        int pScore = std::min(150, std::max(0, (lastWeekRevenue - lastWeekExpense) / 20));
        int valScore = std::min(200, (int)(computeValuation() / 1000));
        int debtScore = loans.empty() ? 100 : std::max(0, 100 - (int)loans[0].balance / 50);
        int cSum = 0;
        for(const auto& c : cities) cSum += std::clamp(c.councilRating, 0, 1000);
        int councilScore = cities.empty() ? 0 : std::min(160, (int)(cSum * 160L / ((long)cities.size() * 1000L)));

        int totalScore = vScore + lScore + dScore + pScore + valScore + debtScore + councilScore;

        const char* rankTitle = "Apprentice Operator";
        SDL_Color rankCol{180,180,180,255};
        if(totalScore >= 900) { rankTitle = "TYCOON"; rankCol = SDL_Color{255,215,70,255}; }
        else if(totalScore >= 750) { rankTitle = "MANAGING DIRECTOR"; rankCol = SDL_Color{100,230,245,255}; }
        else if(totalScore >= 550) { rankTitle = "CHIEF EXECUTIVE"; rankCol = SDL_Color{120,240,140,255}; }
        else if(totalScore >= 350) { rankTitle = "FLEET MANAGER"; rankCol = SDL_Color{240,220,90,255}; }
        else if(totalScore >= 150) { rankTitle = "LOCAL CONTRACTOR"; rankCol = SDL_Color{240,150,80,255}; }

        fillRect(e, x + 34, y + 68, pw - 68, 44, 28, 36, 52, 255);
        strokeRect(e, x + 34, y + 68, pw - 68, 44, rankCol.r, rankCol.g, rankCol.b, 255);
        std::string rankStr = std::string("CURRENT RANK: ") + rankTitle + "  (" + std::to_string(totalScore) + " / 1000 Points)";
        drawText(e, rankStr.c_str(), W/2, y + 80, rankCol, 20, true);

        int ry = y + 124, rh = 38;
        auto drawRow = [&](const char* category, int score, int maxScore, const std::string& detail){
            fillRect(e, x + 34, ry, pw - 68, rh - 6, 24, 28, 40, 255);
            strokeRect(e, x + 34, ry, pw - 68, rh - 6, 75, 90, 115, 255);
            drawText(e, category, x + 46, ry + 6, SDL_Color{220,230,245,255}, 16);

            int bx = x + 260, bw = 240, bh = 14;
            fillRect(e, bx, ry + 8, bw, bh, 35, 42, 58, 255);
            int fillW = (int)((float)score / maxScore * bw);
            if(fillW > 0) fillRect(e, bx, ry + 8, fillW, bh, 80, 200, 120, 255);
            strokeRect(e, bx, ry + 8, bw, bh, 120, 140, 170, 255);

            std::string sText = std::to_string(score) + " / " + std::to_string(maxScore) + " pts";
            drawText(e, sText.c_str(), bx + bw + 16, ry + 6, SDL_Color{240,230,120,255}, 15);

            drawText(e, detail.c_str(), x + pw - 180, ry + 7, SDL_Color{160,185,210,255}, 14);
            ry += rh;
        };

        drawRow("1. Fleet Vehicles", vScore, 120, std::to_string(ownedTrucks.size()) + " Trucks");
        drawRow("2. Contract Lots Held", lScore, 120, std::to_string(mineLots) + " Lots");
        drawRow("3. Total Tows Delivered", dScore, 150, std::to_string(tows) + " Tows");
        drawRow("4. Weekly Net Profit", pScore, 150, "+$" + std::to_string(std::max(0, lastWeekRevenue - lastWeekExpense)));
        drawRow("5. Company Valuation", valScore, 200, "$" + std::to_string(computeValuation()));
        drawRow("6. Debt / Loan Clearance", debtScore, 100, loans.empty() ? "Debt Free" : "$" + std::to_string(loans[0].balance));
        drawRow("7. Town Council Favor", councilScore, 160, std::to_string(cities.size()) + " Municipalities");

        drawText(e, "Press P or ESC to Close", W/2, y+ph-20, SDL_Color{170,185,210,255}, 15, true);
    }
}
inline void WorldState::render(Engine& e){ drawWorld(e); drawLots(e); drawEntities(e);
    if(weather==W_RAIN) fillRect(e,0,0,e.logicalW(),e.logicalH(),40,60,95,45);
    else if(weather==W_STORM) fillRect(e,0,0,e.logicalW(),e.logicalH(),18,26,52,95);
    if(weather==W_RAIN||weather==W_STORM){
        const int count=weather==W_STORM?150:95;
        const int width=e.logicalW(),height=e.logicalH();
        const int phase=(int)(pulse*260.0f);
        SDL_SetRenderDrawColor(e.renderer(),weather==W_STORM?170:145,
            weather==W_STORM?195:205,weather==W_STORM?240:230,weather==W_STORM?175:145);
        for(int i=0;i<count;++i){
            const int x=(i*97+phase*3)%(width>0?width:1);
            const int y=(i*53+phase*5)%(height>0?height:1);
            SDL_RenderDrawLine(e.renderer(),x,y,x-5,y+12);
        }
    }
        drawHud(e); if(panel!=P_NONE){
        fillRect(e,0,0,e.logicalW(),e.logicalH(),0,0,0,120);drawPanels(e);
        if(flashTimer>0){fillRect(e,0,e.logicalH()-26,e.logicalW(),26,12,18,22,250);
            drawText(e,flashMsg.c_str(),e.logicalW()/2,e.logicalH()-24,SDL_Color{255,225,150,255},17,true);}
    }
}

inline bool WorldState::prepareQuit(Engine& e){
    ensureLoaded(e);
    if(saveProtected)return true;
    std::string error;
    if(saveGame(e,&error))return true;
    panel=P_SETTINGS;flashMsg="Cannot quit safely: "+error;flashTimer=6;
    return false;
}








