#include "health_check_server_app.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    const char* host = "127.0.0.1";
    uint16_t    port = 9100;
    if (argc > 1)
    {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    if (argc > 2)
    {
        host = argv[2];
    }
    KiottyNetAddress address;
    KiottyNetCode    code = KiottyNet_initializeTcpAddress(&address, host, port);
    if (code != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "bad address, code %d\n", static_cast<int>(code));
        return 1;
    }
    std::string where = std::string("tcp ") + host + ":" + std::to_string(port);
    return health_check::runServerApp(address, where);
}
