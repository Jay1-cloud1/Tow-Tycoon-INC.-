#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "Engine.h"
#include "States.h"
#include <memory>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    Engine engine;
    if (!engine.init("TowTycoon v0.1.1", 1280, 720)) return 1;

    engine.changeState(std::make_unique<MenuState>());
    engine.run();
    engine.shutdown();
    return 0;
}

