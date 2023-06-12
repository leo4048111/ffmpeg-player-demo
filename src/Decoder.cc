#include "Decoder.hxx"
#include "Logger.hxx"

#include <thread>

namespace fpd
{
    Decoder::Decoder(int flag, const std::string_view &file) : _flag(flag)
    {
        if (avformat_open_input(&_avFormatCtx, file.data(), nullptr, nullptr) != 0)
            throw std::runtime_error("Could not open file " + std::string(file));
    }

    Decoder::~Decoder()
    {
        avformat_close_input(&_avFormatCtx);
    }

    int Decoder::start(DecoderCallback callback)
    {
        int ec = 0;

        if (_flag & INIT_VIDEO)
        {
            ec = av_find_best_stream(_avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (ec < 0)
            {
                LOG_ERROR("Could not find video stream in input file");
                return ec;
            }
            else
                _streamDecoderMap[ec] = nullptr;
        }

        if (_flag & INIT_AUDIO)
        {
            ec = av_find_best_stream(_avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (ec < 0)
            {
                LOG_ERROR("Could not find audio stream in input file");
                return ec;
            }
            else
                _streamDecoderMap[ec] = nullptr;
        }

        // init codec ctx for all streams to be decoded
        for (auto &x : _streamDecoderMap)
        {
            const int streamIdx = x.first;
            AVStream *stream = _avFormatCtx->streams[x.first];

            const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (codec == nullptr)
            {
                LOG_WARNING("Failed to find decoder for stream %d, type %s", streamIdx, av_get_media_type_string(stream->codecpar->codec_type));
            }
            else
            {
                _streamDecoderMap[streamIdx] = std::make_unique<CodecContext>(codec);
                if ((ec = avcodec_parameters_to_context(_streamDecoderMap[streamIdx]->get(), stream->codecpar)) < 0)
                {
                    LOG_ERROR("Failed to copy decoder parameters to input decoder context for stream %d, type %s", streamIdx, av_get_media_type_string(stream->codecpar->codec_type));
                    continue;
                }

                if ((ec = avcodec_open2(_streamDecoderMap[streamIdx]->get(), codec, nullptr)) < 0)
                {
                    LOG_ERROR("Failed to open codec for stream %d, type %s", streamIdx, av_get_media_type_string(stream->codecpar->codec_type));
                    continue;
                }
            }
        }

        // start decode thread
        auto x = [=]()
        {
            AVPacket pkt;
            LOG_INFO("In decode thread...");
            callback(AVMEDIA_TYPE_UNKNOWN, nullptr);
        };

        std::thread t(x);
        t.detach();

        return ec;
    }
}