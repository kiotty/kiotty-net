#include "data/repository/http/kiotty_http_session_pool.h"

#include "core/kiotty_checked.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    namespace
    {
        struct SessionLayout
        {
            size_t header;
            size_t body;
            size_t response;
            size_t total;
        };

        using LayoutResult = Result<SessionLayout, KiottyNetCode>;
        using SizeResult = Result<size_t, KiottyNetCode>;

        LayoutResult   layoutOf(const KiottyNetHttpLimits& limits);
        SizeResult     totalOf(const SessionLayout& layout, size_t count);
        SessionBuffers buffersAt(Span<char> block, const SessionLayout& layout, size_t index);
    }

    HttpSessionPool::HttpSessionPool()
        : _block()
        , _headers()
        , _sessions()
        , _free()
        , _generations()
        , _mutex()
    {
    }

    KiottyNetCode HttpSessionPool::initialize(HttpSite& site,
                                              size_t count,
                                              const KiottyNetHttpLimits& limits)
    {
        const LayoutResult layout = layoutOf(limits);
        if (!layout) { return layout.code; }
        const SizeResult total = totalOf(layout.value, count);
        if (!total) { return total.code; }
        const SizeResult header_slots = checkedMultiply<size_t>(count, limits.max_headers);
        if (!header_slots) { return header_slots.code; }
        if (_block.size() < total.value) { _block.resize(total.value); }
        if (_headers.size() < header_slots.value) { _headers.resize(header_slots.value); }
        keepGenerations();
        _sessions.clear();
        _sessions.reserve(count);
        _free.clear();
        _free.reserve(count);
        const Span<char> block{ _block.data(), total.value };
        for (size_t i = 0; i < count; ++i)
        {
            SessionBuffers buffers = buffersAt(block, layout.value, i);
            buffers.headers = Span<KiottyNetHttpHeader>{ _headers.data() + i * limits.max_headers,
                                                         limits.max_headers };
            _sessions.emplace_back(site, static_cast<int32_t>(i), buffers);
            const bool has_history = i < _generations.size();
            _sessions.back().id.generation = has_history ? _generations[i] : 0;
            _free.push_back(static_cast<int32_t>(count - 1 - i));
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    SessionResult HttpSessionPool::acquire(const KiottyNetConnectionInfo& info)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (_free.empty()) { return SessionResult::failure(KIOTTY_NET_CODE_SESSION_EXHAUSTED); }
        const int32_t index = _free.back();
        _free.pop_back();
        HttpSession& session = _sessions[static_cast<size_t>(index)];
        session.info = info;
        return SessionResult::success(session);
    }

    void HttpSessionPool::release(HttpSession& session)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        session.is_orphaned = session.is_worker_owned;
        if (session.is_orphaned) { return; }
        session.id.generation = nextSessionGeneration(session.id.generation);
        _free.push_back(session.id.index);
    }

    bool HttpSessionPool::handBack(HttpSession& session)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        session.is_worker_owned = false;
        return session.is_orphaned;
    }

    SessionResult HttpSessionPool::find(int64_t id)
    {
        const PoolHandle handle = PoolHandle::fromValue(id);
        const size_t index = static_cast<size_t>(handle.index);
        const bool is_inside = handle.index >= 0 && index < _sessions.size();
        if (!is_inside) { return SessionResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
        HttpSession& session = _sessions[index];
        if (session.id.generation != handle.generation)
        {
            return SessionResult::failure(KIOTTY_NET_CODE_PEER_CLOSED);
        }
        return SessionResult::success(session);
    }

    void HttpSessionPool::keepGenerations()
    {
        if (_generations.size() < _sessions.size()) { _generations.resize(_sessions.size(), 0); }
        for (size_t i = 0; i < _sessions.size(); ++i)
        {
            _generations[i] = nextSessionGeneration(_sessions[i].id.generation);
        }
    }

    int32_t nextSessionGeneration(int32_t generation)
    {
        if (generation == INT32_MAX) { return 1; }
        return generation + 1;
    }

    namespace
    {
        LayoutResult layoutOf(const KiottyNetHttpLimits& limits)
        {
            const size_t header = limits.max_header_bytes;
            const SizeResult with_body = checkedAdd<size_t>(header, limits.max_body_bytes);
            if (!with_body) { return LayoutResult::failure(with_body.code); }
            const SizeResult total = checkedAdd<size_t>(with_body.value, limits.response_bytes);
            if (!total) { return LayoutResult::failure(total.code); }
            const SessionLayout layout{ limits.max_header_bytes,
                                        limits.max_body_bytes,
                                        limits.response_bytes,
                                        total.value };
            return LayoutResult::success(layout);
        }

        SizeResult totalOf(const SessionLayout& layout, size_t count)
        {
            return checkedMultiply<size_t>(layout.total, count);
        }

        SessionBuffers buffersAt(Span<char> block, const SessionLayout& layout, size_t index)
        {
            const Span<char> own = block.skip(index * layout.total);
            const Span<char> header{ own.data, layout.header };
            const Span<char> body{ own.data + layout.header, layout.body };
            const Span<char> response{ own.data + layout.header + layout.body, layout.response };
            const Span<KiottyNetHttpHeader> no_headers{ nullptr, 0 };
            return SessionBuffers{ header, no_headers, body, response };
        }
    }
}
