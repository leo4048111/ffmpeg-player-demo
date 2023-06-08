#include "Player.hxx"
#include "Logger.hxx"
#include "Utils.hxx"
namespace fpd
{
    Player::Player()
    {
        avformat_network_init();
        _av_format_ctx = avformat_alloc_context();
    }

    bool Player::getStreamInfo(const std::string_view &file)
    {
        if (avformat_open_input(&_av_format_ctx, file.data(), nullptr, nullptr) != 0)
        {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return false;
        }

        if (avformat_find_stream_info(_av_format_ctx, nullptr) < 0)
        {
            LOG_ERROR("Failed to find stream info for file: %s", file.data());
            return false;
        }

        av_dump_format(_av_format_ctx, 0, file.data(), 0);

        avformat_close_input(&_av_format_ctx);

        return true;
    }

    // avformat_open_input -> avformat_find_stream_info -> avcodec_find_decoder(optional) -> avcodec_alloc_context3 -> avcodec_parameters_to_context
    // -> avcodec_open2 -> int ret = av_read_frame -> if frame is from video stream, write to video file, otherwise write to audio file -> av_packet_unref
    bool Player::dumpDemuxer(const std::string_view &file)
    {
        if (avformat_open_input(&_av_format_ctx, file.data(), nullptr, nullptr) != 0)
        {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return false;
        }

        if (avformat_find_stream_info(_av_format_ctx, nullptr) < 0)
        {
            return false;
        }

        int videoStreamidx = -1, audioStreamidx = -1;

        const AVCodec *videoCodec = nullptr, *audioCodec = nullptr;

        videoStreamidx = av_find_best_stream(_av_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
        audioStreamidx = av_find_best_stream(_av_format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &audioCodec, 0);

        // for(int i = 0; i < _av_format_ctx->nb_streams; i++)
        // {
        //     if(_av_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamidx == -1)
        //         videoStreamidx = i;
        //     else if(_av_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamidx == -1)
        //         audioStreamidx = i;
        // }

        if (videoStreamidx == -1)
        {
            LOG_ERROR("Failed to find video stream in file: %s", file.data());
            return false;
        }
        else if (audioStreamidx == -1)
        {
            LOG_ERROR("Failed to find audio stream in file: %s", file.data());
            return false;
        }
        else if (videoCodec == nullptr)
        {
            LOG_ERROR("Failed to find video codec in file: %s", file.data());
            return false;
        }
        else if (audioCodec == nullptr)
        {
            LOG_ERROR("Failed to find audio codec in file: %s", file.data());
            return false;
        }

        AVCodecContext *videoCodecCtx = avcodec_alloc_context3(videoCodec);
        AVCodecContext *audioCodecCtx = avcodec_alloc_context3(audioCodec);

        if (videoCodecCtx == nullptr)
        {
            LOG_ERROR("Failed to allocate video codec context in file: %s", file.data());
            return false;
        }
        else if (audioCodecCtx == nullptr)
        {
            LOG_ERROR("Failed to allocate audio codec context in file: %s", file.data());
            return false;
        }

        // Get pointer to codec parameters
        AVCodecParameters *videoCodecParams = _av_format_ctx->streams[videoStreamidx]->codecpar;
        AVCodecParameters *audioCodecParams = _av_format_ctx->streams[audioStreamidx]->codecpar;

        if (avcodec_parameters_to_context(videoCodecCtx, videoCodecParams) < 0)
        {
            LOG_ERROR("Failed to copy video codec parameters to codec context in file: %s", file.data());
            return false;
        }
        else if (avcodec_parameters_to_context(audioCodecCtx, audioCodecParams) < 0)
        {
            LOG_ERROR("Failed to copy audio codec parameters to codec context in file: %s", file.data());
            return false;
        }

        if (avcodec_open2(videoCodecCtx, videoCodec, nullptr) < 0)
        {
            LOG_ERROR("Failed to open video codec in file: %s", file.data());
            return false;
        }
        else if (avcodec_open2(audioCodecCtx, audioCodec, nullptr) < 0)
        {
            LOG_ERROR("Failed to open audio codec in file: %s", file.data());
            return false;
        }

        auto filenameNoExt = Utils::getFilenameNoExt(file);
        auto videoFilename = filenameNoExt + ".h265";
        auto audioFilename = filenameNoExt + ".aac";

        FILE *videoFile = fopen(videoFilename.c_str(), "wb+");
        FILE *audioFile = fopen(audioFilename.c_str(), "wb+");

        AVPacket av_packet;

        while (int ret = av_read_frame(_av_format_ctx, &av_packet) >= 0)
        {
            if (av_packet.stream_index == videoStreamidx)
                fwrite(av_packet.data, 1, av_packet.size, videoFile);
            else if (av_packet.stream_index == audioStreamidx)
                fwrite(av_packet.data, 1, av_packet.size, audioFile);

            av_packet_unref(&av_packet);
        }

        fclose(videoFile);
        fclose(audioFile);

        LOG_INFO("Dumped video stream to file: %s", videoFilename.c_str());
        LOG_INFO("Dumped audio stream to file: %s", audioFilename.c_str());

        avcodec_close(videoCodecCtx);
        avcodec_close(audioCodecCtx);
        avcodec_free_context(&videoCodecCtx);
        avcodec_free_context(&audioCodecCtx);

        avformat_close_input(&_av_format_ctx);
        return true;
    }
}