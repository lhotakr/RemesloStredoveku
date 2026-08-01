#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "game/Game.h"   // uprav include podle tvé struktury

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "RemesloStredoveku",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    Game game;
    if (!game.init(window)) {
        SDL_Log("Game init failed");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Event e;
    Uint64 prev = SDL_GetPerformanceCounter();

    while (game.isRunning()) // musí existovat (nebo použij game.running())
    {
        while (SDL_PollEvent(&e)) {
            game.handleEvent(e);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - prev) / (double)SDL_GetPerformanceFrequency());
        prev = now;

        game.update(dt);
        game.render();
    }

    game.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}