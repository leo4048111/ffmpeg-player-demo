#include "Decoder.hxx"
#include "Logger.hxx"

#include "Spinner.hxx"

namespace fpd
{
    Decoder::Decoder(int flag, const std::string_view &file) : _flag(flag)
    {
        if (_formatCtx.openInput(file) != 0)
            throw std::runtime_error("Could not open input file");
    }

    Decoder::~Decoder()
    {
    }

    int Decoder::start(DecoderCallback onDecoderExit)
    {
        int ec = 0;

        if (_flag & INIT_VIDEO)
        {
            ec = av_find_best_stream(_formatCtx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (ec < 0)
            {
                LOG_ERROR("Could not find video stream in input file");
                return ec;
            }
            else
                _streamInfoMap[ec] = nullptr;
        }

        if (_flag & INIT_AUDIO)
        {
            ec = av_find_best_stream(_formatCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (ec < 0)
            {
                LOG_ERROR("Could not find audio stream in input file");
                return ec;
            }
            else
                _streamInfoMap[ec] = nullptr;
        }

        // init codec ctx for all streams to be decoded
        for (auto &x : _streamInfoMap)
        {
            const int streamIdx = x.first;
            AVStream *stream = _formatCtx->streams[streamIdx];

            const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (codec == nullptr)
            {
                LOG_WARNING("Failed to find decoder for stream %d, type %s", streamIdx, av_get_media_type_string(stream->codecpar->codec_type));
            }
            else
            {
                _streamInfoMap.insert(std::make_pair(streamIdx, std::make_unique<StreamInfo>(stream->codecpar->codec_type, codec)));
                _streamDecoderMap.insert(std::make_pair(streamIdx, std::make_unique<CodecContext>(codec)));
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
            auto x = std::make_unique<spinner::spinner>(41);
            x->start();

            while (av_read_frame(_formatCtx.get(), &pkt) >= 0)
            {
                if ((_streamDecoderMap.find(pkt.stream_index) != _streamDecoderMap.end()) &&
                    (_streamDecoderMap[pkt.stream_index] != nullptr))
                {
                    // has valid codec
                    Frame frame;
                    AVCodecContext *codecCtx = _streamDecoderMap[pkt.stream_index]->get();
                    AVStream *avStream = _formatCtx->streams[pkt.stream_index];
                    if (avcodec_send_packet(codecCtx, &pkt) == 0)
                    {
                        while (avcodec_receive_frame(codecCtx, frame.get()) == 0)
                        {
                            std::lock_guard<std::mutex> lock(_streamInfoMap[pkt.stream_index]->queueLock);
                            _streamInfoMap[pkt.stream_index]->frameQueue.push(frame);
                        }
                    }
                }

                av_packet_unref(&pkt);
            }

            x->stop();
            onDecoderExit();
        };

        _t = std::thread(x);
        _t.detach();

        return ec;
    }

    bool Decoder::receive(const AVMediaType type, Frame &frame)
    {
        bool ret = false;

        for (auto &x : _streamInfoMap)
        {
            if (x.second->type == type)
            {
                if (x.second->frameQueue.size())
                {
                    std::lock_guard<std::mutex> lock(x.second->queueLock);
                    frame = std::move(x.second->frameQueue.front());
                    x.second->frameQueue.pop();
                    ret = true;
                }
                break;
            }
        }

        return ret;
    }
}