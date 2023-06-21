#include <fstream>
#include <mutex>
#include <functional>

#include "Player.hxx"
#include "Logger.hxx"
#include "Utils.hxx"
#include "FFWrapper.hxx"
#include "Decoder.hxx"
#include "Window.hxx"
#include "Spinner.hxx"

namespace
{
    const SDL_AudioFormat ffAudioFmt2SdlAudioFmt(const AVSampleFormat fmt)
    {
        switch (fmt)
        {
        case AV_SAMPLE_FMT_NONE:
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_U8P:
            return AUDIO_U8;

        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S16P:
            return AUDIO_S16SYS;

        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_S32P:
            return AUDIO_S32SYS;

        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_FLTP:
            return AUDIO_F32SYS;

        // SDL doesn't support double type or 64 bits, so we'll return an error for these
        case AV_SAMPLE_FMT_DBL:
        case AV_SAMPLE_FMT_DBLP:
        case AV_SAMPLE_FMT_S64:
        case AV_SAMPLE_FMT_S64P:
            return -1;

        default:
            return -1;
        }
    }

    int h264ExtradataToAnnexb(const uint8_t *codec_extradata, const int codec_extradata_size, AVPacket *out_extradata, int padding)
    {
        uint16_t unit_size = 0;
        uint64_t total_size = 0;
        uint8_t *out = NULL;
        uint8_t unit_nb = 0;
        uint8_t sps_done = 0;
        uint8_t sps_seen = 0;
        uint8_t pps_seen = 0;
        uint8_t sps_offset = 0;
        uint8_t pps_offset = 0;

        /**
         * AVCC
         * bits
         *  8   version ( always 0x01 )
         *  8   avc profile ( sps[0][1] )
         *  8   avc compatibility ( sps[0][2] )
         *  8   avc level ( sps[0][3] )
         *  6   reserved ( all bits on )
         *  2   NALULengthSizeMinusOne
         *  3   reserved ( all bits on )
         *  5   number of SPS NALUs (usually 1)
         *
         *  repeated once per SPS
         *  16     SPS size
         *
         *  variable   SPS NALU data
         *  8   number of PPS NALUs (usually 1)
         *  repeated once per PPS
         *  16    PPS size
         *  variable PPS NALU data
         */
        const uint8_t *extradata = codec_extradata + 4;
        static const uint8_t nalu_header[4] = {0, 0, 0, 1};

        extradata++;

        sps_offset = pps_offset = -1;

        /* retrieve sps and pps unit(s) */
        unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
        if (!unit_nb)
        {
            goto pps;
        }
        else
        {
            sps_offset = 0;
            sps_seen = 1;
        }

        while (unit_nb--)
        {
            int ec;

            unit_size = AV_RB16(extradata);
            total_size += unit_size + 4;
            if (total_size > INT_MAX - padding)
            {
                LOG_ERROR("Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream");
                return AVERROR(EINVAL);
            }

            if (extradata + 2 + unit_size > codec_extradata + codec_extradata_size)
            {
                LOG_ERROR("Packet header is not contained in global extradata, "
                          "corrupted stream or invalid MP4/AVCC bitstream");
                return AVERROR(EINVAL);
            }

            if ((ec = av_reallocp(&out, total_size + padding)) < 0)
                return ec;

            memcpy(out + total_size - unit_size - 4, nalu_header, 4);
            memcpy(out + total_size - unit_size, extradata + 2, unit_size);
            extradata += 2 + unit_size;

        pps:
            if (!unit_nb && !sps_done++)
            {
                unit_nb = *extradata++; /* number of pps unit(s) */
                if (unit_nb)
                {
                    pps_offset = total_size;
                    pps_seen = 1;
                }
            }
        }

        if (out)
            memset(out + total_size, 0, padding);

        if (!sps_seen)
            LOG_WARNING("SPS NALU missing or invalid. "
                        "The resulting stream may not play.\n");

        if (!pps_seen)
            LOG_WARNING("PPS NALU missing or invalid. "
                        "The resulting stream may not play.\n");

        out_extradata->data = out;
        out_extradata->size = total_size;

        return 0;
    }

    int parseAdtsHeader(char *const p_adts_header, const int data_length,
                        const int profile, const int samplerate, const int channels)
    {
#define ADTS_HEADER_LEN 7
        const int sampling_frequencies[] =
            {
                96000, // 0x0
                88200, // 0x1
                64000, // 0x2
                48000, // 0x3
                44100, // 0x4
                32000, // 0x5
                24000, // 0x6
                22050, // 0x7
                16000, // 0x8
                12000, // 0x9
                11025, // 0xa
                8000   // 0xb
            };

        int sampling_frequencies_index = 3;
        int adtsLen = data_length + 7;

        int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
        int i = 0;
        for (i = 0; i < frequencies_size; i++)
        {
            if (samplerate == sampling_frequencies[i])
            {
                sampling_frequencies_index = i;
                break;
            }
        }
        if (sampling_frequencies_index >= frequencies_size)
        {
            printf("unsupport samplerate:%d\n", samplerate);
            return AVERROR(EINVAL);
        }

        p_adts_header[0] = 0xff;
        p_adts_header[1] = 0xf0;

        p_adts_header[1] |= (0 << 3);
        p_adts_header[1] |= (0 << 1);
        p_adts_header[1] |= 1;

        p_adts_header[2] = (profile) << 6;

        p_adts_header[2] |= (sampling_frequencies_index & 0x0f) << 2;

        p_adts_header[2] |= (0 << 1);

        p_adts_header[2] |= (channels & 0x04) >> 2;
        p_adts_header[3] = (channels & 0x03) << 6;

        p_adts_header[3] |= (0 << 5);

        p_adts_header[3] |= (0 << 4);

        p_adts_header[3] |= (0 << 3);

        p_adts_header[3] |= (0 << 2);

        p_adts_header[3] |= ((adtsLen & 0x1800) >> 11);
        p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);
        p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5);

        p_adts_header[5] |= 0x1f;
        p_adts_header[6] = 0xfc;

        p_adts_header[6] &= 0xfc;

        return 0;

#undef ADTS_HEADER_LEN
    }
}

namespace fpd
{
    Player::Player()
    {
        avformat_network_init();
    }

    Player::~Player()
    {
        avformat_network_deinit();
    }

    std::string_view Player::getPlayerModeName(const int mode)
    {
        switch (mode)
        {
        case 0:
            return "get stream infos";
        case 1:
            return "dump H.264/265 and acc streams from mp4/vls file";
        case 2:
            return "dump yuv data of video stream from mp4/vls file and play with SDL2";
        case 3:
            return "dump pcm data of audio stream from mp4/vls file and play with SDL2";
        case 4:
            return "play mp4/vls file with SDL2";
        default:
            return "unknown mode";
        }
    }

    int Player::getStreamInfo(const std::string_view &file)
    {
        int ec = 0;
        FormatContext formatCtx;

        if ((ec = formatCtx.openInput(file, nullptr, nullptr)) != 0)
        {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return ec;
        }

        if ((ec = avformat_find_stream_info(formatCtx.get(), nullptr)) < 0)
        {
            LOG_ERROR("Failed to find stream info for file: %s", file.data());
            return ec;
        }

        av_dump_format(formatCtx.get(), 0, file.data(), 0);

        return ec;
    }

    // int Player::dumpVideoAndAudioStream(const std::string_view &file)
    // {
    //     int ec = 0;

    //     AVFormatContext *avFormatCtx = avformat_alloc_context();

    //     if ((ec = avformat_open_input(&avFormatCtx, file.data(), nullptr, nullptr)) != 0)
    //     {
    //         LOG_ERROR("Failed to open input file: %s", file.data());
    //         return ec;
    //     }

    //     if ((ec = avformat_find_stream_info(avFormatCtx, nullptr)) < 0)
    //     {
    //         return ec;
    //     }

    //     int videoStreamidx, audioStreamidx;

    //     const AVCodec *videoCodec = nullptr, *audioCodec = nullptr;

    //     ec = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);

    //     if (ec < 0)
    //     {
    //         LOG_ERROR("Failed to find best video stream in file: %s", file.data());
    //         return ec;
    //     }
    //     else
    //         videoStreamidx = ec;

    //     ec = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &audioCodec, 0);

    //     if (ec < 0)
    //     {
    //         LOG_ERROR("Failed to find best audio stream in file: %s", file.data());
    //         return ec;
    //     }
    //     else
    //         audioStreamidx = ec;

    //     AVStream *videoInStream = avFormatCtx->streams[videoStreamidx];
    //     AVStream *audioInStream = avFormatCtx->streams[audioStreamidx];
    //     const AVCodec *videoInCodec = avcodec_find_decoder(videoInStream->codecpar->codec_id);
    //     const AVCodec *audioInCodec = avcodec_find_decoder(audioInStream->codecpar->codec_id);
    //     AVCodecContext *videoInCodecCtx = avcodec_alloc_context3(videoInCodec);
    //     AVCodecContext *audioInCodecCtx = avcodec_alloc_context3(audioInCodec);

    //     // Get pointer to codec parameters
    //     AVCodecParameters *videoCodecParams = videoInStream->codecpar;
    //     AVCodecParameters *audioCodecParams = audioInStream->codecpar;

    //     if ((ec = avcodec_parameters_to_context(videoInCodecCtx, videoCodecParams)) < 0)
    //     {
    //         LOG_ERROR("Failed to copy video codec parameters to input codec context in file: %s", file.data());
    //         return ec;
    //     }

    //     if ((ec = avcodec_parameters_to_context(audioInCodecCtx, audioCodecParams)) < 0)
    //     {
    //         LOG_ERROR("Failed to copy audio codec parameters to input codec context in file: %s", file.data());
    //         return ec;
    //     }

    //     if ((ec = avcodec_open2(videoInCodecCtx, videoInCodec, nullptr)) < 0)
    //     {
    //         LOG_ERROR("Failed to open video codec in file: %s", file.data());
    //         return ec;
    //     }

    //     if ((ec = avcodec_open2(audioInCodecCtx, audioInCodec, nullptr)) < 0)
    //     {
    //         LOG_ERROR("Failed to open audio codec in file: %s", file.data());
    //         return ec;
    //     }

    //     auto filenameNoExt = Utils::getFilenameNoExt(file);
    //     auto videoFilename = filenameNoExt + ".h265";
    //     auto audioFilename = filenameNoExt + ".aac";

    //     AVFormatContext *videoOutFormatCtx, *audioOutFormatCtx;
    //     if ((ec = avformat_alloc_output_context2(&videoOutFormatCtx, nullptr, nullptr, videoFilename.c_str())))
    //     {
    //         LOG_ERROR("Failed to allocate output context for video file: %s", videoFilename.c_str());
    //         return ec;
    //     }

    //     if ((ec = avformat_alloc_output_context2(&audioOutFormatCtx, nullptr, nullptr, audioFilename.c_str())))
    //     {
    //         LOG_ERROR("Failed to allocate output context for audio file: %s", audioFilename.c_str());
    //         return ec;
    //     }

    //     AVStream *videoOutStream = avformat_new_stream(videoOutFormatCtx, nullptr);
    //     AVStream *audioOutStream = avformat_new_stream(audioOutFormatCtx, nullptr);

    //     const AVOutputFormat *videoOutFormat = av_guess_format(nullptr, videoFilename.c_str(), nullptr);
    //     const AVOutputFormat *audioOutFormat = av_guess_format(nullptr, audioFilename.c_str(), nullptr);

    //     const AVCodec *videoOutCodec = avcodec_find_encoder(videoOutFormat->video_codec);
    //     const AVCodec *audioOutCodec = avcodec_find_encoder(audioOutFormat->audio_codec);

    //     AVCodecContext *videoOutCodecCtx = avcodec_alloc_context3(videoOutCodec);
    //     AVCodecContext *audioOutCodecCtx = avcodec_alloc_context3(audioOutCodec);
    //     videoOutCodecCtx->width = videoInCodecCtx->width;
    //     videoOutCodecCtx->height = videoInCodecCtx->height;
    //     videoOutCodecCtx->pix_fmt = videoOutCodec->pix_fmts[0];
    //     videoOutCodecCtx->time_base = videoInStream->time_base;
    //     ec = avcodec_open2(videoOutCodecCtx, videoOutCodec, nullptr);
    //     // ec = avcodec_open2(audioOutCodecCtx, audioOutCodec, nullptr);

    //     avcodec_parameters_from_context(videoOutStream->codecpar, videoOutCodecCtx);
    //     videoOutStream->time_base = videoOutCodecCtx->time_base;
    //     // avcodec_parameters_from_context(audioOutStream->codecpar, audioOutCodecCtx);
    //     // audioOutStream->time_base = audioOutCodecCtx->time_base;

    //     if ((ec = avformat_write_header(videoOutFormatCtx, nullptr)) < 0)
    //     {
    //         LOG_ERROR("Failed to write header to video output file: %s", videoFilename.c_str());
    //         return ec;
    //     }

    //     // if ((ec = avformat_write_header(audioOutFormatCtx, nullptr)) < 0)
    //     // {
    //     //     LOG_ERROR("Failed to write header to audio output file: %s", audioFilename.c_str());
    //     //     return ec;
    //     // }

    //     AVPacket pkt;
    //     AVFrame *frame = av_frame_alloc();
    //     SwsContext *swsCtx = sws_getContext(videoInCodecCtx->width, videoInCodecCtx->height, videoInCodecCtx->pix_fmt, videoOutCodecCtx->width, videoOutCodecCtx->height, videoOutCodecCtx->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);

    //     while (av_read_frame(avFormatCtx, &pkt) >= 0)
    //     {
    //         if (pkt.stream_index == videoStreamidx)
    //         {
    //             avcodec_send_packet(videoInCodecCtx, &pkt);
    //             while (avcodec_receive_frame(videoInCodecCtx, frame) == 0)
    //             {
    //                 AVFrame *outFrame = av_frame_alloc();
    //                 outFrame->format = videoOutCodecCtx->pix_fmt;
    //                 outFrame->width = videoInCodecCtx->width;
    //                 outFrame->height = videoInCodecCtx->height;
    //                 av_frame_get_buffer(outFrame, 0);
    //                 sws_scale(swsCtx, frame->data, frame->linesize, 0, videoInCodecCtx->height, outFrame->data, outFrame->linesize);

    //                 avcodec_send_frame(videoOutCodecCtx, outFrame);

    //                 while (avcodec_receive_packet(videoOutCodecCtx, &pkt) == 0)
    //                 {
    //                     pkt.stream_index = videoOutStream->index;
    //                     pkt.pts = av_rescale_q_rnd(pkt.pts, videoInStream->time_base, videoOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    //                     pkt.dts = av_rescale_q_rnd(pkt.dts, videoInStream->time_base, videoOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    //                     pkt.duration = av_rescale_q(pkt.duration, videoInStream->time_base, videoOutStream->time_base);
    //                     av_interleaved_write_frame(videoOutFormatCtx, &pkt);
    //                 }
    //                 av_frame_free(&outFrame);
    //             }
    //         }
    //         // else if(pkt.stream_index == audioStreamidx)
    //         // {
    //         //     pkt.pts = av_rescale_q_rnd(pkt.pts, avFormatCtx->streams[audioStreamidx]->time_base, audioOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    //         //     pkt.dts = av_rescale_q_rnd(pkt.dts, avFormatCtx->streams[audioStreamidx]->time_base, audioOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    //         //     pkt.duration = av_rescale_q(pkt.duration, avFormatCtx->streams[audioStreamidx]->time_base, audioOutStream->time_base);
    //         //     pkt.pos = -1;
    //         //     av_interleaved_write_frame(audioOutFormatCtx, &pkt);
    //         // }

    //         av_packet_unref(&pkt);
    //     }

    //     av_write_trailer(videoOutFormatCtx);
    //     // av_write_trailer(audioOutFormatCtx);

    //     LOG_INFO("Dumped video stream to file: %s", videoFilename.c_str());
    //     LOG_INFO("Dumped audio stream to file: %s", audioFilename.c_str());

    //     av_frame_free(&frame);
    //     sws_freeContext(swsCtx);
    //     avcodec_close(videoInCodecCtx);
    //     avcodec_close(audioInCodecCtx);
    //     avcodec_close(videoOutCodecCtx);
    //     avcodec_close(audioOutCodecCtx);

    //     avformat_close_input(&avFormatCtx);

    //     avformat_free_context(videoOutFormatCtx);
    //     avformat_free_context(audioOutFormatCtx);
    //     avformat_free_context(avFormatCtx);

    //     return ec;
    // }

    int Player::dumpH264AndAACFromVideoFile(const std::string_view &file)
    {
        int ec = 0;
        FormatContext formatCtx;

        if ((ec = formatCtx.openInput(file, nullptr, nullptr)) < 0)
        {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return ec;
        }

        if ((ec = avformat_find_stream_info(formatCtx.get(), nullptr)) < 0)
        {
            LOG_ERROR("Failed to find stream info for input file: %s", file.data());
            return ec;
        }

        int videoStreamidx, audioStreamidx;

        ec = av_find_best_stream(formatCtx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (ec < 0)
        {
            LOG_ERROR("Failed to find best video stream for input file: %s", file.data());
            return ec;
        }
        else
            videoStreamidx = ec;

        ec = av_find_best_stream(formatCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

        if (ec < 0)
        {
            LOG_ERROR("Failed to find best audio stream for input file: %s", file.data());
            return ec;
        }
        else
            audioStreamidx = ec;

        AVStream *videoInStream = formatCtx->streams[videoStreamidx];
        AVStream *audioInStream = formatCtx->streams[audioStreamidx];

        // check stream encoding
        if (videoInStream->codecpar->codec_id != AV_CODEC_ID_H264) // video stream only support h264
        {
            LOG_ERROR("Expect video stream encoding to be %s, got %s",
                      avcodec_get_name(AV_CODEC_ID_H264),
                      avcodec_get_name(videoInStream->codecpar->codec_id));
            return AVERROR(EINVAL);
        }

        if (audioInStream->codecpar->codec_id != AV_CODEC_ID_AAC) // audio stream only support aac
        {
            LOG_ERROR("Expect audio stream encoding to be %s, got %s",
                      avcodec_get_name(AV_CODEC_ID_AAC),
                      avcodec_get_name(audioInStream->codecpar->codec_id));
            return AVERROR(EINVAL);
        }

        auto filenameNoExt = Utils::getFilenameNoExt(file);
        auto videoFilename = filenameNoExt + ".h264";
        auto audioFilename = filenameNoExt + ".aac";

        std::ofstream videoFile(videoFilename, std::ios::binary);
        std::ofstream audioFile(audioFilename, std::ios::binary);

        // parse SPS and PPS info and write to head of the video file
        AVPacket outExtraDataPkt;
        const char h264VideoFrameHeader[] = {0x00, 0x00, 0x00, 0x01};
        ec = h264ExtradataToAnnexb(
            videoInStream->codecpar->extradata,
            videoInStream->codecpar->extradata_size,
            &outExtraDataPkt,
            AV_INPUT_BUFFER_PADDING_SIZE);

        if (ec != 0)
            return ec;

        memcpy(&outExtraDataPkt.data[0], h264VideoFrameHeader, sizeof(h264VideoFrameHeader));
        videoFile.write((const char *)outExtraDataPkt.data, outExtraDataPkt.size);

        AVPacket pkt;

        // auto x = std::make_unique<spinner::spinner>(41);
        // x->start();
        while (av_read_frame(formatCtx.get(), &pkt) >= 0)
        {
            if (pkt.stream_index == videoStreamidx)
            {
                memcpy(&pkt.data[0], h264VideoFrameHeader, sizeof(h264VideoFrameHeader));
                videoFile.write((const char *)pkt.data, pkt.size);
            }
            else if (pkt.stream_index == audioStreamidx)
            {
                char adtsHeader[7] = {0};
                ec = parseAdtsHeader(adtsHeader, pkt.size, audioInStream->codecpar->profile, audioInStream->codecpar->sample_rate, audioInStream->codecpar->ch_layout.nb_channels);
                audioFile.write(adtsHeader, sizeof(adtsHeader));
                audioFile.write((const char *)pkt.data, pkt.size);
            }
            av_packet_unref(&pkt);
        }

        // x->stop();
        LOG_INFO("Dumped video stream to file: %s", videoFilename.c_str());
        LOG_INFO("Dumped audio stream to file: %s", audioFilename.c_str());

        videoFile.close();
        audioFile.close();

        return ec;
    }

    int Player::dumpYUVAndPlayVideoStream(const std::string_view &file)
    {
        int ec = 0;

        auto filenameNoExt = Utils::getFilenameNoExt(file);
        auto videoYuvOutFilename = filenameNoExt + ".yuv";

        std::ofstream videoYuvOutFile(videoYuvOutFilename, std::ios::binary);

        Decoder decoder(Decoder::INIT_VIDEO, file);
        auto spin = std::make_unique<spinner::spinner>(41);
        spin->start();

        std::function<void(const AVMediaType type, AVFrame *frame)> onReceiveFrame = [&](const AVMediaType type, AVFrame *frame)
        {
            if (type == AVMEDIA_TYPE_VIDEO)
            {
                videoYuvOutFile.write((const char *)frame->data[0], frame->linesize[0] * frame->height);
                videoYuvOutFile.write((const char *)frame->data[1], frame->linesize[1] * frame->height / 2);
                videoYuvOutFile.write((const char *)frame->data[2], frame->linesize[2] * frame->height / 2);

                AVFrame *tmp = av_frame_alloc();
                av_frame_ref(tmp, frame);
                std::lock_guard<std::mutex> lock(_videoFrameQueueMutex);
                _videoFrameQueue.push(tmp);
            }
        };

        std::function<void(const AVMediaType type, AVFrame *frame)> onDecoderExit = [&](const AVMediaType type, AVFrame *frame)
        {
            spin->stop();
            LOG_INFO("Dumped yuv data to file: %s", videoYuvOutFilename.c_str());
            videoYuvOutFile.close();
        };

        AVRational videoStreamTimebase = decoder.getStreamTimebase(AVMEDIA_TYPE_VIDEO);

        auto onWindowLoop = [&]()
        {
            AVFrame *frame;
            static int frameLost = 0;
            static int totalFrame = 0;

            // render video frame
            {
                std::lock_guard<std::mutex> lock(_videoFrameQueueMutex);
                if (_videoFrameQueue.empty())
                    return;
                if (_videoFrameQueue.size() > 10)
                    decoder.pause();
                else if (decoder.isPaused())
                    decoder.resume();

                frame = _videoFrameQueue.front();
                _videoFrameQueue.pop();
                totalFrame++;
            }

            static double syncThreshold = 0.009;
            static int64_t lastPts = 0;
            static double videoStartTime = -1;

            if (frame->pts == 0)
            {
                videoStartTime = av_gettime_relative() / 1000000.0;
                Window::instance().videoRefresh(frame->data[0], frame->linesize[0],
                                                frame->data[1], frame->linesize[1],
                                                frame->data[2], frame->linesize[2]);
                Window::instance().render();
            }
            else
            {
                bool shouldDropFrame = false;
                double currentTime = av_gettime_relative() / 1000000.0 - videoStartTime;
                double pts = frame->pts * (double)videoStreamTimebase.num / videoStreamTimebase.den;
                double diff = pts - currentTime;
                double delay = (frame->pts - lastPts) * (double)videoStreamTimebase.num / videoStreamTimebase.den;
                lastPts = frame->pts;
                // too late, drop frame and rebase timestamp
                if (diff <= -syncThreshold)
                {
                    // videoStartTime = av_gettime_relative() / 1000000.0;
                    if (diff > -delay)
                    {
                        delay += diff;
                    }
                    else
                    {
                        shouldDropFrame = true;
                        frameLost++;
                    }
                }

                // too early, wait some proper time
                if (diff >= syncThreshold)
                {
                    delay = std::min(delay * 2, delay + diff);
                    currentTime = av_gettime_relative() / 1000000.0 - videoStartTime;
                    diff = pts - currentTime;
                }

                // render frame if not too early or too late
                if (!shouldDropFrame)
                {
                    int64_t beforeRenderTime = av_gettime_relative();
                    Window::instance().videoRefresh(frame->data[0], frame->linesize[0],
                                                    frame->data[1], frame->linesize[1],
                                                    frame->data[2], frame->linesize[2]);
                    auto frameInfo = Logger::instance().format("pts: %.5f, diff: %.3fms, elapsed time: %.2fs, loss: %.2f%", pts, diff * 1000, currentTime, (float)frameLost / totalFrame * 100);
                    Window::instance().addText(frameInfo, 0, 0, {255, 255, 255, 255});
                    Window::instance().render();
                    int64_t renderTime = av_gettime_relative() - beforeRenderTime;
                    av_usleep((unsigned int)(delay * 1000000 - renderTime));
                }
            }

            av_frame_unref(frame);
        };


        Window::instance().init(PLAYER_WINDOW_WIDTH, PLAYER_WINDOW_HEIGHT,
                                decoder.getVideoWidth(), decoder.getVideoHeight());
                                
        decoder.start(onReceiveFrame, onDecoderExit);
        Window::instance().loop(onWindowLoop);
        Window::instance().destroy();
        decoder.stop();
        return ec;
    }

    int Player::dumpPCMAndPlayAudioStream(const std::string_view &file)
    {
        int ec = 0;

        auto filenameNoExt = Utils::getFilenameNoExt(file);
        auto audioPcmOutFilename = filenameNoExt + ".pcm";

        std::ofstream audioPcmOutFile(audioPcmOutFilename, std::ios::binary);

        Decoder decoder(Decoder::INIT_AUDIO, file);

        SDL_AudioSpec spec;
        spec.freq = decoder.getAudioSampleRate();
        spec.format = ffAudioFmt2SdlAudioFmt(decoder.getAudioSampleFormat());
        spec.channels = decoder.getAudioChannels();
        spec.silence = 0;
        spec.samples = 4096;
        spec.callback = nullptr;
        spec.userdata = nullptr;

        SDL_AudioDeviceID audioDeviceId;

        std::function<void(const AVMediaType type, AVFrame *frame)> onReceiveFrame = [&](const AVMediaType type, AVFrame *frame)
        {
            if (type == AVMEDIA_TYPE_AUDIO)
            {
                int bytesPerSample = av_get_bytes_per_sample((AVSampleFormat)frame->format);
                for (int i = 0; i < frame->nb_samples; ++i)
                {
                    for (int ch = 0; ch < spec.channels; ++ch)
                    {
                        SDL_QueueAudio(audioDeviceId, frame->data[ch] + i * bytesPerSample, bytesPerSample);
                        audioPcmOutFile.write((const char *)frame->data[ch] + i * bytesPerSample, bytesPerSample);
                    }
                }
            }
        };

        auto spin = std::make_unique<spinner::spinner>(41);
        spin->start();

        std::function<void(const AVMediaType type, AVFrame *frame)> onDecoderExit = [&](const AVMediaType type, AVFrame *frame)
        {
            spin->stop();
            LOG_INFO("Dumped pcm data to file: %s", audioPcmOutFilename.c_str());
            audioPcmOutFile.close();
        };

        auto onWindowLoop = [&]() {

        };

        Window::instance().init(PLAYER_WINDOW_WIDTH, PLAYER_WINDOW_HEIGHT,
                                decoder.getVideoWidth(), decoder.getVideoHeight());

        if ((ec = Window::instance().openAudio(spec)) < 2)
        {
            Window::instance().destroy();
            return ec;
        }
        else
            audioDeviceId = ec;

        decoder.start(onReceiveFrame, onDecoderExit);
        Window::instance().loop(onWindowLoop);
        Window::instance().closeAudio();
        Window::instance().destroy();
        decoder.stop();
        return ec;
    }

    int Player::play(const std::string_view &file)
    {
        int ec = 0;

        Decoder decoder(Decoder::INIT_VIDEO | Decoder::INIT_AUDIO, file);

        SDL_AudioSpec spec;
        spec.freq = decoder.getAudioSampleRate();
        spec.format = ffAudioFmt2SdlAudioFmt(decoder.getAudioSampleFormat());
        spec.channels = decoder.getAudioChannels();
        spec.silence = 0;
        spec.samples = 4096;
        spec.callback = nullptr;
        spec.userdata = nullptr;

        SDL_AudioDeviceID audioDeviceId;

        std::function<void(const AVMediaType type, AVFrame *frame)> onReceiveFrame = [&](const AVMediaType type, AVFrame *frame)
        {
            if (type == AVMEDIA_TYPE_VIDEO)
            {
                AVFrame *tmp = av_frame_alloc();
                av_frame_ref(tmp, frame);
                std::lock_guard<std::mutex> lock(_videoFrameQueueMutex);
                _videoFrameQueue.push(tmp);
            }
            else if (type == AVMEDIA_TYPE_AUDIO)
            {
                int bytesPerSample = av_get_bytes_per_sample((AVSampleFormat)frame->format);
                for (int i = 0; i < frame->nb_samples; ++i)
                {
                    for (int ch = 0; ch < spec.channels; ++ch)
                    {
                        SDL_QueueAudio(audioDeviceId, (const char *)frame->data[ch] + i * bytesPerSample, bytesPerSample);
                    }
                }
            }
        };

        std::function<void(const AVMediaType type, AVFrame *frame)> onDecoderExit = [&](const AVMediaType type, AVFrame *frame)
        {
            return;
        };

        AVRational videoStreamTimebase = decoder.getStreamTimebase(AVMEDIA_TYPE_VIDEO);

        auto onWindowLoop = [&]()
        {
            AVFrame *frame;
            static int frameLost = 0;
            static int totalFrame = 0;

            // render video frame
            {
                std::lock_guard<std::mutex> lock(_videoFrameQueueMutex);
                if (_videoFrameQueue.empty())
                    return;
                if (_videoFrameQueue.size() > 10)
                    decoder.pause();
                else if (decoder.isPaused())
                    decoder.resume();

                frame = _videoFrameQueue.front();
                _videoFrameQueue.pop();
                totalFrame++;
            }

            static double syncThreshold = 0.009;
            static int64_t lastPts = 0;
            static double videoStartTime = -1;

            if (frame->pts == 0)
            {
                videoStartTime = av_gettime_relative() / 1000000.0;
                Window::instance().videoRefresh(frame->data[0], frame->linesize[0],
                                                frame->data[1], frame->linesize[1],
                                                frame->data[2], frame->linesize[2]);
                Window::instance().render();
            }
            else
            {
                bool shouldDropFrame = false;
                double currentTime = av_gettime_relative() / 1000000.0 - videoStartTime;
                double pts = frame->pts * (double)videoStreamTimebase.num / videoStreamTimebase.den;
                double diff = pts - currentTime;
                double delay = (frame->pts - lastPts) * (double)videoStreamTimebase.num / videoStreamTimebase.den;
                lastPts = frame->pts;
                // too late, drop frame and rebase timestamp
                if (diff <= -syncThreshold)
                {
                    // videoStartTime = av_gettime_relative() / 1000000.0;
                    if (diff > -delay)
                    {
                        delay += diff;
                    }
                    else
                    {
                        shouldDropFrame = true;
                        frameLost++;
                    }
                }

                // too early, wait some proper time
                if (diff >= syncThreshold)
                {
                    delay = std::min(delay * 2, delay + diff);
                    currentTime = av_gettime_relative() / 1000000.0 - videoStartTime;
                    diff = pts - currentTime;
                }

                // render frame if not too early or too late
                if (!shouldDropFrame)
                {
                    int64_t beforeRenderTime = av_gettime_relative();
                    Window::instance().videoRefresh(frame->data[0], frame->linesize[0],
                                                    frame->data[1], frame->linesize[1],
                                                    frame->data[2], frame->linesize[2]);
                    auto frameInfo = Logger::instance().format("pts: %.5f, diff: %.3fms, elapsed time: %.2fs, loss: %.2f%", pts, diff * 1000, currentTime, (float)frameLost / totalFrame * 100);
                    Window::instance().addText(frameInfo, 0, 0, {255, 255, 255, 255});
                    Window::instance().render();
                    int64_t renderTime = av_gettime_relative() - beforeRenderTime;
                    av_usleep((unsigned int)(delay * 1000000 - renderTime));
                }
            }

            av_frame_unref(frame);
        };

        Window::instance().init(PLAYER_WINDOW_WIDTH, PLAYER_WINDOW_HEIGHT,
                                decoder.getVideoWidth(), decoder.getVideoHeight());
        if ((ec = Window::instance().openAudio(spec)) < 2)
        {
            Window::instance().destroy();
            return ec;
        }
        else
            audioDeviceId = ec;

        decoder.start(onReceiveFrame, onDecoderExit);
        Window::instance().loop(onWindowLoop);
        Window::instance().destroy();
        decoder.stop();
        return ec;

        return ec;
    }
}
