#ifndef KIOTTY_HEALTH_CHECK_CONSOLE_H
#define KIOTTY_HEALTH_CHECK_CONSOLE_H

#include <cstdio>
#include <mutex>
#include <string>

namespace health_check
{
    inline std::mutex& printLock()
    {
        static std::mutex lock;
        return lock;
    }

    inline void printLine(const std::string& line)
    {
        std::lock_guard<std::mutex> guard(printLock());
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
}

#endif
