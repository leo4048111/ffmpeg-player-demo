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
    if (!fpd::Options::instance().parse(argc, argv))
        return 0;

    int ec = 0;

    fpd::Player& player = fpd::Player::instance();

    LOG_INFO("Player task: %s", player.getPlayerModeName(fpd::Options::instance()._mode).data());

    switch (fpd::Options::instance()._mode)
    {
    case 0: // get stream infos
        for (auto &f : fpd::Options::instance()._files)
        {
            LOG_INFO("Get stream infos for file: %s", f.c_str());
            if((ec = player.getStreamInfo(f)))
                return ec;
        }
        break;
    case 1:
        for (auto &f : fpd::Options::instance()._files)
        {
            LOG_INFO("Dump H.264/265 and acc streams for file: %s", f.c_str());
            if ((ec = player.dumpH264AndAACFromVideoFile(f)))
                return ec;
        }
        break;
    default:
        break;
    }

    return ec;
}