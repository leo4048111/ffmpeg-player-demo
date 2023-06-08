#include "Player.hxx"
#include "Logger.hxx"
#include "Utils.hxx"
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

    int Player::getStreamInfo(const std::string_view &file)
    {
        int ec = 0;
        AVFormatContext *avFormatCtx = avformat_alloc_context();

        if ((ec = avformat_open_input(&avFormatCtx, file.data(), nullptr, nullptr)) != 0)
        {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return ec;
        }

        if ((ec = avformat_find_stream_info(avFormatCtx, nullptr)) < 0)
        {
            LOG_ERROR("Failed to find stream info for file: %s", file.data());
            return ec;
        }

        av_dump_format(avFormatCtx, 0, file.data(), 0);

        avformat_close_input(&avFormatCtx);

        avformat_free_context(avFormatCtx);

        return ec;
    }

    int Player::dumpVideoAndAudioStream(const std::string_view &file)
    {
        int ec = 0;

        AVFormatContext *avFormatCtx = avformat_alloc_context();

        if ((ec = avformat_open_input(&avFormatCtx, file.data(), nullptr, nullptr)) != 0)
        {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return ec;
        }

        if ((ec = avformat_find_stream_info(avFormatCtx, nullptr)) < 0)
        {
            return ec;
        }

        int videoStreamidx, audioStreamidx;

        const AVCodec *videoCodec = nullptr, *audioCodec = nullptr;

        ec = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);

        if (ec < 0)
        {
            LOG_ERROR("Failed to find best video stream in file: %s", file.data());
            return ec;
        }
        else
            videoStreamidx = ec;

        ec = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &audioCodec, 0);

        if (ec < 0)
        {
            LOG_ERROR("Failed to find best audio stream in file: %s", file.data());
            return ec;
        }
        else
            audioStreamidx = ec;

        AVStream *videoInStream = avFormatCtx->streams[videoStreamidx];
        AVStream *audioInStream = avFormatCtx->streams[audioStreamidx];
        const AVCodec *videoInCodec = avcodec_find_decoder(videoInStream->codecpar->codec_id);
        const AVCodec *audioInCodec = avcodec_find_decoder(audioInStream->codecpar->codec_id);
        AVCodecContext *videoInCodecCtx = avcodec_alloc_context3(videoInCodec);
        AVCodecContext *audioInCodecCtx = avcodec_alloc_context3(audioInCodec);

        // Get pointer to codec parameters
        AVCodecParameters *videoCodecParams = videoInStream->codecpar;
        AVCodecParameters *audioCodecParams = audioInStream->codecpar;

        if ((ec = avcodec_parameters_to_context(videoInCodecCtx, videoCodecParams)) < 0)
        {
            LOG_ERROR("Failed to copy video codec parameters to input codec context in file: %s", file.data());
            return ec;
        }

        if ((ec = avcodec_parameters_to_context(audioInCodecCtx, audioCodecParams)) < 0)
        {
            LOG_ERROR("Failed to copy audio codec parameters to input codec context in file: %s", file.data());
            return ec;
        }

        if ((ec = avcodec_open2(videoInCodecCtx, videoInCodec, nullptr)) < 0)
        {
            LOG_ERROR("Failed to open video codec in file: %s", file.data());
            return ec;
        }

        if ((ec = avcodec_open2(audioInCodecCtx, audioInCodec, nullptr)) < 0)
        {
            LOG_ERROR("Failed to open audio codec in file: %s", file.data());
            return ec;
        }

        auto filenameNoExt = Utils::getFilenameNoExt(file);
        auto videoFilename = filenameNoExt + ".h265";
        auto audioFilename = filenameNoExt + ".aac";

        AVFormatContext *videoOutFormatCtx, *audioOutFormatCtx;
        if ((ec = avformat_alloc_output_context2(&videoOutFormatCtx, nullptr, nullptr, videoFilename.c_str())))
        {
            LOG_ERROR("Failed to allocate output context for video file: %s", videoFilename.c_str());
            return ec;
        }

        if ((ec = avformat_alloc_output_context2(&audioOutFormatCtx, nullptr, nullptr, audioFilename.c_str())))
        {
            LOG_ERROR("Failed to allocate output context for audio file: %s", audioFilename.c_str());
            return ec;
        }

        AVStream *videoOutStream = avformat_new_stream(videoOutFormatCtx, nullptr);
        AVStream *audioOutStream = avformat_new_stream(audioOutFormatCtx, nullptr);

        const AVOutputFormat *videoOutFormat = av_guess_format(nullptr, videoFilename.c_str(), nullptr);
        const AVOutputFormat *audioOutFormat = av_guess_format(nullptr, audioFilename.c_str(), nullptr);

        const AVCodec *videoOutCodec = avcodec_find_encoder(videoOutFormat->video_codec);
        const AVCodec *audioOutCodec = avcodec_find_encoder(audioOutFormat->audio_codec);

        AVCodecContext *videoOutCodecCtx = avcodec_alloc_context3(videoOutCodec);
        AVCodecContext *audioOutCodecCtx = avcodec_alloc_context3(audioOutCodec);
        videoOutCodecCtx->width = videoInCodecCtx->width;
        videoOutCodecCtx->height = videoInCodecCtx->height;
        videoOutCodecCtx->pix_fmt = videoOutCodec->pix_fmts[0];
        videoOutCodecCtx->time_base = videoInStream->time_base;
        ec = avcodec_open2(videoOutCodecCtx, videoOutCodec, nullptr);
        // ec = avcodec_open2(audioOutCodecCtx, audioOutCodec, nullptr);

        avcodec_parameters_from_context(videoOutStream->codecpar, videoOutCodecCtx);
        videoOutStream->time_base = videoOutCodecCtx->time_base;
        // avcodec_parameters_from_context(audioOutStream->codecpar, audioOutCodecCtx);
        // audioOutStream->time_base = audioOutCodecCtx->time_base;

        if ((ec = avformat_write_header(videoOutFormatCtx, nullptr)) < 0)
        {
            LOG_ERROR("Failed to write header to video output file: %s", videoFilename.c_str());
            return ec;
        }

        // if ((ec = avformat_write_header(audioOutFormatCtx, nullptr)) < 0)
        // {
        //     LOG_ERROR("Failed to write header to audio output file: %s", audioFilename.c_str());
        //     return ec;
        // }

        AVPacket pkt;
        AVFrame *frame = av_frame_alloc();
        SwsContext *swsCtx = sws_getContext(videoInCodecCtx->width, videoInCodecCtx->height, videoInCodecCtx->pix_fmt, videoOutCodecCtx->width, videoOutCodecCtx->height, videoOutCodecCtx->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);

        while (av_read_frame(avFormatCtx, &pkt) >= 0)
        {
            if (pkt.stream_index == videoStreamidx)
            {
                avcodec_send_packet(videoInCodecCtx, &pkt);
                while (avcodec_receive_frame(videoInCodecCtx, frame) == 0)
                {
                    AVFrame *outFrame = av_frame_alloc();
                    outFrame->format = videoOutCodecCtx->pix_fmt;
                    outFrame->width = videoInCodecCtx->width;
                    outFrame->height = videoInCodecCtx->height;
                    av_frame_get_buffer(outFrame, 0);
                    sws_scale(swsCtx, frame->data, frame->linesize, 0, videoInCodecCtx->height, outFrame->data, outFrame->linesize);

                    avcodec_send_frame(videoOutCodecCtx, outFrame);

                    while (avcodec_receive_packet(videoOutCodecCtx, &pkt) == 0)
                    {
                        pkt.stream_index = videoOutStream->index;
                        pkt.pts = av_rescale_q_rnd(pkt.pts, videoInStream->time_base, videoOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        pkt.dts = av_rescale_q_rnd(pkt.dts, videoInStream->time_base, videoOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        pkt.duration = av_rescale_q(pkt.duration, videoInStream->time_base, videoOutStream->time_base);
                        av_interleaved_write_frame(videoOutFormatCtx, &pkt);
                    }
                    av_frame_free(&outFrame);
                }
            }
            // else if(pkt.stream_index == audioStreamidx)
            // {
            //     pkt.pts = av_rescale_q_rnd(pkt.pts, avFormatCtx->streams[audioStreamidx]->time_base, audioOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            //     pkt.dts = av_rescale_q_rnd(pkt.dts, avFormatCtx->streams[audioStreamidx]->time_base, audioOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            //     pkt.duration = av_rescale_q(pkt.duration, avFormatCtx->streams[audioStreamidx]->time_base, audioOutStream->time_base);
            //     pkt.pos = -1;
            //     av_interleaved_write_frame(audioOutFormatCtx, &pkt);
            // }

            av_packet_unref(&pkt);
        }

        av_write_trailer(videoOutFormatCtx);
        // av_write_trailer(audioOutFormatCtx);

        LOG_INFO("Dumped video stream to file: %s", videoFilename.c_str());
        LOG_INFO("Dumped audio stream to file: %s", audioFilename.c_str());

        av_frame_free(&frame);
        sws_freeContext(swsCtx);
        avcodec_close(videoInCodecCtx);
        avcodec_close(audioInCodecCtx);
        avcodec_close(videoOutCodecCtx);
        avcodec_close(audioOutCodecCtx);

        avformat_close_input(&avFormatCtx);

        avformat_free_context(videoOutFormatCtx);
        avformat_free_context(audioOutFormatCtx);
        avformat_free_context(avFormatCtx);

        return ec;
    }

    int Player::lameDumpVideoAndAudioStream(const std::string_view &file)
    {
        int ec = 0;

        AVFormatContext *avFormatCtx = avformat_alloc_context();

        if ((ec = avformat_open_input(&avFormatCtx, file.data(), nullptr, nullptr)) < 0)
        {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return ec;
        }

        if ((ec = avformat_find_stream_info(avFormatCtx, nullptr)) < 0)
        {
            LOG_ERROR("Failed to find stream info for input file: %s", file.data());
            return ec;
        }

        int videoStreamidx, audioStreamidx;

        ec = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (ec < 0)
        {
            LOG_ERROR("Failed to find best video stream for input file: %s", file.data());
            return ec;
        }
        else
            videoStreamidx = ec;

        ec = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (ec < 0)
        {
            LOG_ERROR("Failed to find best audio stream for input file: %s", file.data());
            return ec;
        }
        else
            audioStreamidx = ec;

        auto filenameNoExt = Utils::getFilenameNoExt(file);
        auto videoFilename = filenameNoExt + ".h265";
        auto audioFilename = filenameNoExt + ".aac";

        FILE *videoFile = fopen(videoFilename.c_str(), "wb");
        FILE *audioFile = fopen(audioFilename.c_str(), "wb");

        AVPacket pkt;

        while (av_read_frame(avFormatCtx, &pkt) >= 0)
        {
            if (pkt.stream_index == videoStreamidx)
            {
                fwrite(pkt.data, 1, pkt.size, videoFile);
            }
            else if (pkt.stream_index == audioStreamidx)
            {
                fwrite(pkt.data, 1, pkt.size, audioFile);
            }
            av_packet_unref(&pkt);
        }

        LOG_INFO("Dumped video stream to file: %s", videoFilename.c_str());
        LOG_INFO("Dumped audio stream to file: %s", audioFilename.c_str());

        fclose(videoFile);
        fclose(audioFile);

        avformat_free_context(avFormatCtx);

        return ec;
    }
}