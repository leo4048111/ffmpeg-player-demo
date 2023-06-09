#ifdef _WIN32
// Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
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
#include <libavutil/intreadwrite.h>
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
        ~Player();
        Player(const Player &) = delete;
        Player &operator=(const Player &) = delete;

        static Player &instance()
        {
            static Player instance;
            return instance;
        }
    
    private:
    
    public:
        static std::string_view getPlayerModeName(const int mode);

        int getStreamInfo(const std::string_view &file);

        int dumpVideoAndAudioStream(const std::string_view &file);

        int lameDumpVideoAndAudioStream(const std::string_view &file);  // lame implementation, just for test
    
        int dumpH264AndAACFromVideoFile(const std::string_view &file); // lame implementation, only works when source streams are already encoded in aac and h264
    
        int dumpYUVAndPlayVideoStream(const std::string_view &file);
    };
}