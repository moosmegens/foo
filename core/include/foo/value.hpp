#ifndef FOO_VALUE_HPP
#define FOO_VALUE_HPP

#include <foo/resolver.hpp>

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace foo
{
    template<typename T>
    using Rc = std::shared_ptr<T>;

    template<class T, class... Args>
    Rc<T> make(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }


    struct Value;
    using ValueRef = Rc<Value>;

    struct ValueRefCmp
    {
        bool operator()(const ValueRef& a, const ValueRef& b) const;
    };

    struct Range
    {
        Range(ValueRef lo, ValueRef hi, bool inclusive)
            : lo(std::move(lo)), hi(std::move(hi)), inclusive(inclusive) {}

        ValueRef lo, hi;
        bool inclusive;
    };

    struct Array
    {
        std::vector<ValueRef> items;
    };

    struct Tuple
    {
        std::vector<ValueRef> items;
    };

    struct Map
    {
        std::map<ValueRef, ValueRef, ValueRefCmp> items;
    };

    struct Set
    {
        std::set<ValueRef, ValueRefCmp> items;
    };

    struct Struct
    {
        std::vector<ValueRef> fields;
    };

    // a variant of an enum, `index` is the position in the enum declaration
    struct EnumValue
    {
        EnumId id = EmptyId;
        int index = 0;
    };

    // used for `any` and for variants: the value and which type it really is
    struct Boxed
    {
        ValueRef v;
        TypeId type = EmptyType;
    };

    // a lambda together with the values it captured when it was created.
    // `captured` is shared between the calls of one closure (so a closure can keep state),
    // and it is deep copied when the closure value is copied. It is never null.
    struct Closure
    {
        const ast::Lambda* lambda = nullptr;
        Rc<std::vector<ValueRef>> captured;
        bool by_ref = false;
    };

    struct Native
    {
        virtual ~Native() = default;

        virtual std::strong_ordering compare(const Native& o) const
            { return std::compare_three_way{}(this, &o); }

        virtual std::optional<Rc<Native>> clone() const { return std::nullopt; }
    };


    struct Value
    {
        using Storage = std::variant<
            std::monostate,
            bool,
            int64_t,
            double,
            std::string,
            Range,
            Array,
            Tuple,
            Map,
            Set,
            Struct,
            EnumValue,
            Boxed,
            Rc<Native>,
            Closure
        >;

        explicit Value(Storage s) : v(std::move(s)) {}

        Value clone() const;

        Storage v;
    };

    std::strong_ordering operator<=>(const Value& a, const Value& b);
    bool operator==(const Value& a, const Value& b);

    inline bool ValueRefCmp::operator()(const ValueRef& a, const ValueRef& b) const
    {
        return (*a <=> *b) < 0;
    }

    // `names` is needed to print enum variants by name
    std::string to_string(const Value& v, const ResolveOutput* names = nullptr);
}


#endif /* FOO_VALUE_HPP */