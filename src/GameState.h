#pragma once
#include <SDL.h>

class Engine;

class GameState {
public:
    virtual ~GameState() = default;
    virtual void handleEvent(Engine&, SDL_Event&) {}
    virtual void update(Engine&, float) {}
    virtual void render(Engine&) {}
    virtual const char* name() const { return "GameState"; }
};
