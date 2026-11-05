#if !defined(KIOTTY_INPUT_FILE_H)
#define KIOTTY_INPUT_FILE_H

#include "core/kiotty_result.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty
{
    namespace net
    {
        typedef Result<KiottyNetCode, size_t> FileReadResult;

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

        private:
            intptr_t _handle;
            uint64_t _size;
        };
    }
}

#endif
