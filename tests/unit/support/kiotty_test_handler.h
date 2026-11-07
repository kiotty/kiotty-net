#if !defined(KIOTTY_TEST_HANDLER_H)
#define KIOTTY_TEST_HANDLER_H

#include "context/kiotty_owner.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace kiotty_test
{
    enum class Call
    {
        ON_CONNECTED,
        ON_READY,
        ON_READ,
        ON_WRITTEN,
        ON_TIMEOUT,
        ON_WORK_STARTED,
        ON_DISCONNECTED,
        ON_ERROR
    };

    struct Record
    {
        Call                      call;
        void*                     app_context;
        void*                     connection_context;
        KiottyNetConnectionHandle handle;
        KiottyNetConnectionStream stream;
        std::string               data;
        size_t                    length;
        KiottyNetCode             code;
        const char*               message;
    };

    class Recorder
    {
    public:
        Recorder();

        static KiottyNetConnectionHandler callbacks();

        void then(KiottyNetConnectionAction action);
        void thenWrite(const std::string& data, uint32_t timeout_milliseconds);
        void thenReadUntil(const std::string& delimiter, size_t max, uint32_t timeout_milliseconds);

        size_t        count(Call call) const;
        size_t        total() const;
        const Record& last(Call call) const;
        const Record& at(size_t index) const;
        void          clear();

    private:
        static Recorder& of(void* context);
        KiottyNetConnectionAction next();
        void record(const Record& entry);

        static KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info);
        static KiottyNetConnectionAction   onReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection);
        static KiottyNetConnectionAction   onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data);
        static KiottyNetConnectionAction   onWritten(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, size_t length);
        static KiottyNetConnectionAction   onTimeout(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection);
        static void onWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream);
        static void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetCode reason);
        static void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message);

        std::vector<Record>                   _calls;
        std::deque<KiottyNetConnectionAction> _script;
        std::deque<std::string>               _buffers;
    };

    class FakeOwner : public kiotty::net::implementation::Owner
    {
    public:
        KiottyNetCode           notify(int64_t id, KiottyNetSpan data) override;
        KiottyNetConnectionInfo info(int64_t id) const override;
        KiottyNetResult         readStream(int64_t id, kiotty::net::implementation::Span<char> buffer, uint32_t timeout_milliseconds) override;
        KiottyNetResult         writeStream(int64_t id, KiottyNetSpan data, uint32_t timeout_milliseconds) override;
        KiottyNetCode           writeStreamFile(int64_t id, const char* path, FileRange range, uint32_t timeout_milliseconds) override;
        bool                    isStreamOpen(int64_t id) override;
        KiottyNetCode           closeStream(int64_t id, const KiottyNetConnectionAction& then) override;
    };

    KiottyNetConnectionAction thenHold(uint32_t timeout_milliseconds);
    KiottyNetConnectionAction thenRead(size_t exact, uint32_t timeout_milliseconds);
    KiottyNetConnectionAction thenWork();
    KiottyNetConnectionAction thenClose(KiottyNetCode reason);
    KiottyNetConnectionAction thenWriteSpan(KiottyNetSpan data, uint32_t timeout_milliseconds);
    KiottyNetConnectionAction thenReadUntilSpan(KiottyNetSpan delimiter, size_t max, uint32_t timeout_milliseconds);
    KiottyNetSpan             spanOf(const std::string& text);
}

#endif
