#pragma once
#include <SDL.h>
#include <SDL_image.h>
#include "simulation/FixedStepClock.h"
#include <fstream>
#include <memory>
#include <string>

class GameState;

struct Camera {
    float x = 0;
    float y = 0;
    float zoom = 2.0f;
};

class Engine {
    friend struct WorldRegression;
public:
    bool init(const std::string& title, int w, int h);
    void run();
    void shutdown();

    void changeState(std::unique_ptr<GameState> next);
    void logInfo(const std::string& msg);
    void requestQuit();
    void toggleFullscreen();
    bool isFullscreen() const;

    SDL_Renderer* renderer() { return m_renderer; }
    int screenW() const { return m_width; }
    int screenH() const { return m_height; }
    std::string assetPath(const std::string& relative) const;
    int logicalW() const { return m_logicalW; }
    int logicalH() const { return m_logicalH; }
    int windowW() const { return m_windowW; }
    int windowH() const { return m_windowH; }
    int renderW() const { return m_renderW; }
    int renderH() const { return m_renderH; }
    float scaleX() const { return m_scaleX; }
    float scaleY() const { return m_scaleY; }
    int mouseX() const { return m_mouseX; }
    int mouseY() const { return m_mouseY; }
    int logicalMouseX() const { return m_logicalMouseX; }
    int logicalMouseY() const { return m_logicalMouseY; }

    // Loads a texture from the asset folder. Returns nullptr on failure
    // (caller decides whether that is fatal). Optionally reports size.
    SDL_Texture* loadTexture(const std::string& relative, int* outW = nullptr, int* outH = nullptr);

    SDL_Texture* tileset = nullptr;
    int tileSize = 16;
    int tilesetCols = 0;

    // Optional title art drawn on the menu (assets/logo.png). May be null.
    SDL_Texture* menuLogo = nullptr;
    int menuLogoW = 0;
    int menuLogoH = 0;

    // Gameplay art (all optional; code null-checks before drawing).
    SDL_Texture* trucksTex = nullptr;     // 6 top-down tow trucks (face up), rotated in code
    int truckCount = 6;
    int truckCellW = 128;
    int truckCellH = 128;
    SDL_Texture* carsTex = nullptr;       // 6 top-down car types in a row (face up)
    int carCount = 6;
    int carCellW = 96;
    int carCellH = 96;
    SDL_Texture* buildingsTex = nullptr;  // 6 cells in a row
    int buildingCount = 6;
    int buildingCellW = 128;
    int buildingCellH = 176;
    SDL_Texture* rivalTrucksTex = nullptr; // 6 black rival trucks, same layout as trucksTex
    SDL_Texture* lotTex = nullptr;         // cordoned "for bid" lot marker
    int lotW = 0, lotH = 0;
    SDL_Texture* iconsTex = nullptr;       // 6 UI icons: gavel,handshake,money,bank,chart,trophy
    int iconCount = 6;
    int iconCell = 64;
    SDL_Texture* menuBg = nullptr;
    int menuBgW = 0;
    int menuBgH = 0;
    SDL_Texture* openttdGuiTex = nullptr; // OpenTTD UI graphics & icons

    float dt() const { return m_dt; } // seconds since last frame
    const std::string& baseDir() const { return m_basePath; }
    const std::string& saveDir() const { return m_savePath; }

    Camera camera;
    struct Preferences {
        int difficulty = 1;
        bool hints = true, weather = true, autosave = true;
        bool overrideSaved = false;
    } preferences;

private:
    void updateViewport();
    void logMessage(const std::string& msg);
    void logError(const std::string& context, const std::string& detail);

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool m_quit = false;
    int m_width = 0, m_height = 0;
    std::unique_ptr<GameState> m_state;
    std::string m_basePath;
    std::string m_savePath;
    std::string m_assetRoot;
    std::ofstream m_log;
    int m_logicalW = 1280;
    int m_logicalH = 720;
    int m_windowW = 0;
    int m_windowH = 0;
    int m_renderW = 0;
    int m_renderH = 0;
    float m_scaleX = 1.0f;
    float m_scaleY = 1.0f;
    float m_dt = 0.0f;
    simulation::FixedStepClock m_simulationClock;
    unsigned int m_lastTicks = 0;
    int m_mouseX = 0;
    int m_mouseY = 0;
    int m_logicalMouseX = 0;
    int m_logicalMouseY = 0;
};

