#pragma once

#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>

namespace kiotty::net::implementation
{
    template <typename Value, typename Code>
    struct [[nodiscard]] Result
    {
        static_assert(std::is_enum<Code>::value || std::is_integral<Code>::value,
                      "Result: Code must be an enum or an integral type");
        static_assert(!std::is_same<Code, bool>::value, "Result: Code must not be bool");
        static_assert(std::is_default_constructible<Value>::value,
                      "Result: a failed result holds a default Value");

        Code  code;
        Value value;

        explicit operator bool() const;

        static Result success(Value content);
        static Result failure(Code error_code);
    };

    template <typename Value, typename Code>
    class [[nodiscard]] Result<Value&, Code>
    {
        static_assert(std::is_enum<Code>::value || std::is_integral<Code>::value,
                      "Result: Code must be an enum or an integral type");
        static_assert(!std::is_same<Code, bool>::value, "Result: Code must not be bool");

    public:
        static Result success(Value& content);
        static Result failure(Code error_code);

        explicit operator bool() const;
        Value&   value() const;

        Code code;

    private:
        Result(Code error_code, Value* content);

        Value* _value;
    };

    template <typename Value, typename Code>
    inline Result<Value, Code>::operator bool() const
    {
        return code == Code{};
    }

    template <typename Value, typename Code>
    inline Result<Value, Code> Result<Value, Code>::success(Value content)
    {
        return Result{ Code{}, std::move(content) };
    }

    template <typename Value, typename Code>
    inline Result<Value, Code> Result<Value, Code>::failure(Code error_code)
    {
        return Result{ error_code, Value{} };
    }

    template <typename Value, typename Code>
    inline Result<Value&, Code>::Result(Code error_code, Value* content)
        : code(error_code)
        , _value(content)
    {
    }

    template <typename Value, typename Code>
    inline Result<Value&, Code> Result<Value&, Code>::success(Value& content)
    {
        return Result(Code{}, std::addressof(content));
    }

    template <typename Value, typename Code>
    inline Result<Value&, Code> Result<Value&, Code>::failure(Code error_code)
    {
        return Result(error_code, nullptr);
    }

    template <typename Value, typename Code>
    inline Result<Value&, Code>::operator bool() const
    {
        return code == Code{};
    }

    template <typename Value, typename Code>
    inline Value& Result<Value&, Code>::value() const
    {
        assert(_value != nullptr && "Result<Value&, Code>::value() on a failed result");
        return *_value;
    }
}
