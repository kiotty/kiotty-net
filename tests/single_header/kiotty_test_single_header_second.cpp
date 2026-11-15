#include "kiotty_test_single_header_names.h"

bool kiottyTestSingleHeaderSecond()
{
    smoke::HttpLimits limits;
    if (smoke::initializeHttpLimits(&limits) != smoke::SUCCESS)
    {
        return false;
    }
    const smoke::Code built = smoke::HttpLimitsBuilder(limits).maxRoutes(4).build();
    smoke::HttpHeaders<2> headers;
    headers.add("Accept", "text/plain").add("X-Unit", "second");
    smoke::Address address;
    const smoke::Code addressed = smoke::initializeTcpAddress(&address, "127.0.0.1", 8080);
    return built == smoke::SUCCESS && headers.code() == smoke::SUCCESS && headers.count() == 2
           && addressed == smoke::SUCCESS && address.port == 8080;
}
