#include <foo/runtime.hpp>

#include <cmath>
#include <utility>

namespace foo
{

    namespace
    {
        using namespace ast;

        enum class Flow
        {
            Continue, Return, Break
        };

        using ValueResult = Result<ValueRef, RuntimeError>;
        using FlowResult = Result<Flow, RuntimeError>;
        using OpResult = Result<ValueRef, std::string>;

        OpResult int_op(BinaryOp op, int64_t a, int64_t b)
        {
            const auto ua = static_cast<uint64_t>(a);
            const auto ub = static_cast<uint64_t>(b);

            switch (op)
            {
            case BinaryOp::Add:
                return make_int(static_cast<int64_t>(ua + ub));

            case BinaryOp::Sub:
                return make_int(static_cast<int64_t>(ua - ub));

            case BinaryOp::Mul:
                return make_int(static_cast<int64_t>(ua * ub));

            case BinaryOp::Div:
                if (b == 0)
                    return std::unexpected(std::string("division by zero"));

                if (b == -1)
                    return make_int(static_cast<int64_t>(0 - ua));

                return make_int(a / b);

            case BinaryOp::Mod:
                if (b == 0)
                    return std::unexpected(std::string("modulo by zero"));

                if (b == -1)
                    return make_int(0);

                return make_int(a % b);

            default:
                return std::unexpected(std::string("internal int_op: bad op"));
            }
        }

        OpResult float_op(BinaryOp op, double a, double b)
        {
            switch (op)
            {
            case BinaryOp::Add:
                return make_float(a + b);

            case BinaryOp::Sub:
                return make_float(a - b);

            case BinaryOp::Mul:
                return make_float(a * b);

            case BinaryOp::Div:
                return make_float(a / b);

            case BinaryOp::Mod:
                return make_float(std::fmod(a, b));

            default:
                return std::unexpected(std::string("internal float_op: bad op"));
            }
        }

        std::optional<double> number(const Value& v)
        {
            if (const auto* i = std::get_if<int64_t>(&v.v))
                return static_cast<double>(*i);

            if (const auto* d = std::get_if<double>(&v.v))
                return *d;

            return std::nullopt;
        }

        OpResult arithmetic(BinaryOp op, const Value& l, const Value& r)
        {
            if (const auto* a = std::get_if<int64_t>(&l.v))
                if (const auto* b = std::get_if<int64_t>(&r.v))
                    return int_op(op, *a, *b);

            if (const auto a = number(l))
                if (const auto b = number(r))
                    return float_op(op, *a, *b);

            if (op == BinaryOp::Add)
                if (const auto* a = std::get_if<std::string>(&l.v))
                    if (const auto* b = std::get_if<std::string>(&r.v))
                        return make_string(*a + *b);

            return std::unexpected(std::string("internal arithmetic: bad op"));
        }

        OpResult float_comparison(BinaryOp op, double a, double b)
        {
            switch (op)
            {
            case BinaryOp::Eq:
                return make_bool(a == b);

            case BinaryOp::NotEq:
                return make_bool(a != b);

            case BinaryOp::Less:
                return make_bool(a < b);

            case BinaryOp::LessEq:
                return make_bool(a <= b);

            case BinaryOp::Greater:
                return make_bool(a > b);

            case BinaryOp::GreaterEq:
                return make_bool(a >= b);

            default:
                return std::unexpected(std::string("internal float_comparison: bad cmp"));
            }
        }

        OpResult comparison(BinaryOp op, const Value& l, const Value& r)
        {
            const bool ints = std::holds_alternative<int64_t>(l.v) && std::holds_alternative<int64_t>(r.v);

            if (!ints)
                if (const auto a = number(l))
                    if (const auto b = number(r))
                        return float_comparison(op, *a, *b);

            const std::strong_ordering c = l <=> r;

            switch (op)
            {
            case BinaryOp::Eq:
                return make_bool(c == 0);

            case BinaryOp::NotEq:
                return make_bool(c != 0);

            case BinaryOp::Less:
                return make_bool(c < 0);

            case BinaryOp::LessEq:
                return make_bool(c <= 0);

            case BinaryOp::Greater:
                return make_bool(c > 0);

            case BinaryOp::GreaterEq:
                return make_bool(c >= 0);

            default:
                return std::unexpected(std::string("internal comparison: bad cmp op"));
            }
        }

        struct Frame
        {
            std::vector<ValueRef> slots;
            const FunctionInfo* fn = nullptr;
        };

        struct FramePopHelper
        {
            std::deque<Frame>& frames;
            ~FramePopHelper() { frames.pop_back(); }
        };
    }

    class Runtime::Runner
    {
    public:
        Runner(Runtime& rt, RuntimeInput input)
            : _rt(rt), _input(std::move(input))
        {

        }

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
