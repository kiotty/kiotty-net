#pragma once

#include "kiotty_net_http.h"

namespace kiotty::net::implementation
{
    class HttpNotices
    {
    public:
        HttpNotices();

        void clear();
        void setDisconnected(KiottyNetHttpDisconnectedAction action,
                             KiottyNetAppContext app_context);
        void setError(KiottyNetHttpErrorAction action, KiottyNetAppContext app_context);
        void reportDisconnected(const KiottyNetConnectionInfo& info, KiottyNetCode reason) const;
        void reportError(const KiottyNetConnectionInfo& info,
                         KiottyNetCode code,
                         const char* message) const;

    private:
        KiottyNetHttpDisconnectedAction _on_disconnected;
        KiottyNetHttpErrorAction        _on_error;
        KiottyNetAppContext             _disconnected_context;
        KiottyNetAppContext             _error_context;
    };
}
