#include "Options.hxx"
#include "Player.hxx"
#include "Logger.hxx"

#undef main

int main(int argc, char **argv)
{
    if (!fpd::Options::instance().parse(argc, argv))
        return 0;

    int ec = 0;

    fpd::Player &player = fpd::Player::instance();

    LOG_INFO("Player task: %s", player.getPlayerModeName(fpd::Options::instance()._mode).data());

    switch (fpd::Options::instance()._mode)
    {
    case 0: // get stream infos
        for (auto &f : fpd::Options::instance()._files)
        {
            LOG_INFO("Get stream infos for file: %s", f.c_str());
            if ((ec = player.getStreamInfo(f)))
                return ec;
        }
        break;
    case 1:
        for (auto &f : fpd::Options::instance()._files)
        {
            LOG_INFO("Dump H.264/265 and acc streams for file: %s", f.c_str());
            if ((ec = player.dumpH264AndAACFromVideoFile(f)))
                return ec;
        }
        break;
    case 2:
        for (auto &f : fpd::Options::instance()._files)
        {
            LOG_INFO("Dump video yuv data for file: %s", f.c_str());
            if ((ec = player.dumpYUVAndPlayVideoStream(f)))
                return ec;
        }
        break;
    case 3:
        for (auto &f : fpd::Options::instance()._files)
        {
            LOG_INFO("Dump audio pcm data for file: %s", f.c_str());
            if ((ec = player.dumpPCMAndPlayAudioStream(f)))
                return ec;
        }
    case 4:
        for (auto &f : fpd::Options::instance()._files)
        {
            LOG_INFO("Playing: %s", f.c_str());
            if ((ec = player.play(f)))
                return ec;
        }
    default:
        break;
    }

    return ec;
}