#include "Engine.h"
#include "Version.h"
#include "GameState.h"
#include "UiViewport.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace {
std::string ensureTrailingSlash(std::string path) {
    if (!path.empty()) {
        char last = path.back();
        if (last != '/' && last != '\\') path += "/";
    }
    return path;
}
}

bool Engine::init(const std::string& title, int w, int h) {
    m_width = w;
    m_height = h;

    std::error_code cwdError;
    const std::string cwd = std::filesystem::current_path(cwdError).string();

    char* basePath = SDL_GetBasePath();
    if (basePath) {
        m_basePath = ensureTrailingSlash(basePath);
        SDL_free(basePath);
    }
    if (!m_basePath.empty()) {
        m_log.open(m_basePath + "towtycoon.log", std::ios::out | std::ios::trunc);
    }

    logMessage(std::string("Startup TowTycoon v") + TOWTYCOON_VERSION);
    logMessage(std::string("CWD: ") + (cwd.empty() ? "<unknown>" : cwd));
    logMessage(std::string("EXE DIR: ") + (m_basePath.empty() ? "<unknown>" : m_basePath));
    if (!m_basePath.empty()) {
        std::filesystem::path root = std::filesystem::path(m_basePath) / "assets";
        m_assetRoot = ensureTrailingSlash(root.make_preferred().string());
        logMessage(std::string("ASSET_ROOT: ") + m_assetRoot);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        logError("SDL_Init failed", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TowTycoon Error", "SDL_Init failed. See towtycoon.log.", nullptr);
        return false;
    }
    char* prefPath = SDL_GetPrefPath("TowTycoon", "TowTycoon");
    if (prefPath) {
        m_savePath = ensureTrailingSlash(prefPath);
        SDL_free(prefPath);
        logMessage(std::string("SAVE_DIR: ") + m_savePath);
    } else {
        logError("SDL_GetPrefPath failed", SDL_GetError());
    }
    if (m_basePath.empty()) {
        char* sdlBasePath = SDL_GetBasePath();
        if (sdlBasePath) {
            m_basePath = ensureTrailingSlash(sdlBasePath);
            SDL_free(sdlBasePath);
            if (m_log.is_open()) m_log.close();
            m_log.open(m_basePath + "towtycoon.log", std::ios::out | std::ios::trunc);
            logMessage(std::string("EXE DIR (SDL): ") + m_basePath);
            std::filesystem::path root = std::filesystem::path(m_basePath) / "assets";
            m_assetRoot = ensureTrailingSlash(root.make_preferred().string());
            logMessage(std::string("ASSET_ROOT: ") + m_assetRoot);
        }
    }
    if (m_basePath.empty()) {
        logError("SDL_GetBasePath failed", "cannot determine executable directory");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TowTycoon Error",
            "Cannot locate the executable directory required to load assets.", nullptr);
        return false;
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        logError("IMG_Init failed", IMG_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TowTycoon Error", "IMG_Init failed. See towtycoon.log.", nullptr);
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    m_window = SDL_CreateWindow(title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!m_window) {
        logError("SDL_CreateWindow failed", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TowTycoon Error", "SDL_CreateWindow failed. See towtycoon.log.", nullptr);
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_renderer) {
        logError("SDL_CreateRenderer failed", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TowTycoon Error", "SDL_CreateRenderer failed. See towtycoon.log.", m_window);
        return false;
    }
    // Enable alpha blending so translucent HUD panels and overlays composite.
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_GetWindowSize(m_window, &m_windowW, &m_windowH);
    SDL_GetRendererOutputSize(m_renderer, &m_renderW, &m_renderH);
    updateViewport();
    {
        std::ostringstream oss;
        oss << "Sizes: window=" << m_windowW << "x" << m_windowH
            << " render=" << m_renderW << "x" << m_renderH
            << " scale=" << m_scaleX << "," << m_scaleY;
        logMessage(oss.str());
    }

    std::filesystem::path tilesPath = assetPath("tiles.png");
    tilesPath = tilesPath.make_preferred();
    logMessage(std::string("Load texture: ") + tilesPath.string());
    if (!std::filesystem::exists(tilesPath)) {
        logError("Missing asset", tilesPath.string());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TowTycoon Error", ("Missing asset: " + tilesPath.string()).c_str(), m_window);
        return false;
    }
    tileset = IMG_LoadTexture(m_renderer, tilesPath.string().c_str());
    if (!tileset) {
        logError("IMG_LoadTexture failed", IMG_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TowTycoon Error", "Failed to load tiles.png. See towtycoon.log.", m_window);
        return false;
    }

    int tw, th;
    SDL_QueryTexture(tileset, nullptr, nullptr, &tw, &th);
    tilesetCols = tw / tileSize;
    {
        std::ostringstream oss;
        oss << "Tileset: " << tw << "x" << th << " cols=" << tilesetCols;
        logMessage(oss.str());
    }

    // Optional title art for the menu. Not fatal if absent.
    menuLogo = loadTexture("logo.png", &menuLogoW, &menuLogoH);
    if (menuLogo) {
        std::ostringstream oss;
        oss << "Logo loaded: " << menuLogoW << "x" << menuLogoH;
        logMessage(oss.str());
    } else {
        logMessage("Logo not loaded (assets/logo.png missing) - menu will draw text title");
    }

    // Optional gameplay art. All null-checked before use.
    trucksTex = loadTexture("trucks.png");
    carsTex = loadTexture("cars.png");
    buildingsTex = loadTexture("buildings.png");
    rivalTrucksTex = loadTexture("rival_trucks.png");
    lotTex = loadTexture("lot_marker.png", &lotW, &lotH);
    iconsTex = loadTexture("icons.png");
    menuBg = loadTexture("menu_bg.png", &menuBgW, &menuBgH);
    openttdGuiTex = loadTexture("openttd/gui04.png");
    logMessage(std::string("Gameplay art: trucks=") + (trucksTex ? "ok" : "missing")
               + " cars=" + (carsTex ? "ok" : "missing")
               + " buildings=" + (buildingsTex ? "ok" : "missing")
               + " rival=" + (rivalTrucksTex ? "ok" : "missing")
               + " lot=" + (lotTex ? "ok" : "missing")
               + " icons=" + (iconsTex ? "ok" : "missing")
               + " openttd_gui=" + (openttdGuiTex ? "ok" : "missing")
               + " menu_bg=" + (menuBg ? "ok" : "missing"));

    m_lastTicks = SDL_GetTicks();
    return true;
}

SDL_Texture* Engine::loadTexture(const std::string& relative, int* outW, int* outH) {
    std::filesystem::path p = std::filesystem::path(assetPath(relative)).make_preferred();
    if (!std::filesystem::exists(p)) {
        logMessage(std::string("Texture missing: ") + p.string());
        return nullptr;
    }
    SDL_Texture* tex = IMG_LoadTexture(m_renderer, p.string().c_str());
    if (!tex) {
        logError("IMG_LoadTexture failed", IMG_GetError());
        return nullptr;
    }
    if (outW || outH) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        if (outW) *outW = tw;
        if (outH) *outH = th;
    }
    return tex;
}

void Engine::updateViewport() {
    const UiViewport viewport = makeUiViewport(m_renderW, m_renderH);
    m_logicalW = viewport.width;
    m_logicalH = viewport.height;
    if (SDL_RenderSetLogicalSize(m_renderer, m_logicalW, m_logicalH) != 0) {
        logError("SDL_RenderSetLogicalSize failed", SDL_GetError());
    }

    if (m_renderW > 0 && m_renderH > 0) {
        const float scale = std::min(
            static_cast<float>(m_renderW) / static_cast<float>(m_logicalW),
            static_cast<float>(m_renderH) / static_cast<float>(m_logicalH));
        m_scaleX = scale;
        m_scaleY = scale;
    }
}

std::string Engine::assetPath(const std::string& relative) const {
    if (m_assetRoot.empty()) return {};
    std::filesystem::path p = std::filesystem::path(m_assetRoot) / relative;
    return p.make_preferred().string();
}

void Engine::changeState(std::unique_ptr<GameState> next) {
    const char* from = m_state ? m_state->name() : "None";
    const char* to = next ? next->name() : "None";
    logMessage(std::string("State change: ") + from + " -> " + to);
    m_state = std::move(next);
}

void Engine::run() {
    SDL_Event e;
    while (!m_quit) {
        // Frame time only schedules fixed simulation steps; world logic never
        // consumes variable render-frame deltas.
        unsigned int now = SDL_GetTicks();
        const double elapsed = static_cast<double>(now - m_lastTicks) / 1000.0;
        m_lastTicks = now;
        const unsigned simulationSteps = m_simulationClock.advance(elapsed);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { requestQuit(); if(m_quit)break; continue; }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                float x = 0, y = 0;
                SDL_RenderWindowToLogical(m_renderer, e.button.x, e.button.y, &x, &y);
                m_logicalMouseX = static_cast<int>(std::round(x));
                m_logicalMouseY = static_cast<int>(std::round(y));
            }
            if (m_state) m_state->handleEvent(*this, e);
            if(m_quit)break;
        }

        int winW = 0, winH = 0;
        int renW = 0, renH = 0;
        SDL_GetWindowSize(m_window, &winW, &winH);
        SDL_GetRendererOutputSize(m_renderer, &renW, &renH);
        if (winW != m_windowW || winH != m_windowH || renW != m_renderW || renH != m_renderH) {
            m_windowW = winW;
            m_windowH = winH;
            m_renderW = renW;
            m_renderH = renH;
            updateViewport();
            std::ostringstream oss;
            oss << "Sizes: window=" << m_windowW << "x" << m_windowH
                << " render=" << m_renderW << "x" << m_renderH
                << " scale=" << m_scaleX << "," << m_scaleY;
            logMessage(oss.str());
        }

        SDL_GetMouseState(&m_mouseX, &m_mouseY);
        if (m_renderW > 0 && m_renderH > 0) {
            float logicalX = 0.0f, logicalY = 0.0f;
            SDL_RenderWindowToLogical(m_renderer, m_mouseX, m_mouseY, &logicalX, &logicalY);
            m_logicalMouseX = static_cast<int>(std::round(logicalX));
            m_logicalMouseY = static_cast<int>(std::round(logicalY));
        } else {
            m_logicalMouseX = m_mouseX;
            m_logicalMouseY = m_mouseY;
        }

        SDL_SetRenderDrawColor(m_renderer, 20, 20, 30, 255);
        SDL_RenderClear(m_renderer);

        for (unsigned step = 0; step < simulationSteps && !m_quit; ++step) {
            m_dt = static_cast<float>(simulation::FixedStepClock::stepSeconds);
            if (m_state) m_state->update(*this, m_dt);
        }

        if (m_state) m_state->render(*this);

        SDL_RenderPresent(m_renderer);
    }
}

void Engine::shutdown() {
    if (openttdGuiTex) SDL_DestroyTexture(openttdGuiTex);
    if (menuBg) SDL_DestroyTexture(menuBg);
    if (iconsTex) SDL_DestroyTexture(iconsTex);
    if (lotTex) SDL_DestroyTexture(lotTex);
    if (rivalTrucksTex) SDL_DestroyTexture(rivalTrucksTex);
    if (buildingsTex) SDL_DestroyTexture(buildingsTex);
    if (carsTex) SDL_DestroyTexture(carsTex);
    if (trucksTex) SDL_DestroyTexture(trucksTex);
    if (menuLogo) SDL_DestroyTexture(menuLogo);
    SDL_DestroyTexture(tileset);
    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    IMG_Quit();
    SDL_Quit();
    if (m_log.is_open()) m_log.close();
}

void Engine::logMessage(const std::string& msg) {
    std::cout << msg << std::endl;
    if (m_log.is_open()) {
        m_log << msg << std::endl;
        m_log.flush();
    }
}

void Engine::logError(const std::string& context, const std::string& detail) {
    std::ostringstream oss;
    oss << context << ": " << detail;
    logMessage(oss.str());
}

void Engine::logInfo(const std::string& msg) {
    logMessage(msg);
}

void Engine::toggleFullscreen() {
    if (!m_window) return;
    const Uint32 flags = isFullscreen() ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (SDL_SetWindowFullscreen(m_window, flags) != 0)
        logError("SDL_SetWindowFullscreen failed", SDL_GetError());
    else
        logMessage(flags ? "Fullscreen on" : "Fullscreen off");
}

bool Engine::isFullscreen() const {
    return m_window && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void Engine::requestQuit() {
    if (m_state && !m_state->prepareQuit(*this)) return;
    logMessage("Quit requested");
    m_quit = true;
}

