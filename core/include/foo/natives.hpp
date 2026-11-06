#ifndef FOO_NATIVES_HPP
#define FOO_NATIVES_HPP

#include <foo/runtime.hpp>

#include <string>
#include <vector>

namespace foo
{
    class NativeDef
    {
    public:
        NativeDef(TypeTable& types, NativeRegistry& natives, NativeTable& table)
            : types(types), natives(natives), table(table) {}

        TypeTable& types;
        NativeRegistry& natives;
        NativeTable& table;

        static FunctionSig sig(std::vector<ParamSig> params, TypeId ret, bool self_by_ref = false)
        {
            FunctionSig s;
            s.params = std::move(params);
            s.ret = ret;
            s.self_by_ref = self_by_ref;
            return s;
        }

        static FunctionSig variadic(std::vector<ParamSig> params, TypeId ret, bool self_by_ref = false)
        {
            FunctionSig s;
            s.params = std::move(params);
            s.ret = ret;
            s.self_by_ref = self_by_ref;
            s.variadic = true;
            return s;
        }

        void function(std::string name, FunctionSig sig, NativeFn fn, const std::string& info = "")
        {
            table.set(natives.add_function(std::move(name), std::move(sig)), std::move(fn), info);
        }

        void method(std::string name, TypeId receiver, FunctionSig sig, NativeFn fn, const std::string& info = "")
        {
            table.set(natives.add_method(std::move(name), receiver, std::move(sig)), std::move(fn), info);
        }

        void method(std::string name, NativeResolver resolve, NativeFn fn, const std::string& info = "")
        {
            table.set(natives.add_method(std::move(name), std::move(resolve)), std::move(fn), info);
        }

        void static_function(std::string name, NativeResolver resolve, NativeFn fn, const std::string& info = "")
        {
            table.set(natives.add_static_function(std::move(name), std::move(resolve)), std::move(fn), info);
        }
    };

    void inject_rng(TypeTable& types, NativeRegistry& natives, NativeTable& table);

    void inject_io(TypeTable& types, NativeRegistry& natives, NativeTable& table);

    void inject_os(TypeTable& types, NativeRegistry& natives, NativeTable& table);
}

#endif /* FOO_NATIVES_HPP */