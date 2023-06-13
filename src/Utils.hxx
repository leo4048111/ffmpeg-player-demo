#pragma once

#include <string>

namespace fpd
{
    class Utils
    {
        Utils() = delete;
        Utils(Utils &&) = delete;
        Utils(const Utils &) = delete;

    public:
        static std::string getFilenameNoExt(const std::string_view &filename)
        {
            size_t lastindex = filename.find_last_of(".");
            return std::string(filename.substr(0, lastindex));
        }
    };
}