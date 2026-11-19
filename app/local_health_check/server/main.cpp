#include "health_check_server_app.h"
#include "local_health_check_path.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    const char* path = local_health_check::kDefaultPath;
    if (argc > 1)
    {
        path = argv[1];
    }
    std::remove(path);
    KiottyNetAddress address;
    KiottyNetCode    code = KiottyNet_initializePipeAddress(&address, path);
    if (code != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "bad pipe path, code %d\n", static_cast<int>(code));
        return 1;
    }
    std::string where = std::string("pipe ") + path;
    int         result = health_check::runServerApp(address, where);
    std::remove(path);
    return result;
}
