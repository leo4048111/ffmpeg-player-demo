// #include <iostream>

// extern "C" {
//     #include "libavcodec/avcodec.h"
//     #include "libavformat/avformat.h"
//     #include "libavutil/avutil.h"
// }

// int main(int argc, char** argv) {
//     // Register all formats and codecs
//     std::cout << "av_version_info:" << av_version_info() << std::endl;
//     std::cout << "av_version_info:" << avcodec_configuration() << std::endl;
//     return 0;
// }

// #include <SDL2/SDL.h>

// #include <iostream>

// const int SCREEN_WIDTH = 640;
// const int SCREEN_HEIGHT = 480;

// int main()
// {
//     if (int err = SDL_Init(SDL_INIT_VIDEO) < 0) {
//         std::cout << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
//         return err;
//     }

//     SDL_Window* window = SDL_CreateWindow("SDL Tutorial", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
//     if (window == nullptr) {
//         std::cout << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
//         return -1;
//     }

//     return 0;
// }

#include "Options.hxx"
#include "Player.hxx"
#include "Logger.hxx"

int main(int argc, char **argv)
{
    if(!fpd::Options::instance().parse(argc, argv))
        return 0;

    switch(fpd::Options::instance()._mode)
    {
        case 0: // get stream infos
        for(auto& f : fpd::Options::instance()._files)
        {
            LOG_INFO("Get stream infos for file: %s", f.c_str());
            fpd::Player::instance().getStreamInfo(f);
        }
            break;
        default:
            break;
    }

    return 0;
}