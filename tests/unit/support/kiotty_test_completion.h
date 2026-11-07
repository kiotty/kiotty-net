#if !defined(KIOTTY_TEST_COMPLETION_H)
#define KIOTTY_TEST_COMPLETION_H

#include "base/connection/kiotty_connection.h"
#include "base/connection/kiotty_connection_host.h"
#include "base/tls/kiotty_tls_profile.h"
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
        inline kiotty::net::IoCompletion make(kiotty::net::IoKind kind, const kiotty::net::IoAction& action, size_t length, kiotty::net::SocketCode code)
        {
            kiotty::net::IoCompletion result = { kind, 0, code, length, action };
            return result;
        }

        inline kiotty::net::IoCompletion receive(const kiotty::net::IoAction& action, size_t length, kiotty::net::SocketCode code = kiotty::net::SocketCode::SUCCESS)
        {
            return make(kiotty::net::IoKind::RECEIVE, action, length, code);
        }

        inline kiotty::net::IoCompletion sent(const kiotty::net::IoAction& action, size_t length, kiotty::net::SocketCode code = kiotty::net::SocketCode::SUCCESS)
        {
            return make(kiotty::net::IoKind::SEND, action, length, code);
        }

        inline kiotty::net::IoCompletion timeout()
        {
            return kiotty::net::IoCompletion::timeout(0);
        }

        inline kiotty::net::IoCompletion workReady()
        {
            return kiotty::net::IoCompletion::workReady(0);
        }

        inline kiotty::net::IoCompletion workDone(const kiotty::net::IoAction& outcome)
        {
            return kiotty::net::IoCompletion::workDone(0, outcome);
        }
    }

    inline kiotty::net::ConnectionLimits limitsOf(size_t receive, size_t send, size_t notify)
    {
        kiotty::net::ConnectionLimits limits = { receive, send, notify };
        return limits;
    }

    template <size_t N>
    class ConnectionBench
    {
    public:
        typedef kiotty::net::ConnectionHost<N> Host;

        explicit ConnectionBench(kiotty::net::ConnectionLimits limits)
            : host(new Host())
            , profile()
            , owner()
            , recorder()
            , callbacks(Recorder::callbacks())
            , handler(callbacks, &recorder, owner)
        {
            host->initialize(limits);
        }

        kiotty::net::ConnectionResult acquire()
        {
            kiotty::net::ConnectionConfiguration configuration = { kiotty::net::ActiveSocket(), &recorder, "", limitsOf(0, 0, 0), profile };
            return host->acquire(std::move(configuration));
        }

        kiotty::net::Connection& open()
        {
            kiotty::net::ConnectionResult acquired = acquire();
            return acquired.value();
        }

        kiotty::net::IoAction opened(kiotty::net::Connection& connection)
        {
            return connection.onOpened(handler);
        }

        kiotty::net::IoAction complete(kiotty::net::Connection& connection, const kiotty::net::IoCompletion& done)
        {
            return connection.dispatch(done, handler, host->notificationMutex());
        }

        size_t place(kiotty::net::Connection& connection, const kiotty::net::IoAction& read, const std::string& bytes)
        {
            kiotty::net::IoPayload room = connection.resolve(read);
            size_t length = bytes.size() < room.length ? bytes.size() : room.length;
            if (length > 0)
            {
                std::memcpy(room.buffer, bytes.data(), length);
            }
            return length;
        }

        kiotty::net::IoAction deliver(kiotty::net::Connection& connection, const kiotty::net::IoAction& read, const std::string& bytes)
        {
            size_t length = place(connection, read, bytes);
            return complete(connection, completion::receive(read, length));
        }

        kiotty::net::IoAction pick(kiotty::net::Connection& connection)
        {
            return connection.pickWrite(host->notificationMutex());
        }

        KiottyNetCode notify(kiotty::net::Connection& connection, const std::string& bytes)
        {
            return host->notify(connection.id(), spanOf(bytes));
        }

        std::unique_ptr<Host>            host;
        kiotty::net::TlsProfile          profile;
        FakeOwner                        owner;
        Recorder                         recorder;
        const KiottyNetConnectionHandler callbacks;
        kiotty::net::Handler             handler;
    };

    inline kiotty::net::IoEvent& readEventOf(kiotty::net::Connection& connection)
    {
        return connection.eventFor(kiotty::net::IoAction::read(kiotty::net::IoPurpose::USER, 0, 1));
    }

    inline kiotty::net::IoEvent& writeEventOf(kiotty::net::Connection& connection)
    {
        return connection.eventFor(kiotty::net::IoAction::write(kiotty::net::IoPurpose::USER, 0, 1));
    }
}

#endif
