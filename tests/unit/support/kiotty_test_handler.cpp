#include "support/kiotty_test_handler.h"

#include <cassert>
#include <cstring>

namespace kiotty_test
{
    namespace
    {
        Record blank(Call call)
        {
            Record entry;
            entry.call = call;
            entry.app_context = nullptr;
            entry.connection_context = nullptr;
            entry.handle.owner = nullptr;
            entry.handle.id = 0;
            entry.stream.owner = nullptr;
            entry.stream.id = 0;
            entry.length = 0;
            entry.code = KIOTTY_NET_CODE_SUCCESS;
            entry.message = nullptr;
            return entry;
        }

        KiottyNetConnectionAction action(KiottyNetActionType type, uint32_t timeout_milliseconds)
        {
            KiottyNetConnectionAction result;
            std::memset(&result, 0, sizeof(result));
            result.type = type;
            result.timeout_milliseconds = timeout_milliseconds;
            return result;
        }
    }

    KiottyNetConnectionAction thenHold(uint32_t timeout_milliseconds)
    {
        return action(KIOTTY_NET_ACTION_TYPE_HOLD, timeout_milliseconds);
    }

    KiottyNetConnectionAction thenRead(size_t exact, uint32_t timeout_milliseconds)
    {
        KiottyNetConnectionAction result = action(KIOTTY_NET_ACTION_TYPE_READ, timeout_milliseconds);
        result.exact = exact;
        return result;
    }

    KiottyNetConnectionAction thenWork()
    {
        return action(KIOTTY_NET_ACTION_TYPE_START_WORK, 0);
    }

    KiottyNetConnectionAction thenClose(KiottyNetCode reason)
    {
        KiottyNetConnectionAction result = action(KIOTTY_NET_ACTION_TYPE_CLOSE, 0);
        result.reason = reason;
        return result;
    }

    KiottyNetConnectionAction thenWriteSpan(KiottyNetSpan data, uint32_t timeout_milliseconds)
    {
        KiottyNetConnectionAction result = action(KIOTTY_NET_ACTION_TYPE_WRITE, timeout_milliseconds);
        result.data = data;
        return result;
    }

    KiottyNetConnectionAction thenReadUntilSpan(KiottyNetSpan delimiter, size_t max, uint32_t timeout_milliseconds)
    {
        KiottyNetConnectionAction result = action(KIOTTY_NET_ACTION_TYPE_READ_UNTIL, timeout_milliseconds);
        result.delimiter = delimiter;
        result.max = max;
        return result;
    }

    KiottyNetSpan spanOf(const std::string& text)
    {
        KiottyNetSpan result = { text.data(), text.size() };
        return result;
    }

    Recorder::Recorder()
        : _calls()
        , _script()
        , _buffers()
    {
    }

    KiottyNetConnectionHandler Recorder::callbacks()
    {
        KiottyNetConnectionHandler table;
        table.onConnected = &Recorder::onConnected;
        table.onReady = &Recorder::onReady;
        table.onRead = &Recorder::onRead;
        table.onWritten = &Recorder::onWritten;
        table.onTimeout = &Recorder::onTimeout;
        table.onWorkStarted = &Recorder::onWorkStarted;
        table.onDisconnected = &Recorder::onDisconnected;
        table.onError = &Recorder::onError;
        return table;
    }

    void Recorder::then(KiottyNetConnectionAction next_action)
    {
        _script.push_back(next_action);
    }

    void Recorder::thenWrite(const std::string& data, uint32_t timeout_milliseconds)
    {
        _buffers.push_back(data);
        then(thenWriteSpan(spanOf(_buffers.back()), timeout_milliseconds));
    }

    void Recorder::thenReadUntil(const std::string& delimiter, size_t max, uint32_t timeout_milliseconds)
    {
        _buffers.push_back(delimiter);
        then(thenReadUntilSpan(spanOf(_buffers.back()), max, timeout_milliseconds));
    }

    size_t Recorder::count(Call call) const
    {
        size_t total = 0;
        for (const Record& entry : _calls)
        {
            if (entry.call == call)
            {
                total += 1;
            }
        }
        return total;
    }

    size_t Recorder::total() const
    {
        return _calls.size();
    }

    const Record& Recorder::last(Call call) const
    {
        for (size_t i = _calls.size(); i > 0; --i)
        {
            if (_calls[i - 1].call == call)
            {
                return _calls[i - 1];
            }
        }
        static const Record missing = blank(Call::ON_CONNECTED);
        assert(false && "no such call was recorded");
        return missing;
    }

    const Record& Recorder::at(size_t index) const
    {
        return _calls.at(index);
    }

    void Recorder::clear()
    {
        _calls.clear();
    }

    Recorder& Recorder::of(void* context)
    {
        return *static_cast<Recorder*>(context);
    }

    KiottyNetConnectionAction Recorder::next()
    {
        if (_script.empty())
        {
            return thenHold(0);
        }
        KiottyNetConnectionAction front = _script.front();
        _script.pop_front();
        return front;
    }

    void Recorder::record(const Record& entry)
    {
        _calls.push_back(entry);
    }

    KiottyNetConnectionDecision Recorder::onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        (void)info;
        Record entry = blank(Call::ON_CONNECTED);
        entry.app_context = app_context;
        of(app_context).record(entry);
        KiottyNetConnectionDecision decision = { true, app_context };
        return decision;
    }

    KiottyNetConnectionAction Recorder::onReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        Record entry = blank(Call::ON_READY);
        entry.connection_context = context;
        entry.handle = connection;
        of(context).record(entry);
        return of(context).next();
    }

    KiottyNetConnectionAction Recorder::onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        Record entry = blank(Call::ON_READ);
        entry.connection_context = context;
        entry.handle = connection;
        entry.data.assign(data.data, data.length);
        entry.length = data.length;
        of(context).record(entry);
        return of(context).next();
    }

    KiottyNetConnectionAction Recorder::onWritten(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, size_t length)
    {
        Record entry = blank(Call::ON_WRITTEN);
        entry.connection_context = context;
        entry.handle = connection;
        entry.length = length;
        of(context).record(entry);
        return of(context).next();
    }

    KiottyNetConnectionAction Recorder::onTimeout(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        Record entry = blank(Call::ON_TIMEOUT);
        entry.connection_context = context;
        entry.handle = connection;
        of(context).record(entry);
        return of(context).next();
    }

    void Recorder::onWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream)
    {
        Record entry = blank(Call::ON_WORK_STARTED);
        entry.connection_context = context;
        entry.stream = stream;
        of(context).record(entry);
    }

    void Recorder::onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetCode reason)
    {
        Record entry = blank(Call::ON_DISCONNECTED);
        entry.app_context = app_context;
        entry.connection_context = context;
        entry.handle = connection;
        entry.code = reason;
        of(app_context).record(entry);
    }

    void Recorder::onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        (void)info;
        Record entry = blank(Call::ON_ERROR);
        entry.app_context = app_context;
        entry.code = code;
        entry.message = message;
        of(app_context).record(entry);
    }

    KiottyNetCode FakeEndpoint::notify(int64_t, KiottyNetSpan)
    {
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    KiottyNetConnectionInfo FakeEndpoint::info(int64_t) const
    {
        return KiottyNetConnectionInfo();
    }

    KiottyNetResult FakeEndpoint::readStream(int64_t, kiotty::net::implementation::Span<char>, uint32_t)
    {
        KiottyNetResult result = { 0, KIOTTY_NET_CODE_INVALID_ARGUMENT };
        return result;
    }

    KiottyNetResult FakeEndpoint::writeStream(int64_t, KiottyNetSpan, uint32_t)
    {
        KiottyNetResult result = { 0, KIOTTY_NET_CODE_INVALID_ARGUMENT };
        return result;
    }

    KiottyNetCode FakeEndpoint::writeStreamFile(int64_t, kiotty::net::implementation::Span<const char>, FileRange, uint32_t)
    {
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    bool FakeEndpoint::isStreamOpen(int64_t)
    {
        return false;
    }

    KiottyNetCode FakeEndpoint::closeStream(int64_t, const KiottyNetConnectionAction&)
    {
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }
}
