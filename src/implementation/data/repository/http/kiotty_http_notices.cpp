#include "data/repository/http/kiotty_http_notices.h"

namespace kiotty::net::implementation
{
    HttpNotices::HttpNotices()
        : _on_disconnected(nullptr)
        , _on_error(nullptr)
        , _disconnected_context(nullptr)
        , _error_context(nullptr)
    {
    }

    void HttpNotices::clear()
    {
        setDisconnected(nullptr, nullptr);
        setError(nullptr, nullptr);
    }

    void HttpNotices::setDisconnected(KiottyNetHttpDisconnectedAction action,
                                      KiottyNetAppContext app_context)
    {
        _on_disconnected = action;
        _disconnected_context = app_context;
    }

    void HttpNotices::setError(KiottyNetHttpErrorAction action, KiottyNetAppContext app_context)
    {
        _on_error = action;
        _error_context = app_context;
    }

    void HttpNotices::reportDisconnected(const KiottyNetConnectionInfo& info,
                                         KiottyNetCode reason) const
    {
        if (_on_disconnected == nullptr) { return; }
        _on_disconnected(_disconnected_context, info, reason);
    }

    void HttpNotices::reportError(const KiottyNetConnectionInfo& info,
                                  KiottyNetCode code,
                                  const char* message) const
    {
        if (_on_error == nullptr) { return; }
        _on_error(_error_context, info, code, message);
    }
}
