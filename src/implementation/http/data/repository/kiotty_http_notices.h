#pragma once

#include "kiotty_net_http.h"

namespace kiotty::net::implementation
{
    class HttpNotices
    {
    public:
        HttpNotices();

        void clear();
        void setDisconnected(KiottyNetHttpDisconnectedAction action);
        void setError(KiottyNetHttpErrorAction action);
        void reportDisconnected(KiottyNetAppContext app_context,
                                const KiottyNetConnectionInfo& info,
                                KiottyNetCode reason) const;
        void reportError(KiottyNetAppContext app_context,
                         const KiottyNetConnectionInfo& info,
                         KiottyNetCode code,
                         const char* message) const;

    private:
        KiottyNetHttpDisconnectedAction _on_disconnected;
        KiottyNetHttpErrorAction        _on_error;
    };
}
