#if !defined(KIOTTY_STAGE_H)
#define KIOTTY_STAGE_H

#include <cstddef>

namespace kiotty
{
    class StageBase
    {
    public:
        class Provider
        {
        public:
            explicit Provider(StageBase& stage);

            size_t writable() const;
            char*  prepare(size_t requested);
            void   commit(size_t written);

        private:
            StageBase& _stage;
        };

        class Consumer
        {
        public:
            explicit Consumer(StageBase& stage);

            size_t readable() const;
            char*  peek();
            void   commit(size_t consumed);

        private:
            StageBase& _stage;
        };

        StageBase(const StageBase&) = delete;
        StageBase& operator=(const StageBase&) = delete;

        size_t   capacity() const;
        size_t   maximum() const;
        Provider provider();
        Consumer consumer();
        void     compact();
        void     clear();
        void     limit(size_t capacity);

    protected:
        StageBase(char* buffer, size_t maximum);
        ~StageBase() = default;

    private:
        char*  _buffer;
        size_t _maximum;
        size_t _capacity;
        size_t _begin_pos;
        size_t _end_pos;
    };

    template <size_t N>
    struct StageStorage
    {
        char bytes[N > 0 ? N : 1];
    };

    template <size_t N>
    class Stage : private StageStorage<N>, public StageBase
    {
    public:
        static const size_t MAXIMUM = N;

        Stage()
            : StageBase(this->bytes, N)
        {
        }
    };

    template <size_t N>
    const size_t Stage<N>::MAXIMUM;
}

#endif
