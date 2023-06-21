#include "Window.hxx"

#include "Logger.hxx"

namespace fpd
{
    Window::Window() = default;
    Window::~Window() = default;

    bool Window::init(const int windowWidth, const int windowHeight, const int textureWidth, const int textureHeight)
    {
        int ec = 0;

        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

        _window = SDL_CreateWindow("ffmpeg player demo",
                                   SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                   windowWidth, windowHeight,
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
                                     textureWidth, textureHeight);

        _windowRect.x = _textureRect.x = 0;
        _windowRect.y = _textureRect.y = 0;
        _windowRect.w = windowWidth;
        _windowRect.h = windowHeight;
        _textureRect.w = textureWidth;
        _textureRect.h = textureHeight;
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

        // _rect.w = width;
        // _rect.h = height;
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
                              const Uint8 *vdata, const int vsize)
    {
        if (!_sdlInitialized)
            return;

        SDL_UpdateYUVTexture(_texture, &_textureRect,
                             ydata, ysize,
                             udata, usize,
                             vdata, vsize);

        SDL_RenderClear(_renderer);
        SDL_RenderCopy(_renderer, _texture, &_textureRect, &_windowRect);
        SDL_RenderPresent(_renderer);
    }

    int Window::openAudio(SDL_AudioSpec spec)
    {
        int ec = 0;
        if((ec = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, SDL_AUDIO_ALLOW_ANY_CHANGE)) < 2)
            LOG_ERROR("Failed to open audio device, error: %s", SDL_GetError());

        return ec;
    }

    void Window::closeAudio()
    {
        SDL_CloseAudio();
    }
}