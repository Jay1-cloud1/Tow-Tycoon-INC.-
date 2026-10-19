#define SDL_MAIN_HANDLED
#include "States.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

struct WorldRegression {
    static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
    static void key(WorldState& w,Engine& e,SDL_Keycode k){SDL_Event ev{};ev.type=SDL_KEYDOWN;ev.key.keysym.sym=k;w.handleEvent(e,ev);}
    static std::string snapshot(WorldState& w,Engine& e){std::string error,text;bool saved=w.saveGame(e,&error);require(saved,error.c_str());require(SaveStore::readAll(std::filesystem::path(e.saveDir())/"savegame.tt",text,error),"read snapshot");return text;}
    static void credit(Engine& e){
        WorldState w;w.loadedOnce=true;w.optAutosave=false;w.panel=WorldState::P_BANK;
        for(int i=0;i<5;++i)key(w,e,SDLK_3);
        require(w.loans.empty()&&w.cash==500,"starter loan stacking must be blocked");
        w.cash=100000;w.tows=6;w.panel=WorldState::P_NONE;
        for(int i=0;i<6;++i){w.day=7;w.week=4;w.dayAccum=11.99f;w.advanceClock(e,.02f);require(w.positiveMonths==i+1,"month-end history boundary");}
        w.lastWeekRevenue=2000;w.lastWeekExpense=200;w.panel=WorldState::P_BANK;
        key(w,e,SDLK_3);require(w.loans.size()==1,"eligible affordable loan");int cash=w.cash;
        key(w,e,SDLK_3);require(w.loans.size()==1&&w.cash==cash,"second loan must be blocked");
        require(simulation::loanRejection(100,6,6,0,211,106)!=nullptr,"affordability lower boundary");
        require(simulation::loanRejection(100,6,6,0,212,106)==nullptr,"affordability inclusive boundary");
        w.cash=0;w.observeCash();require(w.positiveMonths==0&&!w.monthPositive,"zero cash resets credit");
        w.cash=100;simulation::finishCreditMonth(w.cash,w.positiveMonths,w.monthPositive);require(w.positiveMonths==0&&w.monthPositive,"partial month cannot qualify");
    }
    static void fleet(Engine& e){
        WorldState w;w.loadedOnce=true;w.ownedTrucks={0,0,0};w.drivers=2;
        w.updateAutoTruck(e,1.f/60);
        require(w.autoTrucks.size()==2,"all staffed spare trucks dispatch");
        require(w.autoTrucks[0].slot!=w.autoTrucks[1].slot,"identical models need distinct slots");
        for(auto& t:w.autoTrucks)t.phase=1;
        int slot=w.drivenSlot;w.switchTruck(1);require(w.drivenSlot==slot,"busy vehicles cannot also be player driven");
        w.autoTrucks[0].phase=0;w.switchTruck(1);require(w.drivenSlot==1&&w.autoTrucks.size()==1,"idle assignment released before player switch");
        w.carried.push_back({125,0,0});slot=w.drivenSlot;w.switchTruck(1);require(w.drivenSlot==slot,"cannot switch with cargo");
        WorldState van;van.loadedOnce=true;van.ownedTrucks={0,0,4};van.drivers=1;van.updateAutoTruck(e,1.f/60);
        require(van.autoTrucks.size()==1&&van.autoTrucks[0].slot==2,"available roadside van retains dispatch priority");
        WorldState bad;bad.cash=10000;bad.panel=WorldState::P_FLEET;key(bad,e,SDLK_h);require(bad.drivers==0,"cannot hire without spare truck");
    }
    static void bounds(Engine& e){
        WorldState w;w.loadedOnce=true;w.cash=100000;w.ownedTrucks.assign(64,0);w.panel=WorldState::P_TRUCKS;
        key(w,e,SDLK_RETURN);require(w.ownedTrucks.size()==64,"fleet purchase respects save limit");snapshot(w,e);
        w.panel=WorldState::P_NONE;w.startDistrict(8);w.startDistrict(9);require(w.district==8,"district progression respects save limit");snapshot(w,e);
        w.carried.push_back({125,0,0});w.startDistrict(2);require(w.district==8&&!w.carried.empty(),"district change cannot discard cargo");
        WorldState expand;expand.loadedOnce=true;expand.optAutosave=false;expand.ownedTrucks={0,0};expand.drivers=1;expand.wonDistrict=true;
        WorldState::AutoTruck trip;trip.active=true;trip.slot=1;trip.model=0;trip.x=expand.yardX;trip.y=expand.yardY;trip.phase=2;trip.payout=125;expand.autoTrucks.push_back(trip);
        expand.startDistrict(2);require(expand.district==1&&expand.pendingDistrict==2,"expansion queues in-flight work");
        const int expansionCash=expand.cash;expand.update(e,1.f/60);require(expand.district==2&&expand.pendingDistrict==0&&expand.cash==expansionCash+125,"expansion waits for delivery payment");
        w.rivalCash=-50000;w.carried.clear();w.tows=6;w.panel=WorldState::P_STOCK;int before=w.cash;key(w,e,SDLK_r);require(w.cash<=before&&w.rivalValuation()>0,"rival shares cannot pay player");
    }
    static void tow(Engine& e){
        WorldState w;w.loadedOnce=true;w.jobs.clear();
        for(int i=0;i<6;++i){w.jobs.push_back({w.truckX,w.truckY,125,true,0,0,0,0,0,"regression tow"});key(w,e,SDLK_e);w.truckX=w.yardX;w.truckY=w.yardY;key(w,e,SDLK_e);w.jobs.clear();}
        require(w.tows==6&&w.cash==1250&&w.marketplaceUnlocked,"six tows unlock marketplace exactly once");
    }
    static void v011Features(Engine& e){
        WorldState w;w.loadedOnce=true;
        require(w.mapW==256&&w.mapH==192,"10x expanded terrain dimensions");
        require(w.cities.size()==6,"six regional named cities");
        require(!w.cityNameAtWorld(w.truckX,w.truckY).empty(),"city name resolution at player position");

        w.panel=WorldState::P_STOCK;
        w.isPublic=false;
        int cashBefore=w.cash;
        key(w,e,SDLK_s);
        require(w.cash==cashBefore,"cannot sell shares when private");

        w.isPublic=true;
        w.sharesOut=1000000.0;
        w.sharePrice=2.0;
        w.youOwnFrac=0.75;
        key(w,e,SDLK_s);
        require(std::abs(w.youOwnFrac-0.70)<0.001&&w.cash>cashBefore,"selling shares increases cash and decreases ownership");

        int cashAfterSell=w.cash;
        key(w,e,SDLK_b);
        require(std::abs(w.youOwnFrac-0.75)<0.001&&w.cash<cashAfterSell,"buying back shares decreases cash and increases ownership");

        w.panel=WorldState::P_NONE;
        require(!w.devMode,"devMode off initially");
        key(w,e,SDLK_F1);
        require(w.devMode,"F1 toggles devMode on");

        w.devBuildRoad(10,10);
        require(w.tiles[simulation::tileIndex(10, 10, w.mapW)]==tt::ROAD,"devBuildRoad creates road tile");
        size_t bCount=w.buildings.size();
        w.devPlaceBuilding(11,10,1);
        require(w.buildings.size()==bCount+1,"devPlaceBuilding creates building");
        w.devBulldoze(10,10);
        require(w.tiles[simulation::tileIndex(10, 10, w.mapW)]==tt::GRASS,"devBulldoze clears tile to grass");
        w.devPlacePark(12,12,4,4);
        require(w.tiles[simulation::tileIndex(12, 12, w.mapW)]==tt::SIDEWALK,"devPlacePark creates park perimeter");
        w.devPlantTrees(15,15);
        require(w.tiles[simulation::tileIndex(15, 15, w.mapW)]==tt::TREE,"devPlantTrees creates tree tile");
        w.devPlaceWater(16,16);
        require(w.tiles[simulation::tileIndex(16, 16, w.mapW)]==tt::WATER,"devPlaceWater creates water tile");
        w.devPlaceSand(17,17);
        require(w.tiles[simulation::tileIndex(17, 17, w.mapW)]==tt::SAND,"devPlaceSand creates sand tile");
        w.devFoundTown(50,50,"Metropolis");
        require(w.cities.size()==7,"devFoundTown adds city");

        size_t jc=w.jobs.size();
        w.devSpawnJob();
        require(w.jobs.size()==jc+1,"devSpawnJob creates active job");

        w.devPlaceWater(20,20);
        w.devBuildRoad(20,20);
        require(w.tiles[simulation::tileIndex(20, 20, w.mapW)]==tt::ROAD,"road placed over water");
        w.devPlaceWater(20,19);
        w.devPlaceWater(20,21);
        require(w.isBridge(20,20),"isBridge identifies road crossing water");

        w.tiles[simulation::tileIndex(25, 25, w.mapW)]=tt::WATER;
        w.tiles[simulation::tileIndex(26, 25, w.mapW)]=tt::GRASS;
        w.devHiggsErode(26,25);
        require(w.tiles[simulation::tileIndex(26, 25, w.mapW)]==tt::SAND,"Higgs relaxation creates beach sand buffer along water");

        key(w,e,SDLK_F1);
        require(!w.devMode,"F1 toggles devMode off");

        // OpenTTD Town Directory & Council relations
        key(w,e,SDLK_h);
        require(w.panel==WorldState::P_TOWNS,"H key opens Town Directory panel");
        require(w.townSel==0,"default selected town is first index");
        key(w,e,SDLK_DOWN);
        require(w.townSel==1,"DOWN arrow navigates to next town");
        key(w,e,SDLK_UP);
        require(w.townSel==0,"UP arrow navigates back to first town");

        w.cash=10000;
        int popBefore=w.cities[0].population;
        int ratingBefore=w.cities[0].councilRating;
        key(w,e,SDLK_b);
        require(w.cities[0].population==popBefore+150,"Fund buildings increases town population");
        require(w.cities[0].councilRating==ratingBefore+80,"Fund buildings raises council standing");
        require(w.cash==7500,"Fund buildings deducts $2500");
        require(!w.newsHeadline.empty(),"Fund buildings posts breaking news headline");

        key(w,e,SDLK_r);
        require(w.cities[0].councilRating==ratingBefore+180,"Sponsor road works raises council standing +100");
        require(w.cash==6300,"Sponsor road works deducts $1200");

        key(w,e,SDLK_h);
        require(w.panel==WorldState::P_NONE,"H key closes Town Directory panel");

        // OpenTTD Detailed Company Performance Rating matrix
        key(w,e,SDLK_p);
        require(w.panel==WorldState::P_PERF,"P key opens Performance Rating panel");
        key(w,e,SDLK_p);
        require(w.panel==WorldState::P_NONE,"P key closes Performance Rating panel");
    }
    static void legacy(Engine& e){
        WorldState city;persistence::SaveGameData data;
        data.cash=4242;data.tows=12;data.day=4;data.week=2;data.dayAccum=1;data.ownedTrucks={0,3};data.drivers=1;data.loans={{2500,90}};
        data.reputation=77;data.marketplaceUnlocked=true;data.rngState=998877665544332211ULL;
        for(const auto& lot:city.lots)data.lots.push_back({lot.owner,lot.incomeMult});
        std::string text,error;require(persistence::encodeSaveGame(data,80,60,16,text,error),"encode legacy fixture base");
        const std::string rng=std::to_string(data.rngState);auto last=text.find(rng);require(last!=std::string::npos,"find legacy RNG field");
        text=text.substr(0,last+rng.size())+"\n";text[0]='6';
        require(SaveStore::writeAtomically(std::filesystem::path(e.saveDir())/"savegame.tt",text,error),"write v6 fixture");
        WorldState upgraded;require(upgraded.loadGame(e,&error),error.c_str());
        require(upgraded.cash==4242&&upgraded.ownedTrucks.size()==2&&upgraded.loans.size()==1&&upgraded.loans[0].balance==2500,"legacy finances and fleet retained");
        require(upgraded.positiveMonths==0&&!upgraded.monthPositive&&upgraded.jobs.size()==3,"legacy history starts without inventing qualification");
        auto current=snapshot(upgraded,e);require(current[0]=='7',"legacy save upgrades to v7");
        std::string backup;require(SaveStore::readAll(std::filesystem::path(e.saveDir())/"savegame.tt.bak",backup,error)&&backup==text,"original v6 retained in backup");
    }
    static void resume(Engine& e){
        WorldState w;w.loadedOnce=true;w.optAutosave=false;w.ownedTrucks={0,0,0};w.drivers=2;w.cash=10000;
        w.updateAutoTruck(e,1.f/60);w.carried.push_back({175,0,0});w.weather=1;w.weatherTimer=17;w.spawnTimer=2;w.positiveMonths=4;
        const auto initial=snapshot(w,e);auto rng=w.rng.state();
        WorldState restored;std::string error;require(restored.loadGame(e,&error),error.c_str());
        require(restored.carried.size()==1&&restored.carried[0].payout==175,"cargo survives reload");
        require(restored.autoTrucks.size()==2&&restored.rng.state()==rng,"trips and RNG survive reload");
        require(snapshot(restored,e)==initial,"save-load-save is lossless");
        for(int i=0;i<1200;++i){w.update(e,1.f/60);restored.update(e,1.f/60);}
        require(snapshot(w,e)==snapshot(restored,e),"continued simulation matches uninterrupted simulation");
        restored.truckX=restored.yardX;restored.truckY=restored.yardY;int before=restored.cash;
        key(restored,e,SDLK_e);require(restored.cash==before+175,"restored cargo pays on delivery");
        key(restored,e,SDLK_e);require(restored.cash==before+175,"cargo cannot pay twice");
        const auto good=snapshot(restored,e);const auto path=std::filesystem::path(e.saveDir())/"savegame.tt";
        require(SaveStore::writeAtomically(path,"broken",error),"write damaged primary fixture");
        require(restored.loadGame(e,&error)&&restored.preserveBackup,"recover backup after corrupt primary");
        require(snapshot(restored,e)==good,"recovered backup restores original snapshot");
        std::string backup;require(SaveStore::readAll(path.string()+".bak",backup,error)&&backup==good,"first recovered write preserves good backup");
        std::filesystem::remove(path);require(restored.loadGame(e,&error),"recover backup when primary is missing");
    }
    static void protection(Engine& e,const std::filesystem::path& root){
        e.m_savePath=(root/"corrupt").string()+"/";std::string error,text;
        const auto file=std::filesystem::path(e.saveDir())/"savegame.tt";
        require(SaveStore::writeAtomically(file,"unreadable",error),"create unreadable fixture");
        WorldState w;w.update(e,1.f/60);require(w.saveProtected&&w.panel==WorldState::P_SETTINGS,"unreadable save pauses and protects");
        require(!w.saveGame(e,&error)&&w.prepareQuit(e),"unreadable save can exit without overwriting");
        require(SaveStore::readAll(file,text,error)&&text=="unreadable","original corrupt file preserved");
        WorldState early;key(early,e,SDLK_F5);
        require(early.saveProtected,"first input initializes before allowing manual save");
        require(SaveStore::readAll(file,text,error)&&text=="unreadable","early F5 cannot overwrite existing save");
        persistence::SaveGameData validData;validData.truckX=w.truckX;validData.truckY=w.truckY;
        for(const auto& lot:w.lots)validData.lots.push_back({lot.owner,lot.incomeMult});
        std::string validText;require(persistence::encodeSaveGame(validData,80,60,16,validText,error),"encode recovery fixture");
        {std::ofstream restored(file);restored<<validText;}
        key(w,e,SDLK_F9);require(!w.saveProtected,"restored save can be retried while paused");
        {std::ofstream damaged(file);damaged<<"unreadable";}
        WorldState moved;moved.update(e,0);require(moved.saveProtected,"protect second damaged save");
        std::filesystem::rename(file,file.string()+".corrupt");
        require(moved.saveGame(e,&error),"moving damaged save aside explicitly permits a new save");
        require(std::filesystem::exists(file.string()+".corrupt"),"moved damaged original remains available");
        const auto blocker=root/"blocker";{std::ofstream out(blocker);out<<"file";}
        e.m_savePath=blocker.string()+"/";WorldState valid;valid.loadedOnce=true;
        require(!valid.prepareQuit(e)&&valid.panel==WorldState::P_SETTINGS,"write failure cancels quit");
        e.m_savePath=(root/"close").string()+"/";
        valid.carried.push_back({125,0,0});require(valid.prepareQuit(e),"window close saves cargo");
        WorldState continued;require(continued.loadGame(e,&error)&&continued.carried.size()==1,"window-close save resumes");
    }
    static void menuAndScreens(Engine& e){
        MenuState menu;auto buttons=menu.buildButtons(e);e.m_logicalMouseX=buttons[MenuState::B_SETTINGS].rect.x+8;e.m_logicalMouseY=buttons[MenuState::B_SETTINGS].rect.y+8;
        SDL_Event ev{};ev.type=SDL_MOUSEBUTTONDOWN;ev.button.button=SDL_BUTTON_LEFT;menu.handleEvent(e,ev);
        require(menu.settingsOpen,"menu Settings opens");ev.type=SDL_KEYDOWN;ev.key.keysym.sym=SDLK_d;int before=e.preferences.difficulty;menu.handleEvent(e,ev);
        require(e.preferences.difficulty==(before+1)%3&&e.preferences.overrideSaved,"menu Settings changes preferences");
        WorldState world;world.update(e,1.f/60);require(world.difficulty==e.preferences.difficulty&&!e.preferences.overrideSaved,"preferences apply after load");
        for(int h:{560,591,720}){
            e.m_logicalH=h;SDL_SetWindowSize(e.m_window,1280,h);SDL_RenderSetLogicalSize(e.m_renderer,1280,h);
            for(int p=WorldState::P_MARKET;p<=WorldState::P_PERF;++p){
                world.panel=(WorldState::Panel)p;world.loans={{1000,50},{2000,100},{500,25}};world.render(e);SDL_RenderPresent(e.m_renderer);
                if(const char* dir=std::getenv("TT_SCREENSHOT_DIR")){
                    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,1280,h,32,SDL_PIXELFORMAT_RGBA32);
                    require(SDL_RenderReadPixels(e.m_renderer,nullptr,surface->format->format,surface->pixels,surface->pitch)==0,"read rendered screen");
                    const auto file=std::filesystem::path(dir)/("panel-"+std::to_string(p)+"-"+std::to_string(h)+".png");
                    require(IMG_SavePNG(surface,file.string().c_str())==0,"save render fixture");SDL_FreeSurface(surface);
                }
            }
            menu.render(e);SDL_RenderPresent(e.m_renderer);
        }
    }
    static int run(){
        SDL_SetHint(SDL_HINT_VIDEODRIVER,"dummy");require(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");require(IMG_Init(IMG_INIT_PNG)&IMG_INIT_PNG,"PNG init");
        Engine e;auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        const auto root=std::filesystem::temp_directory_path()/("TowTycoon-v011-"+std::to_string(stamp));
        e.m_savePath=(root/"save").string()+"/";e.m_basePath=(root/"legacy").string()+"/";e.m_assetRoot=std::string(TT_SOURCE_DIR)+"/Assets/";
        e.m_window=SDL_CreateWindow("regression",0,0,1280,720,SDL_WINDOW_HIDDEN);
        e.m_renderer=SDL_CreateRenderer(e.m_window,-1,SDL_RENDERER_SOFTWARE);require(e.m_renderer,"software renderer");SDL_SetRenderDrawBlendMode(e.m_renderer,SDL_BLENDMODE_BLEND);
        e.tileset=e.loadTexture("tiles.png");require(e.tileset,"tiles texture");int w,h;SDL_QueryTexture(e.tileset,nullptr,nullptr,&w,&h);e.tilesetCols=w/e.tileSize;
        e.trucksTex=e.loadTexture("trucks.png");e.carsTex=e.loadTexture("cars.png");e.buildingsTex=e.loadTexture("buildings.png");
        e.rivalTrucksTex=e.loadTexture("rival_trucks.png");e.lotTex=e.loadTexture("lot_marker.png",&e.lotW,&e.lotH);e.iconsTex=e.loadTexture("icons.png");
        e.openttdGuiTex=e.loadTexture("openttd/gui04.png");
        credit(e);fleet(e);bounds(e);tow(e);v011Features(e);legacy(e);resume(e);protection(e,root);menuAndScreens(e);
        e.shutdown();std::filesystem::remove_all(root);std::cout<<"v0.1.1 gameplay regressions passed\n";return 0;
    }
};
int main(){try{return WorldRegression::run();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}




