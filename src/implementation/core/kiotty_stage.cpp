#include "core/kiotty_stage.h"

#include <cassert>
#include <cstring>
#include <new>

namespace kiotty
{
    Stage::Provider::Provider(Stage& stage)
        : _stage(stage)
    {
    }

    size_t Stage::Provider::writable() const
    {
        return _stage._capacity - _stage._end_pos;
    }

    char* Stage::Provider::prepare(size_t requested)
    {
        if (requested > writable())
        {
            return nullptr;
        }
        return _stage._buffer + _stage._end_pos;
    }

    void Stage::Provider::commit(size_t written)
    {
        size_t room = writable();
        assert(written <= room);
        if (written > room)
        {
            written = room;
        }
        _stage._end_pos += written;
    }

    Stage::Consumer::Consumer(Stage& stage)
        : _stage(stage)
    {
    }

    size_t Stage::Consumer::readable() const
    {
        return _stage._end_pos - _stage._begin_pos;
    }

    char* Stage::Consumer::peek()
    {
        if (_stage._buffer == nullptr)
        {
            return nullptr;
        }
        return _stage._buffer + _stage._begin_pos;
    }

    void Stage::Consumer::commit(size_t consumed)
    {
        size_t ready = readable();
        assert(consumed <= ready);
        if (consumed > ready)
        {
            consumed = ready;
        }
        _stage._begin_pos += consumed;
    }

    Stage::Stage(size_t capacity)
        : _buffer(nullptr)
        , _capacity(0)
        , _begin_pos(0)
        , _end_pos(0)
        , _valid(true)
    {
        if (capacity == 0)
        {
            return;
        }
        _buffer = new (std::nothrow) char[capacity];
        if (_buffer == nullptr)
        {
            _valid = false;
            return;
        }
        _capacity = capacity;
    }

    Stage::~Stage()
    {
        delete[] _buffer;
    }

    bool Stage::valid() const
    {
        return _valid;
    }

    size_t Stage::capacity() const
    {
        return _capacity;
    }

    Stage::Provider Stage::provider()
    {
        return Provider(*this);
    }

    Stage::Consumer Stage::consumer()
    {
        return Consumer(*this);
    }

    void Stage::compact()
    {
        size_t unread = _end_pos - _begin_pos;
        if (unread > 0 && _begin_pos > 0)
        {
            std::memmove(_buffer, _buffer + _begin_pos, unread);
        }
        _begin_pos = 0;
        _end_pos = unread;
    }
}
