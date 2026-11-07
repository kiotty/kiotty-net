#if !defined(KIOTTY_TEST_COMPLETION_H)
#define KIOTTY_TEST_COMPLETION_H

#include "data/repository/connection/kiotty_connection.h"
#include "data/repository/connection/kiotty_connection_host.h"
#include "data/repository/connection/kiotty_notification_desk.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "support/kiotty_test_handler.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace kiotty_test
{
    namespace completion
    {
        inline kiotty::net::implementation::IoCompletion make(kiotty::net::implementation::IoKind kind, const kiotty::net::implementation::IoAction& action, size_t length, kiotty::net::implementation::SocketCode code)
        {
            kiotty::net::implementation::IoCompletion result = { action, length, 0, kind, code };
            return result;
        }

        inline kiotty::net::implementation::IoCompletion receive(const kiotty::net::implementation::IoAction& action, size_t length, kiotty::net::implementation::SocketCode code = kiotty::net::implementation::SocketCode::SUCCESS)
        {
            return make(kiotty::net::implementation::IoKind::RECEIVE, action, length, code);
        }

        inline kiotty::net::implementation::IoCompletion sent(const kiotty::net::implementation::IoAction& action, size_t length, kiotty::net::implementation::SocketCode code = kiotty::net::implementation::SocketCode::SUCCESS)
        {
            return make(kiotty::net::implementation::IoKind::SEND, action, length, code);
        }

        inline kiotty::net::implementation::IoCompletion timeout()
        {
            return kiotty::net::implementation::IoCompletion::timeout(0);
        }

        inline kiotty::net::implementation::IoCompletion workReady()
        {
            return kiotty::net::implementation::IoCompletion::workReady(0);
        }

        inline kiotty::net::implementation::IoCompletion workDone(const kiotty::net::implementation::IoAction& outcome)
        {
            return kiotty::net::implementation::IoCompletion::workDone(0, outcome);
        }
    }

    inline kiotty::net::implementation::ConnectionLimits limitsOf(size_t receive, size_t send, size_t notify)
    {
        kiotty::net::implementation::ConnectionLimits limits = { receive, send, notify };
        return limits;
    }

    template <size_t N>
    class ConnectionBench
    {
    public:
        typedef kiotty::net::implementation::ReusablePool<kiotty::net::implementation::Connection, N> Pool;

        explicit ConnectionBench(kiotty::net::implementation::ConnectionLimits limits)
            : pool(new Pool())
            , host(*pool)
            , desk(*pool, host.notificationMutex())
            , profile()
            , owner()
            , recorder()
            , callbacks(Recorder::callbacks())
            , handler(callbacks, &recorder, owner)
        {
            host.initialize(limits);
        }

        kiotty::net::implementation::ConnectionResult acquire()
        {
            kiotty::net::implementation::ConnectionConfiguration configuration = { kiotty::net::implementation::ActiveSocket(), limitsOf(0, 0, 0), &recorder, "", profile };
            return host.acquire(std::move(configuration));
        }

        kiotty::net::implementation::Connection& open()
        {
            kiotty::net::implementation::ConnectionResult acquired = acquire();
            return acquired.value();
        }

        kiotty::net::implementation::IoAction opened(kiotty::net::implementation::Connection& connection)
        {
            return connection.onOpened(handler);
        }

        kiotty::net::implementation::IoAction complete(kiotty::net::implementation::Connection& connection, const kiotty::net::implementation::IoCompletion& done)
        {
            return connection.dispatch(done, handler, host.notificationMutex());
        }

        size_t place(kiotty::net::implementation::Connection& connection, const kiotty::net::implementation::IoAction& read, const std::string& bytes)
        {
            kiotty::net::implementation::IoPayload room = connection.resolve(read);
            size_t length = bytes.size() < room.length ? bytes.size() : room.length;
            if (length > 0)
            {
                std::memcpy(room.buffer, bytes.data(), length);
            }
            return length;
        }

        kiotty::net::implementation::IoAction deliver(kiotty::net::implementation::Connection& connection, const kiotty::net::implementation::IoAction& read, const std::string& bytes)
        {
            size_t length = place(connection, read, bytes);
            return complete(connection, completion::receive(read, length));
        }

        kiotty::net::implementation::IoAction pick(kiotty::net::implementation::Connection& connection)
        {
            return connection.flows().send.pick(host.notificationMutex());
        }

        KiottyNetCode notify(kiotty::net::implementation::Connection& connection, const std::string& bytes)
        {
            return desk.notify(connection.state().id, spanOf(bytes));
        }

        bool empty() const
        {
            return pool->count() == 0;
        }

        bool full() const
        {
            return pool->count() == pool->capacity();
        }

        std::unique_ptr<Pool>                               pool;
        kiotty::net::implementation::ConnectionHost         host;
        kiotty::net::implementation::NotificationDesk       desk;
        kiotty::net::implementation::TlsProfile             profile;
        FakeOwner                                           owner;
        Recorder                                            recorder;
        const KiottyNetConnectionHandler                    callbacks;
        kiotty::net::implementation::Handler                handler;
    };

    inline kiotty::net::implementation::IoEvent& readEventOf(kiotty::net::implementation::Connection& connection)
    {
        return connection.eventFor(kiotty::net::implementation::IoAction::read(kiotty::net::implementation::IoPurpose::USER, 0, 1));
    }

    inline kiotty::net::implementation::IoEvent& writeEventOf(kiotty::net::implementation::Connection& connection)
    {
        return connection.eventFor(kiotty::net::implementation::IoAction::write(kiotty::net::implementation::IoPurpose::USER, 0, 1));
    }
}

#endif
