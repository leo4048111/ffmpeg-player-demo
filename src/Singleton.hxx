#pragma once

namespace fpd
{
#define SINGLETON(T)                  \
private:                              \
    T();                              \
                                      \
public:                               \
    ~T();                             \
    T(const T &) = delete;            \
    T &operator=(const T &) = delete; \
    static T &instance()              \
    {                                 \
        static T instance;            \
        return instance;              \
    }
}