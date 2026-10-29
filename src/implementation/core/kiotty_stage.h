#if !defined(KIOTTY_STAGE_H)
#define KIOTTY_STAGE_H

#include <cstddef>

namespace kiotty
{
    class Stage
    {
    public:
        class Provider
        {
        public:
            explicit Provider(Stage& stage);

            size_t writable() const;
            char*  prepare(size_t requested);
            void   commit(size_t written);

        private:
            Stage& _stage;
        };

        class Consumer
        {
        public:
            explicit Consumer(Stage& stage);

            size_t readable() const;
            char*  peek();
            void   commit(size_t consumed);

        private:
            Stage& _stage;
        };

        explicit Stage(size_t capacity);
        ~Stage();

        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;

        bool     valid() const;
        size_t   capacity() const;
        Provider provider();
        Consumer consumer();
        void     compact();

    private:
        char*  _buffer;
        size_t _capacity;
        size_t _begin_pos;
        size_t _end_pos;
        bool   _valid;
    };
}

#endif
