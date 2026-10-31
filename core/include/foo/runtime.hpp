#ifndef FOO_RUNTIME_HPP
#define FOO_RUNTIME_HPP

#include <deque>
#include <foo/resolver.hpp>
#include <foo/value.hpp>

namespace foo
{
    class Runtime;

    inline ValueRef make_value(Value::Storage s)
    {
        return make<Value>(std::move(s));
    }

    inline ValueRef make_void()                 { return make_value(std::monostate{}); }
    inline ValueRef make_bool(bool b)           { return make_value(b); }
    inline ValueRef make_int(int64_t i)         { return make_value(i); }
    inline ValueRef make_float(double d)        { return make_value(d); }
    inline ValueRef make_string(std::string s)  { return make_value(std::move(s)); }

    using Args = std::span<const ValueRef>;
    using NativeFn = std::function<ValueRef(Runtime&, Args)>;

    class NativeTable
    {
    public:
        void set(NativeFunctionId id, NativeFn fn)
        {
            if (id >= _fns.size())
                _fns.resize(static_cast<size_t>(id) + 1);

            _fns[id] = std::move(fn);
        }

        bool has(NativeFunctionId id) const
        {
            return id < _fns.size() && _fns[id];
        }

        const NativeFn* get(NativeFunctionId id) const
        {
            if (id >= _fns.size())
                return nullptr;

            return &_fns[id];
        }

    private:
        std::vector<NativeFn> _fns;
    };





    struct RuntimeInput
    {
        ResolveOutput resolver_info;
        NativeTable native_table;
    };

    class Runtime
    {
    public:
        Runtime(RuntimeInput input);

        static void inject(TypeTable& types, NativeRegistry& natives, NativeTable& table);

        ValueRef call(FunctionId fn, Args args);

    private:
        struct Frame
        {
            std::vector<ValueRef> slots;
        };

        RuntimeInput _input;
        std::deque<Frame> _frames;
    };
}


#endif //FOO_RUNTIME_HPP
