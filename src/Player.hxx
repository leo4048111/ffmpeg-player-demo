#ifdef _WIN32
// Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "SDL2/SDL.h"
};
#else
//Linux, Mac OS X...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
};
#endif
#endif

#include <string_view>

namespace fpd
{
    class Player
    {
    private:
        Player();

    public:
        ~Player() = default;
        Player(const Player &) = delete;
        Player &operator=(const Player &) = delete;

        static Player &instance()
        {
            static Player instance;
            return instance;
        }
    
    private:
        AVFormatContext* _av_format_ctx;

    public:
        bool getStreamInfo(const std::string_view &file);
    };
}