#include <foo/runtime.hpp>
#include <utility>

namespace foo
{

    namespace
    {
        struct Frame
        {
            std::vector<ValueRef> slots;
            const FunctionInfo* fn = nullptr;
        };

        enum class Flow
        {
            Continue, Return, Break
        };
    }

    class Runtime::Runner
    {
    public:
        Runner(Runtime& rt, RuntimeInput input)
            : _rt(rt), _input(std::move(input)) {}

        Result<ValueRef, RuntimeError> call_function(FunctionId id, std::vector<ValueRef> args)
        {
            const FunctionInfo& f = function(id);

            if (!f.decl)
                return error("function has no body", f);

            const size_t expected = f.sig.params.size() + (f.is_method ? 1 : 0);



            return make_void();
        }

    private:
        Runtime& _rt;
        RuntimeInput _input;
        std::deque<Frame> _frames;

        ValueRef _ret;


        static std::unexpected<RuntimeError> error(const std::string& what, const DeclInfo& decl)
        {
            return error(what, decl.file_index, decl.span);
        }

        static std::unexpected<RuntimeError> error(const std::string& what, int file_index = -1, SourceSpan span = {})
        {
            return std::unexpected(RuntimeError{
              .what = what, .file_index = file_index, .span = span}
            );
        }

        const ResolveOutput& resolved() const { return _input.resolver_info; }

        const FunctionInfo& function(Id id) const
        {
            if (id < 0 || static_cast<size_t>(id) >= resolved().functions.size())
                throw std::runtime_error("internal: bad function id");

            return resolved().functions[static_cast<size_t>(id)];
        }
    };


    Runtime::Runtime(RuntimeInput input)
        : _runner(std::make_unique<Runner>(*this, std::move(input)))
    { }

    Result<ValueRef, RuntimeError> Runtime::call(FunctionId fn, Args args)
    {

        return make_void();
    }





    void Runtime::inject(TypeTable& types, NativeRegistry& natives, NativeTable& table)
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
