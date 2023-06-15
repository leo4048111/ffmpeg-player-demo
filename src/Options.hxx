#pragma once

#include <vector>
#include <string>

#include "Singleton.hxx"

namespace fpd
{
    class Options
    {
        SINGLETON(Options)

        bool parse(int argc, char **argv);

    private:
    public:
        std::vector<std::string> _files;
        int _mode{0};
    };
}