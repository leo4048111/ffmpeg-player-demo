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
// Linux, Mac OS X...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
};
#endif
#endif

#include <string>
#include <unordered_map>
#include <stdexcept>
#include <memory>

#include "FFWrapper.hxx"

namespace fpd
{
    class Decoder
    {
    public:
        static const int INIT_VIDEO = 0x01;
        static const int INIT_AUDIO = 0x02;

        using DecoderCallback = void (*)(const AVMediaType type,  AVFrame *frame);

        Decoder(int flag, const std::string_view &file);
        ~Decoder();

        int start(DecoderCallback callback);

    private:
        AVFormatContext *_avFormatCtx{nullptr};
        int _flag{0};
        std::unordered_map<int, std::unique_ptr<CodecContext>> _streamDecoderMap;
    };
}