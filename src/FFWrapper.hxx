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

#include <queue>
#include <mutex>

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

        FormatContext(const std::string_view &file, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr)
        {
            avformat_open_input(&_avFormatCtx, file.data(), fmt, options);
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

    class Frame
    {
    public:
        Frame()
        {
            if (_avFrame == nullptr)
                _avFrame = av_frame_alloc();
        }

        ~Frame()
        {
            if (_avFrame != nullptr)
                av_frame_free(&_avFrame);
        }

        Frame(const Frame &other)
        {
            _avFrame = av_frame_alloc();
            if (_avFrame != nullptr)
            {
                av_frame_ref(_avFrame, other._avFrame);
            }
        }

        Frame &operator=(const Frame &other)
        {
            if (this != &other)
            {
                if (_avFrame != nullptr)
                {
                    av_frame_unref(_avFrame); // 先释放原来的数据
                }
                else
                {
                    _avFrame = av_frame_alloc();
                }

                if (_avFrame != nullptr)
                {
                    av_frame_ref(_avFrame, other._avFrame);
                }
            }
            return *this;
        }

        Frame(Frame &&other)
        {
            _avFrame = other.get();
            other.set(nullptr);
        }
        Frame &operator=(Frame &&other)
        {
            _avFrame = other.get();
            other.set(nullptr);
            return *this;
        }

        AVFrame *operator->() const
        {
            return _avFrame;
        }

        AVFrame *get() const
        {
            return _avFrame;
        }

        void set(AVFrame *frame)
        {
            _avFrame = frame;
        }

    private:
        AVFrame *_avFrame{nullptr};
    };

    using StreamInfo = struct StreamInfo
    {
        AVMediaType type;
        const AVCodec *codec;
        std::queue<Frame> frameQueue;
        std::mutex queueLock;

        StreamInfo(AVMediaType type, const AVCodec *codec)
            : type(type), codec(codec)
        {
        }
    };
}