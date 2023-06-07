#include <vector>
#include <string>

namespace fpd
{
    class Options
    {
    private:
        Options() = default;

    public:
        ~Options() = default;
        Options(const Options &) = delete;
        Options &operator=(const Options &) = delete;

        static Options &getInstance()
        {
            static Options instance;
            return instance;
        }

        bool parse(int argc, char **argv);
    
    private:

    public:
        std::vector<std::string> _files;
    };
}