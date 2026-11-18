#include "http/data/state/kiotty_http_client_state.h"

#include "http/data/repository/body/kiotty_http_upload_rules.h"

namespace kiotty::net::implementation
{
    HttpClientState::HttpClientState()
        : shared(Span<char>{ nullptr, 0 }, Span<KiottyNetHttpHeader>{ nullptr, 0 })
        , reply(Span<char>{ nullptr, 0 }, Span<KiottyNetHttpHeader>{ nullptr, 0 })
        , body()
        , upload(idleUpload())
        , target{}
        , head_area{ nullptr, 0 }
        , max_headers(0)
        , file_piece(0)
        , persistence(Persistence::CLOSE)
        , busy()
    {
    }

    void dropReply(HttpClientState& state)
    {
        state.reply.begin();
        state.body = HttpBodyReader();
        state.persistence = Persistence::CLOSE;
    }
}
