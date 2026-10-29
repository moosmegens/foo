#include <foo/runtime.hpp>

namespace foo
{
    void Runtime::inject(TypeTable&, NativeRegistry& natives)
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

        natives.add_method("at", [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            if (ts.kind(receiver) == TypeKind::Array)
            {
                FunctionSig sig;
                sig.ret = ts.optional_of(ts.element(receiver));
                sig.variadic = false;
                sig.params = {{BaseTypes::Int}};
                return sig;
            }

            if (ts.kind(receiver) != TypeKind::String)
            {
                FunctionSig sig;
                sig.ret = ts.optional_of(BaseTypes::String);
                sig.variadic = false;
                sig.params = {{BaseTypes::Int}};
                return sig;
            }

            return std::nullopt;
        });

        natives.add_method("length", [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            if (ts.kind(receiver) == TypeKind::Array)
            {
                FunctionSig sig;
                sig.ret = BaseTypes::Int;
                sig.variadic = false;
                return sig;
            }

            if (ts.kind(receiver) != TypeKind::String)
            {
                FunctionSig sig;
                sig.ret = BaseTypes::Int;
                sig.variadic = false;
                return sig;
            }

            return std::nullopt;
        });


    }
}
