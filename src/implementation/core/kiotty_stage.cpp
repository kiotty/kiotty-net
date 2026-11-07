#include "core/kiotty_stage.h"

#include <cassert>
#include <cstring>

namespace kiotty::net::implementation
{
    StageBase::Provider::Provider(StageBase& stage)
        : _stage(stage)
    {
    }

    size_t StageBase::Provider::writable() const
    {
        return _stage._capacity - _stage._end_position;
    }

    char* StageBase::Provider::prepare(size_t requested)
    {
        if (_stage._capacity == 0 || requested > writable()) { return nullptr; }
        return _stage._buffer + _stage._end_position;
    }

    void StageBase::Provider::commit(size_t written)
    {
        const size_t room = writable();
        assert(written <= room);
        const size_t accepted = written > room ? room : written;
        _stage._end_position += accepted;
    }

    StageBase::Consumer::Consumer(StageBase& stage)
        : _stage(stage)
    {
    }

    size_t StageBase::Consumer::readable() const
    {
        return _stage._end_position - _stage._begin_position;
    }

    char* StageBase::Consumer::peek()
    {
        if (_stage._capacity == 0) { return nullptr; }
        return _stage._buffer + _stage._begin_position;
    }

    void StageBase::Consumer::commit(size_t consumed)
    {
        const size_t ready = readable();
        assert(consumed <= ready);
        const size_t accepted = consumed > ready ? ready : consumed;
        _stage._begin_position += accepted;
    }

    StageBase::StageBase(Span<char> storage)
        : _buffer(storage.data)
        , _maximum(storage.length)
        , _capacity(storage.length)
        , _begin_position(0)
        , _end_position(0)
    {
    }

    size_t StageBase::capacity() const
    {
        return _capacity;
    }

    size_t StageBase::maximum() const
    {
        return _maximum;
    }

    StageBase::Provider StageBase::provider()
    {
        return Provider(*this);
    }

    StageBase::Consumer StageBase::consumer()
    {
        return Consumer(*this);
    }

    void StageBase::compact()
    {
        const size_t unread = _end_position - _begin_position;
        if (unread > 0 && _begin_position > 0)
        {
            std::memmove(_buffer, _buffer + _begin_position, unread);
        }
        _begin_position = 0;
        _end_position = unread;
    }

    void StageBase::clear()
    {
        _begin_position = 0;
        _end_position = 0;
    }

    void StageBase::limit(size_t capacity)
    {
        assert(_end_position == _begin_position);
        _capacity = capacity > _maximum ? _maximum : capacity;
        clear();
    }
}
