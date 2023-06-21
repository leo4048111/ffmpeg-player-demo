#include "Decoder.hxx"
#include "Logger.hxx"

#include "Spinner.hxx"

namespace fpd
{
    Decoder::Decoder(int flag, const std::string_view &file) : _flag(flag)
    {
        if (avformat_open_input(&_avFormatCtx, file.data(), nullptr, nullptr) != 0)
            throw std::runtime_error("Could not open file " + std::string(file));

        if (avformat_find_stream_info(_avFormatCtx, nullptr) < 0)
            throw std::runtime_error("Could not find stream information");

        int ec = 0;
        if (_flag & INIT_VIDEO)
        {
            ec = av_find_best_stream(_avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (ec < 0)
            {
                LOG_WARNING("Could not find video stream in input file");
            }
            else
            {
                _videoStreamIdx = ec;
                _streamDecoderMap[ec] = nullptr;
            }
        }

        if (_flag & INIT_AUDIO)
        {
            ec = av_find_best_stream(_avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (ec < 0)
            {
                LOG_WARNING("Could not find audio stream in input file");
            }
            else
            {
                _audioStreamIdx = ec;
                _streamDecoderMap[ec] = nullptr;
            }
        }
    }

    Decoder::~Decoder()
    {
        avformat_close_input(&_avFormatCtx);
    }

    const int Decoder::getVideoWidth() const
    {
        if (_videoStreamIdx != -1)
            return _avFormatCtx->streams[_videoStreamIdx]->codecpar->width;
        else
            return 0;
    }

    const int Decoder::getVideoHeight() const
    {
        if (_videoStreamIdx != -1)
            return _avFormatCtx->streams[_videoStreamIdx]->codecpar->height;
        else
            return 0;
    }

    const AVRational Decoder::getStreamTimebase(const AVMediaType type) const
    {
        int ec = 0;
        ec = av_find_best_stream(_avFormatCtx, type, -1, -1, nullptr, 0);
        if (ec < 0)
        {
            return AVRational{0, 0};
        }
        else
        {
            return _avFormatCtx->streams[ec]->time_base;
        }
    }

    int Decoder::start(DecoderCallback onReceiveFrame, DecoderCallback onDecoderExit)
    {
        int ec = 0;

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
        _running = true;
        _paused = false;

        auto x = [=]()
        {
            AVPacket pkt;
            auto x = std::make_unique<spinner::spinner>(41);
            x->start();

            while (_running)
            {
                if (_paused)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                if (av_read_frame(_avFormatCtx, &pkt) < 0)
                    break;
                if (_streamDecoderMap.find(pkt.stream_index) != _streamDecoderMap.end())
                {
                    // has valid codec
                    AVFrame *frame = av_frame_alloc();
                    AVCodecContext *codecCtx = _streamDecoderMap[pkt.stream_index]->get();
                    AVStream *avStream = _avFormatCtx->streams[pkt.stream_index];
                    if (avcodec_send_packet(codecCtx, &pkt) == 0)
                    {
                        while (avcodec_receive_frame(codecCtx, frame) == 0)
                        {
                            onReceiveFrame(avStream->codecpar->codec_type, frame);
                        }
                    }

                    av_frame_free(&frame);
                }

                av_packet_unref(&pkt);
            }

            x->stop();
            onDecoderExit(AVMEDIA_TYPE_UNKNOWN, nullptr);
        };

        _t = std::thread(x);
        _t.detach();

        return ec;
    }

    const int Decoder::getAudioChannels() const
    {
        if (_audioStreamIdx != -1)
            return _avFormatCtx->streams[_audioStreamIdx]->codecpar->ch_layout.nb_channels;
        else
            return 0;
    }

    void Decoder::stop()
    {
        _running = false;
    }

    void Decoder::pause()
    {
        _paused = true;
    }

    void Decoder::resume()
    {
        _paused = false;
    }

    bool Decoder::isPaused() const
    {
        return _paused;
    }
}