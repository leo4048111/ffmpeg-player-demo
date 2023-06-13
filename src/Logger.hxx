#pragma once

#include <iostream>
#include <string>
#include <mutex>
#include <memory>

namespace fpd
{
    class Logger
    {
    public:
        enum class LogLevel
        {
            FPD_LOG_INFO,
            FPD_LOG_WARNING,
            FPD_LOG_ERROR
        };

        static Logger &instance()
        {
            static Logger instance;
            return instance;
        }

        ~Logger() = default;
        Logger(Logger const &) = delete;
        void operator=(Logger const &) = delete;

    private:
        Logger() = default;

    public:
        void log(LogLevel level, const std::string_view &message)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            switch (level)
            {
            case LogLevel::FPD_LOG_INFO:
                std::cout << "\033[32m[INFO]\033[0m " << message << std::endl;
                break;
            case LogLevel::FPD_LOG_WARNING:
                std::cout << "\033[33m[WARNING]\033[0m " << message << std::endl;
                break;
            case LogLevel::FPD_LOG_ERROR:
                std::cerr << "\033[31m[ERROR]\033[0m " << message << std::endl;
                break;
            default:
                std::cerr << "\033[30m[UNKNOWN]\033[0m " << message << std::endl;
            }
        }

        template <typename... Args>
        static std::string format(const std::string_view &fmt, Args... args)
        {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
            // Compute the size we will need.
            size_t size = snprintf(nullptr, 0, fmt.data(), args...) + 1; // Extra space for '\0'

            if (size <= 0)
            {
                throw std::runtime_error("Error during formatting.");
            }

            std::unique_ptr<char[]> buf(new char[size]);
            snprintf(buf.get(), size, fmt.data(), args...);
#pragma GCC diagnostic pop
            return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
        }

    private:
        std::mutex mutex_;
    };
}

#define LOG_INFO(fmt, ...)                                                                                       \
    do                                                                                                           \
    {                                                                                                            \
        fpd::Logger::instance().log(fpd::Logger::LogLevel::FPD_LOG_INFO, fpd::Logger::format(fmt, ##__VA_ARGS__)); \
    } while (0)
#define LOG_WARNING(fmt, ...)                                                                                       \
    do                                                                                                              \
    {                                                                                                               \
        fpd::Logger::instance().log(fpd::Logger::LogLevel::FPD_LOG_WARNING, fpd::Logger::format(fmt, ##__VA_ARGS__)); \
    } while (0)
#define LOG_ERROR(fmt, ...)                                                                                       \
    do                                                                                                            \
    {                                                                                                             \
        fpd::Logger::instance().log(fpd::Logger::LogLevel::FPD_LOG_ERROR, fpd::Logger::format(fmt, ##__VA_ARGS__)); \
    } while (0)
