#include "Window.hxx"

#include "Logger.hxx"

namespace fpd
{
    Window::Window() = default;
    Window::~Window() = default;

    bool Window::init(const int width, const int height)
    {
        int ec = 0;

        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

        _window = SDL_CreateWindow("ffmpeg player demo",
                                   SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                   width, height,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);

        if (_window == nullptr)
        {
            LOG_ERROR("Failed to create SDL window, error: %s", SDL_GetError());
            return -1;
        }

        _renderer = SDL_CreateRenderer(_window, -1, 0);
        _texture = SDL_CreateTexture(_renderer,
                                     SDL_PIXELFORMAT_IYUV,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     width, height);
        _rect.x = 0;
        _rect.y = 0;
        _rect.w = width;
        _rect.h = height;
        _sdlInitialized = true;

        return ec;
    }

    void Window::destroy()
    {
        if (_texture != nullptr)
        {
            SDL_DestroyTexture(_texture);
            _texture = nullptr;
        }

        if (_renderer != nullptr)
        {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }

        if (_window != nullptr)
        {
            SDL_DestroyWindow(_window);
            _window = nullptr;
        }

        SDL_Quit();

        _sdlInitialized = false;
    }

    void Window::resize(const int width, const int height)
    {
        if (!_sdlInitialized)
            return;

        _rect.w = width;
        _rect.h = height;
    }

    void Window::loop(WindowLoopCallback onWindowLoop)
    {
        _running = true;
        SDL_Event e;
        while (_running)
        {
            onWindowLoop();
            while (SDL_PollEvent(&e))
            {
                if (e.type == SDL_QUIT)
                {
                    _running = false;
                }
            }
        }
    }

    void Window::videoRefresh(const Uint8 *ydata, const int ysize,
                              const Uint8 *udata, const int usize,
                              const Uint8 *vdata, const int vsize,
                              int64_t delay)
    {
        if (!_sdlInitialized)
            return;

        SDL_UpdateYUVTexture(_texture, &_rect,
                             ydata, ysize,
                             udata, usize,
                             vdata, vsize);

        SDL_RenderClear(_renderer);
        SDL_RenderCopy(_renderer, _texture, nullptr, &_rect);
        SDL_RenderPresent(_renderer);

        SDL_Delay(delay);
    }
}