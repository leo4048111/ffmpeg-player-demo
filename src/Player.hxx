#pragma once

#ifdef _WIN32
// Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "libavutil/intreadwrite.h"
#include "SDL2/SDL.h"
};
#else
// Linux, Mac OS X...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/intreadwrite.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
};
#endif
#endif

#include <string_view>
#include <queue>
#include <mutex>

#include "Singleton.hxx"

namespace fpd
{
    class Player
    {
        SINGLETON(Player)

    public:
        static const int PLAYER_WINDOW_WIDTH = 1280;
        static const int PLAYER_WINDOW_HEIGHT = 720;

    public:
        static std::string_view getPlayerModeName(const int mode);

        int getStreamInfo(const std::string_view &file);

        // int dumpVideoAndAudioStream(const std::string_view &file);

        // int lameDumpVideoAndAudioStream(const std::string_view &file);  // lame implementation, just for test

        int dumpH264AndAACFromVideoFile(const std::string_view &file);

        int dumpYUVAndPlayVideoStream(const std::string_view &file);

        int dumpPCMAndPlayAudioStream(const std::string_view &file);

        int play(const std::string_view &file);

    private:
        std::queue<AVFrame*> _videoFrameQueue;
        std::queue<AVFrame*> _audioFrameQueue;
        std::mutex _videoFrameQueueMutex;
        std::mutex _audioFrameQueueMutex;
    };
}