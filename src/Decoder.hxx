#pragma once

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
#include <thread>
#include <functional>

#include "FFWrapper.hxx"

namespace fpd
{
    class Decoder
    {
    public:
        static const int INIT_VIDEO = 0x01;
        static const int INIT_AUDIO = 0x02;

        using DecoderCallback = std::function<void(const AVMediaType type, AVFrame *frame)>;

        Decoder(int flag, const std::string_view &file);
        ~Decoder();

        int start(DecoderCallback onReceiveFrame, DecoderCallback onDecoderExit);

        const int getVideoWidth() const;
        const int getVideoHeight() const;

    private:
        AVFormatContext *_avFormatCtx{nullptr};
        int _flag{0};
        std::unordered_map<int, std::unique_ptr<CodecContext>> _streamDecoderMap;
        std::thread _t;
        int _videoStreamIdx{-1};
        int _audioStreamIdx{-1};

        int _videoWidth{0};
        int _videoHeight{0};
    };
}