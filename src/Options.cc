#include "Options.hxx"

#include <ProgramOptions.hxx>

#include "Logger.hxx"

namespace fpd
{
    bool Options::parse(int argc, char **argv)
    {
        po::parser parser;
        auto &help = parser["help"]
                         .abbreviation('?')
                         .description("print this help screen");

        auto &M = parser["mode"]
                      .abbreviation('m')
                      .type(po::u32)
                      .description("player mode");

        auto &files = parser[""]
                          .callback([&](const std::string &value)
                                    { _files.push_back(value); });

        if (!parser(argc, argv) || help.was_set())
        {
            std::cout << parser << std::endl;
            return false;
        }

        if (!files.size())
        {
            LOG_ERROR("No files specified");
            return false;
        }

        for (auto &f : _files)
        {
            LOG_INFO("Input file: %s", f.c_str());
        }

        if (!M.was_set())
            LOG_WARNING("No mode specified, assuming default %d", _mode);
        else
            _mode = M.get().u32;

        LOG_INFO("Running mode: %d", _mode);

        return true;
    }
}