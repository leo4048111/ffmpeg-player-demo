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
#include <SDL2/SDL.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
#ifdef __cplusplus
};
#endif
#endif

namespace fpd
{
    class FormatContext
    {
    public:
        FormatContext()
        {
            if (_avFormatCtx == nullptr)
                _avFormatCtx = avformat_alloc_context();
        }

        ~FormatContext()
        {
            if (_avFormatCtx != nullptr)
                avformat_free_context(_avFormatCtx);
        }

        FormatContext(const FormatContext &) = delete;
        FormatContext &operator=(const FormatContext &) = delete;
        FormatContext(FormatContext &&other)
        {
            _avFormatCtx = other.get();
            other.set(nullptr);
        }
        FormatContext &operator=(FormatContext &&other)
        {
            _avFormatCtx = other.get();
            other.set(nullptr);
            return *this;
        }

        AVFormatContext *operator->() const
        {
            return _avFormatCtx;
        }

        AVFormatContext *get() const
        {
            return _avFormatCtx;
        }

        void set(AVFormatContext *ctx)
        {
            _avFormatCtx = ctx;
        }

        int openInput(const std::string_view &file, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr)
        {
            return avformat_open_input(&_avFormatCtx, file.data(), fmt, options);
        }

    private:
        AVFormatContext *_avFormatCtx{nullptr};
    };

    class CodecContext
    {
    public:
        CodecContext(const AVCodec *avCodec)
        {
            if (_avCodecCtx == nullptr)
                _avCodecCtx = avcodec_alloc_context3(avCodec);
        }

        ~CodecContext()
        {
            if (_avCodecCtx != nullptr)
                avcodec_free_context(&_avCodecCtx);
        }

        CodecContext(const CodecContext &) = delete;
        CodecContext &operator=(const CodecContext &) = delete;
        CodecContext(CodecContext &&other)
        {
            _avCodecCtx = other.get();
            other.set(nullptr);
        }
        CodecContext &operator=(CodecContext &&other)
        {
            _avCodecCtx = other.get();
            other.set(nullptr);
            return *this;
        }

        AVCodecContext *operator->() const
        {
            return _avCodecCtx;
        }

        AVCodecContext *get() const
        {
            return _avCodecCtx;
        }

        void set(AVCodecContext *ctx)
        {
            _avCodecCtx = ctx;
        }

    private:
        AVCodecContext *_avCodecCtx{nullptr};
    };
}