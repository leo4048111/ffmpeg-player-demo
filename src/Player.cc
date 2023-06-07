#include "Player.hxx"

#include "Logger.hxx"

namespace fpd
{
    Player::Player() {
        avformat_network_init();
        _av_format_ctx = avformat_alloc_context();
    }

    bool Player::getStreamInfo(const std::string_view &file) {
        if (avformat_open_input(&_av_format_ctx, file.data(), nullptr, nullptr) != 0) {
            LOG_ERROR("Failed to open input file: %s", file.data());
            return false;
        }

        if (avformat_find_stream_info(_av_format_ctx, nullptr) < 0) {
            return false;
        }

        av_dump_format(_av_format_ctx, 0, file.data(), 0);

        return true;
    }
}