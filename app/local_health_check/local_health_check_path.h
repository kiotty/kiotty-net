#ifndef KIOTTY_LOCAL_HEALTH_CHECK_PATH_H
#define KIOTTY_LOCAL_HEALTH_CHECK_PATH_H

namespace local_health_check
{
#if defined(_WIN32)
    const char* const kDefaultPath = "kiotty_health_check.sock";
#else
    const char* const kDefaultPath = "/tmp/kiotty_health_check.sock";
#endif
}

#endif
