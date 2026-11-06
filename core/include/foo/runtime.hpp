#ifndef FOO_RUNTIME_HPP
#define FOO_RUNTIME_HPP


#include <foo/error.hpp>

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

    [[noreturn]] inline void type_error(const Value& got)
    {
        throw std::runtime_error(std::format("got wrong type"));
    }

    template<class T>
    T& expect(Value& v)
    {
        if (auto* p = std::get_if<T>(&v.v))
            return *p;

        type_error(v);
    }

    template<class T>
    T& native_cast(Value& v)
    {
        if (auto* p = dynamic_cast<T*>(expect<Rc<Native>>(v).get()))
            return *p;

        throw std::runtime_error("wrong native type");
    }

    inline bool&        as_bool  (const ValueRef& r) { return expect<bool>(*r); }
    inline int64_t&     as_int   (const ValueRef& r) { return expect<int64_t>(*r); }
    inline double&      as_float (const ValueRef& r) { return expect<double>(*r); }
    inline std::string& as_string(const ValueRef& r) { return expect<std::string>(*r); }
    inline Array&       as_array (const ValueRef& r) { return expect<Array>(*r); }
    inline Map&         as_map   (const ValueRef& r) { return expect<Map>(*r); }

    inline ValueRef copy(const ValueRef& r)
    {
        return make<Value>(r->clone());
    }

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


    struct RuntimeError
    {
        std::string what;

        int file_index;
        SourceSpan span;
    };


    struct RuntimeInput
    {
        ResolveOutput resolver_info;
        NativeTable native_table;

        size_t max_depth = 256;
    };

    class Runtime
    {
    public:
        Runtime(RuntimeInput input);
        ~Runtime();

        static void inject(TypeTable& types, NativeRegistry& natives, NativeTable& table);

        Result<ValueRef, RuntimeError> call(FunctionId fn, Args args);

        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

    private:
        class Runner;
        std::unique_ptr<Runner> _runner;

    };
}


#endif //FOO_RUNTIME_HPP
