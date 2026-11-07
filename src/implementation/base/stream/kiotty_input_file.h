#if !defined(KIOTTY_INPUT_FILE_H)
#define KIOTTY_INPUT_FILE_H

#include "base/socket/kiotty_socket.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_result.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    typedef Result<size_t, KiottyNetCode> FileReadResult;

    class InputFile
    {
    public:
        InputFile();
        ~InputFile();

        InputFile(const InputFile&) = delete;
        InputFile& operator=(const InputFile&) = delete;

        KiottyNetCode  open(const char* path);
        FileReadResult read(uint64_t offset, char* buffer, size_t length);
        void           close();

        bool     valid() const;
        uint64_t size() const;
        intptr_t handle() const;

    private:
        intptr_t _handle;
        uint64_t _size;
    };

    SocketCode sendFile(ActiveSocket& socket, InputFile& file, uint64_t offset, uint64_t length, const Deadline& deadline, uint64_t& sent);
}

#endif
