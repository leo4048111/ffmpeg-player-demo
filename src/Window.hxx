#pragma once

#ifdef _WIN32
// Windows
extern "C"
{
#include "SDL2/SDL.h"
};
#else
// Linux, Mac OS X...
#ifdef __cplusplus
extern "C"
{
#endif
#include <SDL2/SDL.h>
#ifdef __cplusplus
};
#endif
#endif

#include "Singleton.hxx"

namespace fpd
{
    class Window
    {
        SINGLETON(Window)

    public:
        bool init();

        void loop();

        bool destroy();

    private:
        int initSDL(const int windowWidth, const int windowHeight);

        void destroySDL();

    private:
        SDL_Window *_window{nullptr};
        SDL_Renderer *_renderer{nullptr};
        SDL_Texture *_texture{nullptr};
        SDL_Rect _rect;
        bool _sdlInitialized{false};
    };
}