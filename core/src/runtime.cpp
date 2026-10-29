#include <foo/runtime.hpp>

namespace foo
{
    void Runtime::inject(TypeTable& types, NativeRegistry& natives)
    {
        FunctionSig signature;


        signature.params = {{BaseTypes::Any}};
        signature.variadic = true;
        natives.add_function("println",signature);
        natives.add_function("print",signature);

        natives.add_static_function("new", [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            if (ts.kind(receiver) != TypeKind::Map)
                return std::nullopt;

            FunctionSig sig;
            sig.ret = receiver;
            return sig;
        });

        natives.add_static_function("from", [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            FunctionSig sig;
            sig.ret = ts.optional_of(receiver);
            sig.variadic = false;
            sig.params = {{BaseTypes::Any}};
            return sig;
        });


    }
}
