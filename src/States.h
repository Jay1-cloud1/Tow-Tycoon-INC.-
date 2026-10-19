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
#include <system_error>
#include <limits>
#include <queue>
#include <random>
#include <sstream>
#include <string>
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
    bool showSettings=false;
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
    void startFresh(){ loadedOnce=true; }
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
    bool t_autoActive()const{return autoT.active;}
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
    struct Job { float x,y; int payout; bool active; int carType; int angle; int jclass; int zone; int jtype; std::string desc; };
    struct Carried { int payout, carType, jclass; };
    // A*-routed self-driving truck - used for your company's auto truck AND rival trucks
    struct AutoTruck { bool active=false; int model=0; float x=0,y=0; int facing=tt::SOUTH;
        std::vector<std::pair<int,int>> route; int ri=0; int phase=0; /*0 seek,1 to-job,2 to-yard*/
        int carType=0, payout=0, jclass=0, jobType=0; int kind=-1; /*rival personality; -1 = player/auto*/ };
    struct Lot { float cx,cy; int owner; float incomeMult; int bType; int baseRev; }; // owner -1 open,0 you,1 rival
    struct Loan { int balance; int weeklyPayment; };
    enum Panel { P_NONE, P_MARKET, P_TRUCKS, P_FLEET, P_BID, P_BANK, P_STOCK, P_SETTINGS };
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
    int  fleetMaxClass() const { int m=0; for(int id:ownedTrucks) m=std::max(m,truckCatalog()[id].maxClass); return m; }
    int  zoneAtTileX(int tx) const { return tx < mapW/3 ? tt::Z_RES : (tx < 2*mapW/3 ? tt::Z_COM : tt::Z_IND); }
    std::vector<std::pair<int,int>> findPath(int sx,int sy,int gx,int gy) const; // A* over road tiles
    void updateAutoTruck(Engine& e, float dt);
    void stepTruck(AutoTruck& t, bool rival, float dt);   // shared A* drive+service loop
    void updateRivals(float dt);
    const char* jobTypeTag(int jt) const { return jt==JT_IMPOUND?"IMPOUND":jt==JT_REPO?"REPO":""; }
    std::string streetIntersectionAt(float wx,float wy) const;
    void drawMinimap(Engine& e,float targetX,float targetY,bool hasTarget) const;

    static constexpr int kRoadSpacing=8;
    int mapW=80,mapH=60,tile=16;
    std::vector<int> tiles; std::vector<Building> buildings; std::vector<Lot> lots;

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
    AutoTruck autoT; std::vector<AutoTruck> rivalTrucks;
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
    long rivalValuation() const { int mine,rv,op; countLots(mine,rv,op); return (long)rivalCash + (long)rv*1500; }

    static constexpr float kSecondsPerDay = 12.0f;
    float dayAccum=0; int day=1, week=1, month=1, year=1;
    bool loadedOnce=false;
};

// ------------------------------ MenuState impl ------------------------------
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
        case B_SETTINGS: showSettings=true; return;
        case B_QUIT: e.requestQuit(); return;
    }
}
inline void MenuState::handleEvent(Engine& e, SDL_Event& ev){
    if (showSettings){
        if (ev.type==SDL_KEYDOWN){ const SDL_Keycode k=ev.key.keysym.sym;
            if (k==SDLK_f||k==SDLK_F11) e.toggleFullscreen();
            else if (k==SDLK_ESCAPE||k==SDLK_s||k==SDLK_RETURN||k==SDLK_KP_ENTER) showSettings=false; }
        if (ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button==SDL_BUTTON_LEFT) showSettings=false;
        return;
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
        bool h=!showSettings&&enabled&&(mx>=b.rect.x&&mx<=b.rect.x+b.rect.w&&my>=b.rect.y&&my<=b.rect.y+b.rect.h);
        const bool warn=(b.id==B_NEW&&confirmNew);
        if(!enabled) fillRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,52,58,72,255);
        else if(warn) fillRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,h?214:184,h?84:64,h?64:48,255);
        else fillRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,h?58:34,h?150:108,h?232:184,255);
        fillRect(e,b.rect.x,b.rect.y,b.rect.w,4,255,255,255,h?70:40); fillRect(e,b.rect.x,b.rect.y+b.rect.h-4,b.rect.w,4,0,0,0,60);
        strokeRect(e,b.rect.x,b.rect.y,b.rect.w,b.rect.h,12,20,34,255); if(h)strokeRect(e,b.rect.x-2,b.rect.y-2,b.rect.w+4,b.rect.h+4,235,200,90,255);
        drawText(e,b.label,b.rect.x+b.rect.w/2,b.rect.y+(b.rect.h-28)/2,enabled?SDL_Color{245,245,245,255}:SDL_Color{140,146,160,255},26,true); }
    const char* hint=confirmNew?"Your current save will be replaced  -  Enter or click again to confirm, Esc to cancel"
        :"Enter to play   N new game   S settings   Esc quit";
    drawText(e,hint,cx,e.logicalH()-40,confirmNew?SDL_Color{240,190,150,255}:SDL_Color{190,198,214,255},18,true);
    drawText(e,"v" TOWTYCOON_VERSION,e.logicalW()-70,e.logicalH()-30,SDL_Color{150,158,176,255},16);
    if (showSettings){
        fillRect(e,0,0,e.logicalW(),e.logicalH(),0,0,0,150);
        const int pw=560,ph=300,x=(e.logicalW()-pw)/2,y=(e.logicalH()-ph)/2;
        fillRect(e,x,y,pw,ph,18,20,30,248); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawText(e,"SETTINGS",cx,y+16,SDL_Color{235,200,90,255},28,true);
        fillRect(e,x+34,y+72,pw-68,34,28,32,44,255); strokeRect(e,x+34,y+72,pw-68,34,90,110,140,255);
        drawText(e,"[F]  Fullscreen",x+50,y+79,SDL_Color{230,232,240,255},19);
        drawText(e,e.isFullscreen()?"On":"Off",x+pw-120,y+79,e.isFullscreen()?SDL_Color{150,230,160,255}:SDL_Color{200,160,120,255},19);
        drawText(e,"Difficulty, weather, hints and autosave are saved",cx,y+130,SDL_Color{200,210,235,255},17,true);
        drawText(e,"with each game: press G while playing to change them.",cx,y+154,SDL_Color{200,210,235,255},17,true);
        drawText(e,"F11 toggles fullscreen anywhere on this menu.",cx,y+190,SDL_Color{170,180,200,255},16,true);
        drawText(e,"F to toggle   -   Esc / click to close",cx,y+ph-34,SDL_Color{190,200,220,255},17,true);
    }
}

// ------------------------------ WorldState impl -----------------------------
inline WorldState::WorldState(std::uint64_t randomSeed):rng(randomSeed){
    tiles.assign(simulation::tileCount(mapW,mapH),tt::GRASS); generateCity();
    int ix=((mapW/2)/kRoadSpacing)*kRoadSpacing, iy=((mapH/2)/kRoadSpacing)*kRoadSpacing;
    truckX=(ix+0.5f)*tile; truckY=(iy+0.5f)*tile; yardX=truckX; yardY=truckY-kRoadSpacing*tile;
    rivalYardX=(kRoadSpacing+0.5f)*tile; rivalYardY=(kRoadSpacing+0.5f)*tile; // rival depot, NW corner
    configureRival();
    // rival starts with a small foothold so there's competition from the outset
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
    for (int y=0;y<mapH;++y) for (int x=0;x<mapW;++x){ bool rx=(x%kRoadSpacing==0),ry=(y%kRoadSpacing==0); int t=tt::GRASS;
        if(rx||ry)t=tt::ROAD; else if(x%kRoadSpacing==1||x%kRoadSpacing==kRoadSpacing-1||y%kRoadSpacing==1||y%kRoadSpacing==kRoadSpacing-1)t=tt::SIDEWALK; tiles[simulation::tileIndex(x,y,mapW)]=t; }
    { int wx=(seed%2)?14:54, wy=(seed%3)?40:16; for (int y=wy;y<wy+6;++y) for(int x=wx;x<wx+8;++x) if(x<mapW&&y<mapH&&tiles[simulation::tileIndex(x,y,mapW)]==tt::GRASS) tiles[simulation::tileIndex(x,y,mapW)]=tt::WATER; }
    std::mt19937 g(seed);
    for (int by=kRoadSpacing;by<mapH-kRoadSpacing;by+=kRoadSpacing) for (int bx=kRoadSpacing;bx<mapW-kRoadSpacing;bx+=kRoadSpacing){
        if(g()%100<70){ int type=(int)(g()%6),tx=bx+2,ty=by+2; buildings.push_back({tx,ty,type});
            for(int xx=tx;xx<tx+3;++xx) if(xx<mapW&&ty+3<mapH) tiles[simulation::tileIndex(xx,ty+3,mapW)]=tt::SIDEWALK;
            // each building gets an adjacent "lot" (the space in front)
            lots.push_back({ (tx+1.5f)*tile, (ty+4.4f)*tile, -1, 1.0f, type, (int)((400+type*60)*revScale) }); }
        else for(int k=0;k<3;++k){ int tx=bx+1+(int)(g()%5),ty=by+1+(int)(g()%5); if(tx<mapW&&ty<mapH&&tiles[simulation::tileIndex(tx,ty,mapW)]==tt::GRASS) tiles[simulation::tileIndex(tx,ty,mapW)]=tt::TREE; } }
}
// Expand into a fresh, tougher district. Keeps your company (cash, fleet, drivers,
// reputation, loans, tows); regenerates the map and installs a new rival.
inline void WorldState::startDistrict(int n){
    district=n;
    tiles.assign(simulation::tileCount(mapW,mapH),tt::GRASS); buildings.clear(); lots.clear();
    generateCity(99u + (unsigned)n*7u, 1.0f + 0.25f*(n-1));
    int ix=((mapW/2)/kRoadSpacing)*kRoadSpacing, iy=((mapH/2)/kRoadSpacing)*kRoadSpacing;
    truckX=(ix+0.5f)*tile; truckY=(iy+0.5f)*tile; yardX=truckX; yardY=truckY-kRoadSpacing*tile;
    rivalYardX=(kRoadSpacing+0.5f)*tile; rivalYardY=(kRoadSpacing+0.5f)*tile;
    wonDistrict=false; announcedMajority=false; rivalStake=0.0;
    configureRival();
    rivalCash *= (1.0 + 0.6*(n-1));                 // new rival starts richer each district
    rivalMaxLots=40; rivalTrucks.clear();
    // the new rival already holds a larger foothold
    int foot=3+2*(n-1); for(int i=0,placed=0;i<(int)lots.size()&&placed<foot;i+=7){ lots[i].owner=1; lots[i].incomeMult=1.0f; placed++; }
    jobs.clear(); carried.clear(); for(int i=0;i<3;++i) spawnJob();
    toastMsg=std::string("Welcome to District ")+std::to_string(n)+"!";
    toastSub=std::string(rivalCompanyName(rivalKind))+" ("+rivalKindName(rivalKind)+") runs this turf - take it from them."; toastTimer=8.0f;
}
inline int WorldState::tileAtWorld(float wx,float wy) const { int tx=(int)std::floor(wx/tile),ty=(int)std::floor(wy/tile); if(tx<0||tx>=mapW||ty<0||ty>=mapH)return -1; return tiles[simulation::tileIndex(tx,ty,mapW)]; }
inline void WorldState::worldToScreen(Engine& e,float wx,float wy,int& sx,int& sy) const { float z=e.camera.zoom; sx=(int)std::round((wx-e.camera.x)*z+e.logicalW()*0.5f); sy=(int)std::round((wy-e.camera.y)*z+e.logicalH()*0.5f); }
inline std::string WorldState::streetIntersectionAt(float wx,float wy) const {
    static const std::array<const char*,10> eastWest{{"Main St","Oak Ave","Pine St","Maple Ave","Harbor Blvd","Elm Rd","Cedar St","Sunset Dr","2nd St","Industrial Way"}};
    static const std::array<const char*,8> northSouth{{"1st Ave","Market St","River Rd","Broadway","Hill St","Union Ave","Lake Dr","Depot St"}};
    const int xRoads=(mapW-1)/kRoadSpacing+1, yRoads=(mapH-1)/kRoadSpacing+1;
    const int xIndex=std::clamp((int)std::round(wx/(tile*kRoadSpacing)),0,xRoads-1);
    const int yIndex=std::clamp((int)std::round(wy/(tile*kRoadSpacing)),0,yRoads-1);
    return std::string(eastWest[std::min<std::size_t>(yIndex,eastWest.size()-1)])
        +" & "+northSouth[std::min<std::size_t>(xIndex,northSouth.size()-1)];
}
inline void WorldState::drawMinimap(Engine& e,float targetX,float targetY,bool hasTarget) const {
    const int panelW=194,panelH=168,panelX=e.logicalW()-panelW-8,panelY=82;
    fillRect(e,panelX,panelY,panelW,panelH,12,16,22,225);
    strokeRect(e,panelX,panelY,panelW,panelH,140,160,175,255);
    drawText(e,"CITY MAP  YOU / OBJECTIVE",panelX+8,panelY+4,SDL_Color{220,228,240,255},12);
    const int mapX=panelX+13,mapY=panelY+22,mapWpx=168,mapHpx=126;
    fillRect(e,mapX,mapY,mapWpx,mapHpx,44,52,52,255);
    for(int ty=0;ty<mapH;++ty) for(int tx=0;tx<mapW;++tx){
        const int tileType=tiles[simulation::tileIndex(tx,ty,mapW)];
        if(tileType!=tt::ROAD && tileType!=tt::WATER) continue;
        const int left=mapX+tx*mapWpx/mapW, right=mapX+(tx+1)*mapWpx/mapW;
        const int top=mapY+ty*mapHpx/mapH, bottom=mapY+(ty+1)*mapHpx/mapH;
        if(tileType==tt::ROAD) fillRect(e,left,top,std::max(1,right-left),std::max(1,bottom-top),115,122,124,255);
        else fillRect(e,left,top,std::max(1,right-left),std::max(1,bottom-top),45,105,150,255);
    }
    auto marker=[&](float wx,float wy,int size,SDL_Color color){
        const int px=mapX+std::clamp((int)(wx/(mapW*(float)tile)*mapWpx),0,mapWpx-1);
        const int py=mapY+std::clamp((int)(wy/(mapH*(float)tile)*mapHpx),0,mapHpx-1);
        fillRect(e,px-size/2,py-size/2,size,size,color.r,color.g,color.b,color.a);
    };
    if(hasTarget) marker(targetX,targetY,7,SDL_Color{255,80,75,255});
    marker(truckX,truckY,5,SDL_Color{255,235,110,255});
    strokeRect(e,mapX,mapY,mapWpx,mapHpx,190,200,200,255);
    fillRect(e,panelX+14,panelY+153,6,6,255,235,110,255);
    drawText(e,"YOU",panelX+23,panelY+150,SDL_Color{220,228,240,255},11);
    fillRect(e,panelX+70,panelY+153,6,6,255,80,75,255);
    drawText(e,"OBJECTIVE",panelX+79,panelY+150,SDL_Color{220,228,240,255},11);
}
inline int WorldState::nearestActiveJob() const { int best=-1; float bd=1e18f; for(size_t i=0;i<jobs.size();++i){ if(!jobs[i].active)continue; float dx=jobs[i].x-truckX,dy=jobs[i].y-truckY,d=dx*dx+dy*dy; if(d<bd){bd=d;best=(int)i;} } return best; }
inline int WorldState::nearestOpenLot() const { int best=-1; float bd=56.0f*56.0f; for(size_t i=0;i<lots.size();++i){ if(lots[i].owner!=-1)continue; float dx=lots[i].cx-truckX,dy=lots[i].cy-truckY,d=dx*dx+dy*dy; if(d<bd){bd=d;best=(int)i;} } return best; }
inline void WorldState::spawnJob(){
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
        if(best>=0){ t.carType=jobs[best].carType; t.payout=jobs[best].payout; t.jclass=jobs[best].jclass; t.jobType=jobs[best].jtype; startRoute(jobs[best].x,jobs[best].y); jobs.erase(jobs.begin()+best); t.phase=1; if(t.route.empty())t.phase=0; }
        return;
    }
    if(t.ri<(int)t.route.size()){ float tx=(t.route[t.ri].first+0.5f)*tile, ty=(t.route[t.ri].second+0.5f)*tile; float dx=tx-t.x,dy=ty-t.y,dist=std::sqrt(dx*dx+dy*dy);
        if(dist<3.0f){ t.ri++; } else { float ux=dx/dist,uy=dy/dist; t.x+=ux*asp*dt; t.y+=uy*asp*dt; t.facing=(std::abs(ux)>std::abs(uy))?(ux>0?tt::EAST:tt::WEST):(uy>0?tt::SOUTH:tt::NORTH); }
    } else {
        if(t.phase==1 && t.jobType==JT_ROADSIDE){
            if(!rival){ cash+=t.payout; weeklyRevenueAccum+=t.payout; reputation=std::min(100.0f,reputation+0.25f); }
            else rivalCash+=t.payout;
            t.phase=0;
        } else if(t.phase==1){ startRoute(homeX,homeY); t.phase=2; if(t.route.empty())t.phase=0; }
        else {
            if(rival) rivalCash+=t.payout;
            else { cash+=t.payout; weeklyRevenueAccum+=t.payout; tows++; reputation=std::min(100.0f,reputation+0.5f); }
            t.phase=0;
        }
    }
}
inline void WorldState::updateAutoTruck(Engine&, float dt){
    if(!autoT.active){
        if(drivers>=1 && (int)ownedTrucks.size()>=2){
            int bestSlot=-1;
            for(int i=0;i<(int)ownedTrucks.size();++i)
                if(i!=drivenSlot && ownedTrucks[i]==4){bestSlot=i;break;}
            if(bestSlot<0) for(int i=0;i<(int)ownedTrucks.size();++i)
                if(i!=drivenSlot){bestSlot=i;break;}
            if(bestSlot>=0){ autoT.active=true; autoT.model=ownedTrucks[bestSlot]; autoT.x=yardX; autoT.y=yardY; autoT.phase=0; autoT.route.clear(); autoT.ri=0; }
        }
        return;
    }
    if(autoT.phase==0 && autoT.model!=4 && drivers>=1){
        for(int i=0;i<(int)ownedTrucks.size();++i)
            if(i!=drivenSlot && ownedTrucks[i]==4){autoT.model=4;break;}
    }
    stepTruck(autoT,false,dt);
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
inline void WorldState::advanceGoals(Engine& e){
    int mine,rival,open; countLots(mine,rival,open);
    auto met=[&](int gi)->bool{ switch(gi){case 0:return tows>=6;case 1:return (int)ownedTrucks.size()>=2&&drivers>=1;case 2:return mine>=1;case 3:return mine*2>(mine+rival+open);default:return false;} };
    while (goal<4 && met(goal)){ goal++; flashMsg="Goal complete!"; flashTimer=3.0f; }
}
inline void WorldState::advanceClock(Engine& e, float dt){
    if (panel!=P_NONE) return;
    dayAccum += dt;
    while (dayAccum>=kSecondsPerDay){ dayAccum-=kSecondsPerDay; day++;
        if (day>7){ day=1; week++;
            rivalWeeklyTurn();      // AI competitor bids/claims lots and runs its books
            weeklyFinances(e);      // your wages, lot upkeep, loan payments, P&L
            if (week>4){ week=1; month++; if(month>12){month=1;year++;} }
            std::string saveError;
            const bool saved=!optAutosave||saveGame(e,&saveError);
            flashMsg = !optAutosave ? "New week: rival moved, bills paid"
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
inline void WorldState::weeklyFinances(Engine& e){
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
    persistence::SaveGameData data;
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
    if(!SaveStore::writeAtomically(path,serialized,storageError)) return fail(storageError);
    if(!storageError.empty()) e.logInfo("Save completed with cleanup warning: "+storageError);
    if(error) error->clear();
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
    std::string contents, storageError;
    std::filesystem::path loadedFromPath=path;
    if(!SaveStore::readAll(path,contents,storageError)){
        std::error_code existsError;
        const bool preferredExists=std::filesystem::exists(path,existsError);
        if(existsError) return fail("cannot inspect save file: "+existsError.message());
        if(preferredExists) return fail(storageError);

        const std::filesystem::path legacyPath=std::filesystem::path(e.baseDir())/"savegame.tt";
        if(legacyPath==path){
            if(error) error->clear();
            return false;
        }
        const bool legacyExists=std::filesystem::exists(legacyPath,existsError);
        if(existsError) return fail("cannot inspect legacy save: "+existsError.message());
        if(!legacyExists){
            if(error) error->clear();
            return false;
        }
        if(!SaveStore::readAll(legacyPath,contents,storageError))
            return fail("cannot read legacy save: "+storageError);
        loadedFromPath=legacyPath;
    }
    WorldState loaded=*this;
    persistence::SaveGameData decodedSave;
    decodedSave.district=loaded.district; decodedSave.cash=loaded.cash; decodedSave.tows=loaded.tows;
    decodedSave.goal=loaded.goal; decodedSave.drivers=loaded.drivers; decodedSave.drivenSlot=loaded.drivenSlot;
    decodedSave.marketplaceUnlocked=loaded.marketplaceUnlocked; decodedSave.truckX=loaded.truckX;
    decodedSave.truckY=loaded.truckY; decodedSave.facing=loaded.facing; decodedSave.day=loaded.day;
    decodedSave.week=loaded.week; decodedSave.month=loaded.month; decodedSave.year=loaded.year;
    decodedSave.dayAccum=loaded.dayAccum; decodedSave.ownedTrucks=loaded.ownedTrucks;
    for(const auto& lot:loaded.lots) decodedSave.lots.push_back({lot.owner,lot.incomeMult});
    decodedSave.reputation=loaded.reputation; decodedSave.rivalCash=loaded.rivalCash;
    decodedSave.rivalReputation=loaded.rivalRep; decodedSave.weeklyRevenueAccum=loaded.weeklyRevenueAccum;
    decodedSave.lastWeekRevenue=loaded.lastWeekRevenue; decodedSave.lastWeekExpense=loaded.lastWeekExpense;
    for(const auto& loan:loaded.loans) decodedSave.loans.push_back({loan.balance,loan.weeklyPayment});
    decodedSave.isPublic=loaded.isPublic; decodedSave.sharePrice=loaded.sharePrice;
    decodedSave.sharesOut=loaded.sharesOut; decodedSave.youOwnFraction=loaded.youOwnFrac;
    decodedSave.rivalStake=loaded.rivalStake; decodedSave.wonDistrict=loaded.wonDistrict;
    decodedSave.rivalKind=loaded.rivalKind; decodedSave.difficulty=loaded.difficulty;
    decodedSave.optHints=loaded.optHints; decodedSave.optWeather=loaded.optWeather;
    decodedSave.optAutosave=loaded.optAutosave; decodedSave.rngState=loaded.rng.state();
    std::string codecError;
    if(!persistence::decodeSaveGame(contents,decodedSave,mapW,mapH,tile,codecError))
        return fail(codecError);

    loaded.district=decodedSave.district;
    loaded.tiles.assign(simulation::tileCount(loaded.mapW,loaded.mapH),tt::GRASS);
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
        }else e.logInfo("Save map differs from regenerated city; ignoring saved lot ownership");
    }

    loaded.rivalTrucks.clear();
    loaded.autoT=AutoTruck{};
    loaded.carried.clear();
    loaded.jobs.clear();
    for(int i=0;i<3;++i) loaded.spawnJob();
    loaded.loadedOnce=true;
    *this=std::move(loaded);
    if(error) error->clear();
    if(loadedFromPath!=path){
        std::string migrationError;
        if(SaveStore::writeAtomically(path,contents,migrationError))
            e.logInfo("Migrated legacy save to "+path.string());
        else
            e.logInfo("Legacy save loaded, but migration failed: "+migrationError);
    }
    e.logInfo("Game loaded: "+loadedFromPath.string());
    return true;
}
inline void WorldState::update(Engine& e,float dt){
    if (!loadedOnce){
        loadedOnce=true;
        std::string loadError;
        if(loadGame(e,&loadError)){ toastMsg="Welcome back"; toastSub="Continued from your last save - F5 save, F9 load"; toastTimer=4.0f; }
        else if(!loadError.empty()){ toastMsg="Save could not be loaded"; toastSub=loadError; toastTimer=5.0f; }
    }
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
            if(tileAtWorld(nx,truckY)!=tt::WATER)truckX=nx; if(tileAtWorld(truckX,ny)!=tt::WATER)truckY=ny;
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
        // dynamic call volume: weather + rush-hour time-of-day
        float frac=dayAccum/kSecondsPerDay; int hour=(int)(frac*24);
        int target=3 + (weather==W_STORM?3:(weather==W_RAIN?1:0));
        if(hour>=7&&hour<9) target+=2; else if(hour>=16&&hour<18) target+=2; else if(hour>=22||hour<6) target-=1;
        if(target<1) target=1;
        int activeCnt=0; for(auto&j:jobs) if(j.active) activeCnt++;
        spawnTimer-=dt; if(spawnTimer<=0){ spawnTimer=(weather==W_STORM?2.2f:4.0f); if(activeCnt<target) spawnJob(); }
    }
    updateAutoTruck(e,dt); updateRivals(dt);
    advanceClock(e,dt); advanceGoals(e);
    // majority announcement
    if(!announcedMajority){ int mine,rival,open; countLots(mine,rival,open); if((mine+rival+open)>0 && mine*2>(mine+rival+open)){ announcedMajority=true; toastMsg="You control the neighborhood!"; toastSub="Majority of lots are yours - keep pushing to take it all"; toastTimer=6.0f; } }
}
inline void WorldState::handleEvent(Engine& e, SDL_Event& ev){
    if (ev.type==SDL_MOUSEWHEEL && panel==P_NONE){ e.camera.zoom=simulation::adjustCameraZoom(e.camera.zoom,ev.wheel.y); return; }
    if (ev.type!=SDL_KEYDOWN) return;
    SDL_Keycode k=ev.key.keysym.sym;
    if (panel==P_TRUCKS){ const auto& cat=truckCatalog();
        if(k==SDLK_ESCAPE||k==SDLK_b){ panel=P_MARKET; return; }
        if(k==SDLK_UP||k==SDLK_w){ truckSel=(truckSel+(int)cat.size()-1)%(int)cat.size(); return; }
        if(k==SDLK_DOWN||k==SDLK_s){ truckSel=(truckSel+1)%(int)cat.size(); return; }
        if(k>=SDLK_1&&k<=SDLK_6){ truckSel=(int)(k-SDLK_1); return; }
        if(k==SDLK_RETURN||k==SDLK_KP_ENTER){ const auto& tk=cat[truckSel];
            if(cash>=tk.price){ cash-=tk.price; ownedTrucks.push_back(truckSel); drivenSlot=(int)ownedTrucks.size()-1; flashMsg=std::string("Bought ")+tk.name+" - now driving it.  -$"+std::to_string(tk.price); flashTimer=3.5f; advanceGoals(e); }
            else { flashMsg="Not enough cash for that truck"; flashTimer=2.5f; } return; }
        return; }
    if (panel==P_MARKET){ if(k==SDLK_ESCAPE||k==SDLK_m){panel=P_NONE;return;} if(k==SDLK_b){panel=P_TRUCKS;return;} return; }
    if (panel==P_FLEET){
        if(k==SDLK_ESCAPE||k==SDLK_o){ panel=P_NONE; return; }
        if(k==SDLK_LEFTBRACKET){ if(!ownedTrucks.empty())drivenSlot=(drivenSlot+(int)ownedTrucks.size()-1)%(int)ownedTrucks.size(); return; }
        if(k==SDLK_RIGHTBRACKET){ if(!ownedTrucks.empty())drivenSlot=(drivenSlot+1)%(int)ownedTrucks.size(); return; }
        if(k==SDLK_h){ const int HIRE=800; if(cash>=HIRE){cash-=HIRE;drivers++;flashMsg="Hired a driver (-$800). Idle trucks now earn.";flashTimer=3.0f;advanceGoals(e);} else {flashMsg="Not enough cash to hire ($800)";flashTimer=2.5f;} return; }
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
            if(loans.size()>=256){ flashMsg="Maximum number of active loans reached"; flashTimer=2.5f; return; }
            loans.push_back({total,wk}); selectedLoan=loans.size()-1; cash+=lp.principal;
            flashMsg=std::string("Took ")+lp.name+": +$"+std::to_string(lp.principal)+" (pay $"+std::to_string(wk)+"/wk)"; flashTimer=3.2f; return; }
        return;
    }
    if (panel==P_STOCK){
        long val=computeValuation();
        if(k==SDLK_ESCAPE||k==SDLK_t){ panel=P_NONE; return; }
        if(k==SDLK_i && !isPublic && val>=kIpoThreshold){ sharesOut=1000000.0; long raise=(long)(val*0.25); cash+=raise; youOwnFrac=0.75; isPublic=true; sharePrice=(double)val/sharesOut;
            flashMsg=std::string("IPO! Raised $")+std::to_string(raise)+" - you keep 75%"; flashTimer=3.6f; return; }
        if(k==SDLK_r){ long rv=rivalValuation(); long cost=(long)(rv*0.10);
            if(rivalStake<0.999 && cash>=cost){ cash-=cost; rivalStake=std::min(1.0,rivalStake+0.10); flashMsg=std::string("Bought 10% of the rival for $")+std::to_string(cost); flashTimer=3.0f; }
            else if(cash<cost){ flashMsg="Not enough cash to buy rival shares"; flashTimer=2.5f; } return; }
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
            e.changeState(std::make_unique<MenuState>());
            return;
        }
        if(k==SDLK_n && wonDistrict){ panel=P_NONE; startDistrict(district+1); return; }
        return;
    }
    // P_NONE
    if(k==SDLK_ESCAPE||k==SDLK_g){ panel=P_SETTINGS; return; }
    if(k==SDLK_f){ panel=P_BANK; return; }
    if(k==SDLK_t){ panel=P_STOCK; return; }
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
    if(k==SDLK_LEFTBRACKET){ if(!ownedTrucks.empty())drivenSlot=(drivenSlot+(int)ownedTrucks.size()-1)%(int)ownedTrucks.size(); return; }
    if(k==SDLK_RIGHTBRACKET){ if(!ownedTrucks.empty())drivenSlot=(drivenSlot+1)%(int)ownedTrucks.size(); return; }
    if(k==SDLK_EQUALS||k==SDLK_PLUS||k==SDLK_KP_PLUS){ e.camera.zoom=simulation::adjustCameraZoom(e.camera.zoom,1); return; }
    if(k==SDLK_MINUS||k==SDLK_KP_MINUS){ e.camera.zoom=simulation::adjustCameraZoom(e.camera.zoom,-1); return; }
    if(k==SDLK_e){
        const float R=48.0f; int b=nearestActiveJob(); float dYard; { float dx=yardX-truckX,dy=yardY-truckY; dYard=dx*dx+dy*dy; }
        if(!carried.empty() && dYard<R*R){ int sum=0; for(auto&c:carried)sum+=c.payout; int n=(int)carried.size(); cash+=sum; tows+=n; weeklyRevenueAccum+=sum; reputation=std::min(100.0f,reputation+1.0f*n); carried.clear(); for(int i=0;i<n;++i)spawnJob();
            flashMsg=std::string("Delivered ")+std::to_string(n)+" vehicle(s)!  +$"+std::to_string(sum); flashTimer=3.0f;
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
    for(int ty=sY;ty<=eY;++ty)for(int tx=sX;tx<=eX;++tx){ int id=tiles[simulation::tileIndex(tx,ty,mapW)]; if(id<0)continue; SDL_Rect src{(id%e.tilesetCols)*tile,(id/e.tilesetCols)*tile,tile,tile}; int xx,yy; worldToScreen(e,(float)tx*tile,(float)ty*tile,xx,yy); SDL_Rect dst{xx,yy,ds,ds}; SDL_RenderCopy(e.renderer(),e.tileset,&src,&dst); }
    if(e.buildingsTex){ const float fw=3.0f*tile,fh=fw*(float)e.buildingCellH/(float)e.buildingCellW;
        for(auto&b:buildings){ float wx=b.tx*(float)tile,wy=(b.ty+3)*(float)tile-fh; int xx,yy; worldToScreen(e,wx,wy,xx,yy); SDL_Rect src{b.type*e.buildingCellW,0,e.buildingCellW,e.buildingCellH}; SDL_Rect dst{xx,yy,(int)std::round(fw*zoom),(int)std::round(fh*zoom)}; SDL_RenderCopy(e.renderer(),e.buildingsTex,&src,&dst); } }
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
    if(autoT.active){
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
    const int W=e.logicalW();
    fillRect(e,0,0,W,46,16,18,26,220); fillRect(e,0,46,W,2,235,200,90,160);
    std::ostringstream c; c<<"Cash: $"<<cash; drawText(e,c.str().c_str(),16,10,SDL_Color{150,230,150,255},24);
    std::ostringstream fl; fl<<"Fleet: "<<ownedTrucks.size(); drawText(e,fl.str().c_str(),220,10,SDL_Color{200,210,235,255},22);
    std::ostringstream dr; dr<<"Drivers: "<<drivers; drawText(e,dr.str().c_str(),360,10,SDL_Color{200,210,235,255},22);
    { std::ostringstream rp; rp<<"Rep "<<(int)reputation; drawText(e,rp.str().c_str(),500,10,SDL_Color{220,200,150,255},22); }
    drawText(e,goalText()[goal],610,14,SDL_Color{235,205,120,255},18);
    // takeover meter
    int mine,rival,open; countLots(mine,rival,open); int total=mine+rival+open;
    { int bx=16,by=52,bw=W-32,bh=14; fillRect(e,bx,by,bw,bh,30,32,40,220);
      if(total>0){ int wy=(int)std::round(bw*(float)mine/total); int wr=(int)std::round(bw*(float)rival/total);
        fillRect(e,bx,by,wy,bh,70,180,100,255); fillRect(e,bx+wy,by,wr,bh,200,80,80,255); }
      strokeRect(e,bx,by,bw,bh,90,96,110,255);
      std::ostringstream m; m<<"Neighborhood  You "<<(total?mine*100/total:0)<<"%   Rival "<<(total?rival*100/total:0)<<"%   Open "<<open;
      drawText(e,m.str().c_str(),W/2,by-2,SDL_Color{220,228,240,255},14,true); }
    // objective
    std::string obj; int nol=nearestOpenLot(), activeJob=nearestActiveJob();
    float targetX=truckX,targetY=truckY; bool hasTarget=false;
    if(!carried.empty()){
        targetX=yardX; targetY=yardY; hasTarget=true;
        obj="Objective: deliver "+std::to_string((int)carried.size())+"/"+std::to_string(carryCap())
            +" to YARD near "+streetIntersectionAt(yardX,yardY)+" (E)";
    } else if(nol>=0){
        targetX=lots[nol].cx; targetY=lots[nol].cy; hasTarget=true;
        obj="Open contract lot near "+streetIntersectionAt(targetX,targetY)+" - press C to bid";
    } else if(activeJob>=0){
        const Job& job=jobs[activeJob]; targetX=job.x; targetY=job.y; hasTarget=true;
        if(job.jtype==JT_ROADSIDE)
            obj="Service call: "+job.desc+" near "+streetIntersectionAt(job.x,job.y)+" (Roadside Van + E)";
        else
            obj=std::string("Dispatch: ")+classWord(job.jclass)+" "+job.desc+" near "+streetIntersectionAt(job.x,job.y);
    } else obj="No active calls";
    drawMinimap(e,targetX,targetY,hasTarget);
    const int H=e.logicalH();
    fillRect(e,0,H-82,W,22,16,18,26,180); drawText(e,obj.c_str(),16,H-80,SDL_Color{200,220,245,255},18);
    { float frac=dayAccum/kSecondsPerDay; int hh=(int)(frac*24); int mm=(int)((frac*24-hh)*60);
      std::ostringstream dt; dt<<"Dist "<<district<<"   "<<weatherName()<<"  "<<std::setw(2)<<std::setfill('0')<<hh<<":"<<std::setw(2)<<std::setfill('0')<<mm
        <<"   Yr "<<year<<" Mo "<<month<<" Wk "<<week<<" D "<<day;
      SDL_Color wc = weather==W_STORM?SDL_Color{150,170,235,255}:weather==W_RAIN?SDL_Color{150,205,225,255}:SDL_Color{180,210,160,255};
      drawText(e,dt.str().c_str(),W-420,H-52,wc,18); }
    // rival banner (who you're up against this district)
    if(!wonDistrict){ std::ostringstream rb; rb<<"Rival: "<<rivalCompanyName(rivalKind)<<" ("<<rivalKindName(rivalKind)<<")";
      int bw=330,bx=W-bw-8,by=72; fillRect(e,bx,by,bw,18,40,20,24,180); drawText(e,rb.str().c_str(),bx+8,by+1,SDL_Color{240,170,170,255},14); }
    if(optHints){ fillRect(e,0,H-30,W,30,16,18,26,200);
      std::string hint=std::string("WASD drive  E service/tow  Wheel or +/- zoom  C bid  F bank  T stocks  [ ] truck  O fleet")+(marketplaceUnlocked?"  M shop":"")+"  F5/F9  G settings";
      drawText(e,hint.c_str(),16,H-26,SDL_Color{150,160,180,255},16); }
    if(flashTimer>0){ int bw=620,bh=40,x=(W-bw)/2,y=70; fillRect(e,x,y,bw,bh,20,28,22,230); strokeRect(e,x,y,bw,bh,120,220,140,255); drawText(e,flashMsg.c_str(),W/2,y+8,SDL_Color{190,240,190,255},20,true); }
    if(toastTimer>0){ int bw=720,bh=70,x=(W-bw)/2,y=118; fillRect(e,x,y,bw,bh,20,30,24,235); strokeRect(e,x,y,bw,bh,120,230,140,255); drawText(e,toastMsg.c_str(),W/2,y+10,SDL_Color{150,230,150,255},24,true); drawText(e,toastSub.c_str(),W/2,y+42,SDL_Color{220,230,220,255},18,true); }
}
inline void WorldState::drawPanels(Engine& e){
    const int W=e.logicalW(); const auto& cat=truckCatalog();
    if(panel==P_MARKET){
        int pw=760,ph=300,x=(W-pw)/2,y=140; fillRect(e,x,y,pw,ph,18,20,30,242); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawText(e,"VEHICLE MARKETPLACE",W/2,y+16,SDL_Color{235,200,90,255},28,true);
        drawText(e,"Buy      - pay cash up front, you own it outright",x+34,y+66,SDL_Color{230,230,230,255},20);
        drawText(e,"Finance  - deposit plus monthly payments  (Phase 2)",x+34,y+100,SDL_Color{190,196,210,255},20);
        drawText(e,"Lease    - low upfront, fixed monthly cost  (Phase 2)",x+34,y+134,SDL_Color{190,196,210,255},20);
        int bx=x+pw/2-150,by=y+ph-86; fillRect(e,bx,by,300,46,40,120,70,255); strokeRect(e,bx,by,300,46,150,230,160,255);
        drawText(e,"Press B - Browse Trucks for Sale",x+pw/2,by+12,SDL_Color{235,245,235,255},20,true);
        drawText(e,"M to close",W/2,y+ph-24,SDL_Color{200,160,80,255},16,true);
    } else if(panel==P_TRUCKS){
        int pw=820,ph=430,x=(W-pw)/2,y=84; fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
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
        int pw=820,ph=440,x=(W-pw)/2,y=80; fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawText(e,"FLEET & OPERATIONS",W/2,y+12,SDL_Color{235,200,90,255},28,true);
        std::ostringstream w; w<<"Cash: $"<<cash<<"    Drivers: "<<drivers
            <<"    AI dispatch: "<<(autoT.active?"working calls":"needs a driver and spare truck");
        drawText(e,w.str().c_str(),W/2,y+48,SDL_Color{160,230,160,255},20,true);
        int rowY=y+86,rowH=42;
        for(int i=0;i<(int)ownedTrucks.size();++i){ int ry=rowY+i*rowH; const auto& tk=cat[ownedTrucks[i]]; fillRect(e,x+20,ry,pw-40,rowH-6,26,30,40,255);
            if(e.trucksTex){ SDL_Rect src{(ownedTrucks[i]%e.truckCount)*e.truckCellW,0,e.truckCellW,e.truckCellH}; SDL_Rect dst{x+26,ry+2,34,34}; SDL_RenderCopy(e.renderer(),e.trucksTex,&src,&dst); }
            drawText(e,tk.name,x+70,ry+8,SDL_Color{235,235,235,255},19);
            const bool dispatched=autoT.active && i!=drivenSlot && ownedTrucks[i]==autoT.model;
            const char* status=(i==drivenSlot)?"DRIVING (you)":dispatched?"AI DISPATCH":"idle - needs driver";
            SDL_Color sc=(i==drivenSlot)?SDL_Color{150,200,255,255}:dispatched?SDL_Color{150,230,150,255}:SDL_Color{200,170,120,255};
            drawText(e,status,x+pw-360,ry+8,sc,18);
            if(ownedTrucks[i]==4) drawText(e,"Roadside services",x+pw-180,ry+8,SDL_Color{100,235,225,255},16); }
        drawText(e,"H = Hire Driver ($800)    AI trucks earn only by completing calls    O/Esc close",W/2,y+ph-28,SDL_Color{190,200,220,255},18,true);
    } else if(panel==P_BID){
        const auto& opt=bidOptions();
        int pw=720,ph=360,x=(W-pw)/2,y=130; fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
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
        int pw=860,ph=612,x=(W-pw)/2,y=52; fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
        if(!loans.empty()) selectedLoan=std::min(selectedLoan,loans.size()-1);
        drawIcon(e,tt::IC_BANK,x+54,y+42,48);
        drawText(e,"FINANCES & LOANS",W/2,y+14,SDL_Color{235,200,90,255},28,true);
        long debt=0; for(auto&L:loans) debt+=L.balance; long val=computeValuation();
        int mine,rival,open; countLots(mine,rival,open);
        std::ostringstream a; a<<"Cash: $"<<cash<<"     Debt: $"<<debt<<"     Reputation: "<<(int)reputation;
        drawText(e,a.str().c_str(),x+40,y+66,SDL_Color{200,220,245,255},20);
        std::ostringstream b; b<<"Company valuation: $"<<val<<"     Contract lots: "<<mine;
        drawText(e,b.str().c_str(),x+40,y+96,SDL_Color{150,230,180,255},20);
        std::ostringstream pl; int net=lastWeekRevenue-lastWeekExpense;
        pl<<"Last week  -  revenue $"<<lastWeekRevenue<<"   expenses $"<<lastWeekExpense<<"   net "<<(net<0?"-$":"+$")<<std::abs(net);
        drawText(e,pl.str().c_str(),x+40,y+126,net<0?SDL_Color{230,150,150,255}:SDL_Color{190,230,190,255},18);
        drawText(e,"ACTIVE LOANS  (Up/Down to select)",x+40,y+158,SDL_Color{220,225,235,255},19);
        const std::size_t loanStart=loans.empty()?0:(selectedLoan/3)*3;
        if(loans.empty()) drawText(e,"No active loans",x+52,y+190,SDL_Color{180,190,210,255},18);
        for(std::size_t slot=0;slot<3 && loanStart+slot<loans.size();++slot){
            const std::size_t index=loanStart+slot; const auto& loan=loans[index];
            const int ry=y+184+(int)slot*38; const bool selected=index==selectedLoan;
            fillRect(e,x+40,ry,pw-80,32,selected?44:28,selected?54:34,selected?68:44,255);
            if(selected) strokeRect(e,x+40,ry,pw-80,32,235,200,90,255);
            std::ostringstream details; details<<(index+1)<<". Loan balance $"<<loan.balance
                <<"    weekly payment $"<<loan.weeklyPayment;
            drawText(e,details.str().c_str(),x+54,ry+6,selected?SDL_Color{245,235,200,255}:SDL_Color{210,218,230,255},18);
        }
        if(!loans.empty()){
            const auto& selected=loans[selectedLoan];
            std::ostringstream selection; selection<<"Selected loan "<<(selectedLoan+1)
                <<" / "<<loans.size()<<"   Remaining principal: $"<<selected.balance;
            drawText(e,selection.str().c_str(),x+40,y+302,SDL_Color{190,220,245,255},18);
            drawText(e,"R = pay $500 principal    Shift+R = repay as much as possible",x+40,y+330,SDL_Color{150,230,170,255},17);
        }else drawText(e,"Take out a loan below to enable principal repayment.",x+40,y+310,SDL_Color{180,190,210,255},17);
        drawText(e,"Take a loan (cash now, weekly repayments):",x+40,y+366,SDL_Color{220,225,235,255},19);
        const auto& lc=loanCatalog(); int rowY=y+394,rowH=42;
        for(int i=0;i<(int)lc.size();++i){ int ry=rowY+i*rowH; fillRect(e,x+40,ry,pw-80,rowH-8,30,34,46,255); strokeRect(e,x+40,ry,pw-80,rowH-8,90,110,140,255);
            int total=(int)std::round(lc[i].principal*(1.0+lc[i].apr*lc[i].weeks/52.0)); int wk=(total+lc[i].weeks-1)/lc[i].weeks;
            std::ostringstream ln; ln<<(i+1)<<".  "<<lc[i].name<<"   +$"<<lc[i].principal<<"   @ "<<(int)std::round(lc[i].apr*100)<<"% APR";
            drawText(e,ln.str().c_str(),x+56,ry+4,SDL_Color{235,235,235,255},18);
            std::ostringstream rp; rp<<"$"<<wk<<"/wk x "<<lc[i].weeks<<"wk"; drawText(e,rp.str().c_str(),x+pw-250,ry+4,SDL_Color{200,210,230,255},17); }
        drawText(e,"Press 1-3 to borrow    -    F/Esc to close",W/2,y+ph-20,SDL_Color{190,200,220,255},17,true);
    } else if(panel==P_STOCK){
        int pw=820,ph=480,x=(W-pw)/2,y=std::max(8,std::min(70,(e.logicalH()-ph)/2)); fillRect(e,x,y,pw,ph,18,20,30,245); strokeRect(e,x,y,pw,ph,235,200,90,255);
        drawIcon(e,tt::IC_CHART,x+54,y+42,48);
        drawText(e,"STOCK MARKET",W/2,y+14,SDL_Color{235,200,90,255},28,true);
        long val=computeValuation(); long rv=rivalValuation();
        // Your company
        drawText(e,"YOUR COMPANY",x+40,y+66,SDL_Color{150,200,255,255},20);
        std::ostringstream a; a<<"Valuation $"<<val<<"     Status: "<<(isPublic?"PUBLIC":"Private");
        drawText(e,a.str().c_str(),x+40,y+94,SDL_Color{220,228,245,255},19);
        if(isPublic){ std::ostringstream s; s<<"Share price $"<<std::fixed<<std::setprecision(3)<<sharePrice<<"   You own "<<(int)std::round(youOwnFrac*100)<<"%   Shares "<<(long)sharesOut;
            drawText(e,s.str().c_str(),x+40,y+120,SDL_Color{190,230,200,255},18); }
        else { std::ostringstream s; s<<"Go public to raise ~$"<<(long)(val*0.25)<<" (sell 25%).  Needs valuation $"<<kIpoThreshold<<"+";
            drawText(e,s.str().c_str(),x+40,y+120,(val>=kIpoThreshold)?SDL_Color{190,230,200,255}:SDL_Color{210,180,150,255},18); }
        // Rival company
        drawText(e,"RIVAL COMPANY",x+40,y+162,SDL_Color{230,150,150,255},20);
        std::ostringstream b; b<<"Valuation $"<<rv<<"     You own "<<(int)std::round(rivalStake*100)<<"% of the rival";
        drawText(e,b.str().c_str(),x+40,y+190,SDL_Color{230,215,215,255},19);
        std::ostringstream bb; long blk=(long)(rv*0.10); bb<<"Buy a 10% block of the rival for $"<<blk<<".  Reach 50% to take over.";
        drawText(e,bb.str().c_str(),x+40,y+216,SDL_Color{220,210,210,255},18);
        // Actions
        int ay=y+260;
        auto action=[&](const char* key,const char* label,bool enabled){ SDL_Color col=enabled?SDL_Color{235,235,235,255}:SDL_Color{130,135,145,255};
            fillRect(e,x+40,ay,pw-80,38,enabled?34:24,enabled?42:28,enabled?56:34,255); strokeRect(e,x+40,ay,pw-80,38,enabled?150:90,enabled?170:100,enabled?200:120,255);
            drawText(e,(std::string("[")+key+"]  "+label).c_str(),x+56,ay+8,col,19); ay+=46; };
        action("I", isPublic?"Already public":"Go Public (IPO) - sell 25% for cash", !isPublic && val>=kIpoThreshold);
        action("R","Buy 10% of the rival", rivalStake<0.999 && cash>=blk);
        action("U", wonDistrict?"Rival acquired":"Acquire the rival (needs 50% stake)", rivalStake>=0.5 && !wonDistrict);
        action("N", (std::string("Expand to District ")+std::to_string(district+1)).c_str(), wonDistrict);
        if(wonDistrict) drawIcon(e,tt::IC_TROPHY,x+pw-70,y+74,48);
        drawText(e,"I / R / U / N to act   -   T/Esc to close",W/2,y+ph-28,SDL_Color{190,200,220,255},18,true);
    } else if(panel==P_SETTINGS){
        int pw=680,ph=486,x=(W-pw)/2,y=66; fillRect(e,x,y,pw,ph,18,20,30,248); strokeRect(e,x,y,pw,ph,235,200,90,255);
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
        if(wonDistrict) row("N",std::string("Expand to District ")+std::to_string(district+1),"Ready!", on);
        row("S","Save now","F5", SDL_Color{200,210,235,255});
        row("Q","Save & quit to menu","", SDL_Color{230,180,180,255});
        drawText(e,"Press a key to change   -   G/Esc to resume",W/2,y+ph-26,SDL_Color{190,200,220,255},18,true);
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
    drawHud(e); if(panel!=P_NONE){ fillRect(e,0,0,e.logicalW(),e.logicalH(),0,0,0,120); drawPanels(e);} }
