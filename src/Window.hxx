#pragma once

#ifdef _WIN32
// Windows
extern "C"
{
#include "SDL2/SDL.h"
#include "SDL2/SDL_ttf.h"
};
#else
// Linux, Mac OS X...
#ifdef __cplusplus
extern "C"
{
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#ifdef __cplusplus
};
#endif
#endif

#include <functional>

#include "Singleton.hxx"

namespace fpd
{
    class Window
    {
        SINGLETON(Window)

    public:
        using WindowLoopCallback = std::function<void()>;

    public:
        bool init(const int windowWidth, const int windowHeight, const int textureWidth, const int textureHeight);

        void loop(WindowLoopCallback onWindowLoop);

        void destroy();

        void videoRefresh(const Uint8 *ydata, const int ysize,
                          const Uint8 *udata, const int usize,
                          const Uint8 *vdata, const int vsize);

        void resize(const int width, const int height);

        int openAudio(SDL_AudioSpec spec);

        void closeAudio();

    private:
        SDL_Window *_window{nullptr};
        SDL_Renderer *_renderer{nullptr};
        SDL_Texture *_texture{nullptr};
        SDL_Rect _textureRect;
        SDL_Rect _windowRect;
        bool _sdlInitialized{false};
        bool _running{false};
    };
}