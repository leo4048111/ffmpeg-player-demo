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

        // Get pointer to codec parameters
        AVCodecParameters *videoCodecParams = avFormatCtx->streams[videoStreamidx]->codecpar;
        AVCodecParameters *audioCodecParams = avFormatCtx->streams[audioStreamidx]->codecpar;

        auto filenameNoExt = Utils::getFilenameNoExt(file);
        auto videoFilename = filenameNoExt + ".h265";
        auto audioFilename = filenameNoExt + ".aac";

        AVFormatContext *videoOutFormatCtx = avformat_alloc_context();
        AVFormatContext *audioOutFormatCtx = avformat_alloc_context();

        const AVOutputFormat *videoOutFormat = av_guess_format(nullptr, videoFilename.c_str(), nullptr);
        const AVOutputFormat *audioOutFormat = av_guess_format(nullptr, audioFilename.c_str(), nullptr);

        videoOutFormatCtx->oformat = videoOutFormat;
        audioOutFormatCtx->oformat = audioOutFormat;

        AVStream *videoOutStream = avformat_new_stream(videoOutFormatCtx, nullptr);
        AVStream *audioOutStream = avformat_new_stream(audioOutFormatCtx, nullptr);

        if ((ec = avcodec_parameters_copy(videoOutStream->codecpar, videoCodecParams)) < 0)
        {
            LOG_ERROR("Failed to copy video codec parameters to output stream in file: %s", file.data());
            return ec;
        }

        if ((ec = avcodec_parameters_copy(audioOutStream->codecpar, audioCodecParams)) < 0)
        {
            LOG_ERROR("Failed to copy audio codec parameters to output stream in file: %s", file.data());
            return ec;
        }

        // videoOutStream->codecpar->codec_tag = 0;
        // audioOutStream->codecpar->codec_tag = 0;

        if ((ec = avio_open(&videoOutFormatCtx->pb, videoFilename.c_str(), AVIO_FLAG_WRITE)) < 0)
        {
            LOG_ERROR("Failed to open video output file: %s", videoFilename.c_str());
            return ec;
        }

        if ((ec = avio_open(&audioOutFormatCtx->pb, audioFilename.c_str(), AVIO_FLAG_WRITE)) < 0)
        {
            LOG_ERROR("Failed to open audio output file: %s", audioFilename.c_str());
            return ec;
        }

        AVPacket pkt;

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

        while (av_read_frame(avFormatCtx, &pkt) >= 0)
        {
            if (pkt.stream_index == videoStreamidx)
            {
                pkt.pts = av_rescale_q_rnd(pkt.pts, avFormatCtx->streams[videoStreamidx]->time_base, videoOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                pkt.dts = av_rescale_q_rnd(pkt.dts, avFormatCtx->streams[videoStreamidx]->time_base, videoOutStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                pkt.duration = av_rescale_q(pkt.duration, avFormatCtx->streams[videoStreamidx]->time_base, videoOutStream->time_base);
                pkt.pos = -1;
                av_interleaved_write_frame(videoOutFormatCtx, &pkt);
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

        avformat_close_input(&avFormatCtx);
        avio_close(videoOutFormatCtx->pb);
        avio_close(audioOutFormatCtx->pb);
        avformat_free_context(avFormatCtx);

        return ec;
    }
}