#pragma once

#include "base/data/infra/socket/kiotty_socket_handle.h"
#include "base/domain/entity/kiotty_socket_code.h"

#include <atomic>
#include <cstdint>

namespace kiotty::net::implementation
{
    class StopSignal
    {
    public:
        StopSignal();
        ~StopSignal();

        StopSignal(const StopSignal&) = delete;
        StopSignal& operator=(const StopSignal&) = delete;
        StopSignal(StopSignal&&) = delete;
        StopSignal& operator=(StopSignal&&) = delete;

        SocketCode   open();
        void         raise();
        bool         accept();
        bool         isRaised() const;
        void         drain() const;
        SocketHandle handle() const;

    private:
        void close();
        void notify() const;

        std::atomic<uint32_t> _raised;
        uint32_t              _accepted;
        SocketHandle          _reader;
        SocketHandle          _writer;
    };
}
