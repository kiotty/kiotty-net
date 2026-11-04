#include "core/kiotty_stage.h"

#include <cassert>
#include <cstring>

namespace kiotty
{
    StageBase::Provider::Provider(StageBase& stage)
        : _stage(stage)
    {
    }

    size_t StageBase::Provider::writable() const
    {
        return _stage._capacity - _stage._end_pos;
    }

    char* StageBase::Provider::prepare(size_t requested)
    {
        if (_stage._capacity == 0 || requested > writable())
        {
            return nullptr;
        }
        return _stage._buffer + _stage._end_pos;
    }

    void StageBase::Provider::commit(size_t written)
    {
        size_t room = writable();
        assert(written <= room);
        if (written > room)
        {
            written = room;
        }
        _stage._end_pos += written;
    }

    StageBase::Consumer::Consumer(StageBase& stage)
        : _stage(stage)
    {
    }

    size_t StageBase::Consumer::readable() const
    {
        return _stage._end_pos - _stage._begin_pos;
    }

    char* StageBase::Consumer::peek()
    {
        if (_stage._capacity == 0)
        {
            return nullptr;
        }
        return _stage._buffer + _stage._begin_pos;
    }

    void StageBase::Consumer::commit(size_t consumed)
    {
        size_t ready = readable();
        assert(consumed <= ready);
        if (consumed > ready)
        {
            consumed = ready;
        }
        _stage._begin_pos += consumed;
    }

    StageBase::StageBase(char* buffer, size_t maximum)
        : _buffer(buffer)
        , _maximum(maximum)
        , _capacity(maximum)
        , _begin_pos(0)
        , _end_pos(0)
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
        size_t unread = _end_pos - _begin_pos;
        if (unread > 0 && _begin_pos > 0)
        {
            std::memmove(_buffer, _buffer + _begin_pos, unread);
        }
        _begin_pos = 0;
        _end_pos = unread;
    }

    void StageBase::clear()
    {
        _begin_pos = 0;
        _end_pos = 0;
    }

    void StageBase::limit(size_t capacity)
    {
        assert(_end_pos == _begin_pos);
        if (capacity > _maximum)
        {
            capacity = _maximum;
        }
        _capacity = capacity;
        clear();
    }
}
