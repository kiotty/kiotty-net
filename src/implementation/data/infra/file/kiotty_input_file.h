#pragma once

#include "data/infra/socket/kiotty_passive_socket.h"
#include "data/infra/socket/kiotty_socket_transfer.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    using FileReadResult = Result<size_t, KiottyNetCode>;

    struct FileRange
    {
        uint64_t offset;
        uint64_t length;
    };

    class InputFile
    {
    public:
        InputFile();
        ~InputFile();

        InputFile(const InputFile&) = delete;
        InputFile& operator=(const InputFile&) = delete;

        KiottyNetCode  open(Span<const char> path);
        FileReadResult read(uint64_t offset, Span<char> target);
        void           close();

        bool     valid() const;
        uint64_t size() const;
        intptr_t handle() const;

    private:
        intptr_t _handle;
        uint64_t _size;
    };

    SocketCode sendFile(const ActiveSocket& socket, const InputFile& file, FileRange range,
        const Deadline& deadline);
}
