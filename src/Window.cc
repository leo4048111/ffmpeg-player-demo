#include "Window.hxx"

#include "Logger.hxx"

namespace fpd
{
    int Window::initSDL(const int windowWidth, const int windowHeight)
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
                                     windowWidth, windowHeight);
        _rect.x = 0;
        _rect.y = 0;
        _rect.w = windowWidth;
        _rect.h = windowHeight;
        _sdlInitialized = true;

        SDL_Event e;
        bool running = true;
        while (running)
        {
            while (SDL_PollEvent(&e))
            {
                if (e.type == SDL_QUIT)
                {
                    running = false;
                }
            }
        }

        return ec;
    }

    void Window::destroySDL()
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

    // void Window::videoRefresh(const AVFrame *yuvFrame, int64_t startTime, AVRational bq, AVRational cq)
    // {
    //     if (!_sdlInitialized)
    //         return;

    //     SDL_UpdateYUVTexture(_texture, &_rect,
    //                          yuvFrame->data[0], yuvFrame->linesize[0],
    //                          yuvFrame->data[1], yuvFrame->linesize[1],
    //                          yuvFrame->data[2], yuvFrame->linesize[2]);

    //     SDL_RenderClear(_renderer);
    //     SDL_RenderCopy(_renderer, _texture, nullptr, &_rect);
    //     SDL_RenderPresent(_renderer);

    //     int64_t delay = av_rescale_q(yuvFrame->pkt_dts - startTime, bq, cq);
    //     SDL_Delay(delay);
    // }

}