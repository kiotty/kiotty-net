#if !defined(KIOTTY_RESULT_H)
#define KIOTTY_RESULT_H

#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>

namespace kiotty
{
    template<typename ErrorCode, typename Value>
    class Result
    {
        static_assert(
            std::is_enum<ErrorCode>::value || std::is_integral<ErrorCode>::value,
            "kiotty::Result: ErrorCode must be an enum or an integral type.");
        static_assert(
            !std::is_same<ErrorCode, bool>::value,
            "kiotty::Result: ErrorCode must not be bool.");
        static_assert(
            std::is_default_constructible<Value>::value,
            "kiotty::Result: Value must be default constructible, a failed result holds a default Value.");

    public:
        static Result success(const Value& value)
        {
            return Result(static_cast<ErrorCode>(0), value);
        }

        static Result success(Value&& value)
        {
            return Result(static_cast<ErrorCode>(0), std::move(value));
        }

        static Result failure(ErrorCode code)
        {
            return Result(code);
        }

        bool isOk() const { return _code == static_cast<ErrorCode>(0); }

        ErrorCode code() const { return _code; }

        Value& value() { return _value; }
        const Value& value() const { return _value; }

    private:
        explicit Result(ErrorCode code) :
            _code(code),
            _value()
            {}

        Result(ErrorCode code, const Value& value) :
            _code(code),
            _value(value)
            {}

        Result(ErrorCode code, Value&& value) :
            _code(code),
            _value(std::move(value))
            {}

        ErrorCode _code;
        Value     _value;
    };

    template<typename ErrorCode, typename Value>
    class Result<ErrorCode, Value&>
    {
        static_assert(
            std::is_enum<ErrorCode>::value || std::is_integral<ErrorCode>::value,
            "kiotty::Result: ErrorCode must be an enum or an integral type.");
        static_assert(
            !std::is_same<ErrorCode, bool>::value,
            "kiotty::Result: ErrorCode must not be bool.");

    public:
        static Result success(Value& value)
        {
            Value* address = std::addressof(value);
            return Result(static_cast<ErrorCode>(0), address);
        }

        static Result failure(ErrorCode code)
        {
            return Result(code, nullptr);
        }

        bool isOk() const { return _code == static_cast<ErrorCode>(0); }

        ErrorCode code() const { return _code; }

        Value& value() const
        {
            assert(_value != nullptr && "kiotty::Result<E, T&>::value() on a failed result");
            return *_value;
        }

    private:
        Result(ErrorCode code, Value* value) :
            _code(code),
            _value(value)
            {}

        ErrorCode _code;
        Value*    _value;
    };
}

#endif
