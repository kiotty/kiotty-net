#include "data/repository/http/kiotty_http_notices.h"

namespace kiotty::net::implementation
{
    HttpNotices::HttpNotices()
        : _on_disconnected(nullptr)
        , _on_error(nullptr)
    {
    }

    void HttpNotices::clear()
    {
        setDisconnected(nullptr);
        setError(nullptr);
    }

    void HttpNotices::setDisconnected(KiottyNetHttpDisconnectedAction action)
    {
        _on_disconnected = action;
    }

    void HttpNotices::setError(KiottyNetHttpErrorAction action)
    {
        _on_error = action;
    }

    void HttpNotices::reportDisconnected(KiottyNetAppContext app_context,
                                         const KiottyNetConnectionInfo& info,
                                         KiottyNetCode reason) const
    {
        if (_on_disconnected == nullptr) { return; }
        _on_disconnected(app_context, info, reason);
    }

    void HttpNotices::reportError(KiottyNetAppContext app_context,
                                  const KiottyNetConnectionInfo& info,
                                  KiottyNetCode code,
                                  const char* message) const
    {
        if (_on_error == nullptr) { return; }
        _on_error(app_context, info, code, message);
    }
}
