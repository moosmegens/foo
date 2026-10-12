#include <foo/dynamic_runner.hpp>

#include "foo/util/str.hpp"

#include <chrono>
#include <cmath>
#include <foo/util/overloaded.hpp>

#include <functional>
#include <iostream>
#include <ranges>

namespace foo
{
    using namespace ast;

    struct Value;
    using Array = std::vector<Value>;

    struct Value
    {
        std::variant<std::monostate, int64_t, double, bool, std::string, Array> data;

        Value() = default;

        Value(int64_t v) : data(v) {}
        Value(double v) : data(v) {}
        Value(bool v) : data(v) {}
        Value(std::string v) : data(std::move(v)) {}
        Value(Array v) : data(std::move(v)) {}
    };

    static std::string display(const Value& v, bool string_qouted = false)
    {
        return std::visit(overload{
            [](std::monostate) { return std::string("null"); },
            [](int64_t n) { return std::to_string(n); },
            [](double d) { return std::format("{}", d); },
            [](bool b) { return std::string(b ? "true" : "false"); },
            [&](const std::string& s)
            {
                return string_qouted ? "\"" + s + "\"" : s;
            },
            [](const Array& a)
            {
                std::string out = "[";
                for (size_t i = 0; i < a.size(); i++)
                {
                    if (i) out += ", ";
                    out += display(a[i]);
                }
                return out + "]";
            },
        }, v.data);
    }

    std::string_view type_name(const Value& v)
    {
        return std::visit(overload{
            [](std::monostate) { return "nil"; },
            [](int64_t) { return "int"; },
            [](double) { return "float"; },
            [](bool) { return "bool"; },
            [](const std::string&) { return "string"; },
            [](const Array&) { return "array"; },
        }, v.data);
    }

    static bool is_number(const Value& v)
    {
        return std::holds_alternative<int64_t>(v.data) || std::holds_alternative<double>(v.data);
    }

    static double as_double(const Value& v)
    {
        if (auto* i = std::get_if<int64_t>(&v.data))
            return static_cast<double>(*i);

        return std::get<double>(v.data);
    }

    static bool equals(const Value& a, const Value& b)
    {
        if (is_number(a) && is_number(b))
        {
            auto* ai = std::get_if<int64_t>(&a.data);
            auto* bi = std::get_if<int64_t>(&b.data);
            return (ai && bi) ? *ai == *bi : as_double(a) == as_double(b);
        }
        if (a.data.index() != b.data.index())
            return false;

        if (auto* s = std::get_if<std::string>(&a.data))
            return *s == std::get<std::string>(b.data);

        if (auto* x = std::get_if<bool>(&a.data))
            return *x == std::get<bool>(b.data);

        if (auto* x = std::get_if<Array>(&a.data))
        {
            const auto& y = std::get<Array>(b.data);
            if (x->size() != y.size())
                return false;

            for (size_t i = 0; i < x->size(); i++)
                if (!equals((*x)[i], y[i]))
                    return false;

            return true;
        }
        return true;
    }

    std::string join(const std::vector<std::string>& parts)
    {
        std::string out;
        for (size_t i = 0; i < parts.size(); i++)
            out += (i ? "::" : "") + parts[i];
        return out;
    }

    class Interpreter
    {
    public:
        explicit Interpreter(std::span<const ast::DeclPtr> decls)
        {
            for (const auto& d : decls)
                if (auto* fn = std::get_if<FnDecl>(&d->kind))
                    _user_functions[fn->name] = fn;
        }

        std::optional<RuntimeError> run()
        {
            auto it = _user_functions.find("main");
            if (it == _user_functions.end())
            {
                return RuntimeError{"no 'main' function", {}};
            }

            _builtin_functions["io::println"] = [](std::span<Value> args, SourceSpan)
            {
                for (size_t i = 0; i < args.size(); i++)
                    std::cout << (i ? " " : "") << display(args[i]);

                std::cout << '\n';
                return Value{};
            };

            _builtin_functions["io::print"] = [](std::span<Value> args, SourceSpan)
            {
                for (size_t i = 0; i < args.size(); i++)
                    std::cout << (i ? " " : "") << display(args[i]);

                return Value{};
            };

            _builtin_functions["to_int"] = [](std::span<Value> args, SourceSpan span)
            {
                if (args.size() != 1)
                    fail(std::format("to_int expects 1 argument, got {}", args.size()), span);

                const Value& v = args[0];

                if (auto* b = std::get_if<bool>(&v.data))
                    return Value(static_cast<std::int64_t>(*b ? 1 : 0));

                if (auto* s = std::get_if<std::string>(&v.data))
                {
                    try
                    {
                        size_t used = 0;
                        std::int64_t out = std::stoll(*s, &used);
                        if (used != s->size())
                            fail("trailing characters", span);

                        return Value(out);
                    }
                    catch (const std::invalid_argument&) { fail("not a number", span); }
                    catch (const std::out_of_range&) { fail("number too large", span); }

                    fail("to_int: cannot convert \"" + *s + "\" to an int", span);
                }

                fail(std::format("to_int: cannot convert {}", type_name(v)), span);
            };

            _builtin_functions["len"] = [](std::span<Value> args, SourceSpan span)
            {
                if (args.size() == 1)
                {
                    if (auto* a = std::get_if<Array>(&args[0].data))
                        return Value(static_cast<std::int64_t>(a->size()));

                    if (auto* s = std::get_if<std::string>(&args[0].data))
                        return Value(static_cast<std::int64_t>(s->size()));
                }

                fail("len expects one array or string", span);
            };

            _builtin_functions["range"] = [](std::span<Value> a, SourceSpan span) -> Value
            {
                bool inclusive = false;

                if (!a.empty())
                {
                    if (auto* flag = std::get_if<bool>(&a.back().data))
                    {
                        inclusive = *flag;
                        a = a.first(a.size() - 1);
                    }
                }

                if (a.empty() || a.size() > 2)
                    fail("range expects 1 or 2 ints", span);

                auto* end   = std::get_if<std::int64_t>(&a.back().data);
                auto* start = a.size() == 2 ? std::get_if<std::int64_t>(&a[0].data) : nullptr;

                if (!end || (a.size() == 2 && !start))
                    fail("range expects ints", span);

                const std::int64_t s = start ? *start : 0;
                const std::int64_t e = *end;

                Array out;

                if (inclusive ? s > e : s >= e)
                    return Value(std::move(out));

                const std::int64_t last = inclusive ? e : e - 1;
                for (std::int64_t i = s; ; i++)
                {
                    out.push_back(Value(i));
                    if (i == last)
                        break;
                }

                return Value(std::move(out));
            };

            _builtin_functions["io::input"] = [](std::span<Value> a, SourceSpan span) -> Value
            {
                if (a.size() > 1)
                    fail("input expects 0 or 1 arguments", span);

                if (a.size() == 1)
                    std::cout << display(a[0]) << std::flush;

                std::string line;
                if (!std::getline(std::cin, line))
                    return Value{};

                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                return Value(std::move(line));
            };

            try
            {
                call_function(*it->second, {}, {});
            }
            catch (RuntimeError& e)
            {
                return std::move(e);
            }


            return std::nullopt;
        }

    private:
        using Scope = std::unordered_map<std::string, Value>;

        [[noreturn]] static void fail(std::string msg, SourceSpan span)
        {
            throw RuntimeError{std::move(msg), span};
        }

        Value* find_var(const std::string& name)
        {
            for (auto & _scope : std::views::reverse(_scopes))
                if (auto found = _scope.find(name); found != _scope.end())
                    return &found->second;

            return nullptr;
        }

        Value call_function(const FnDecl& fn, std::vector<Value> args, SourceSpan caller_span)
        {
            if (args.size() != fn.params.size())
                fail(std::format("'{}' expects {} argument(s), got {}", fn.name, fn.params.size(), args.size()), caller_span);

            auto saved = std::move(_scopes);
            _scopes.clear();
            _scopes.emplace_back();

            for (size_t i = 0; i < args.size(); i++)
                _scopes.back()[fn.params[i].name] = std::move(args[i]);

            _depth++;
            _return = Value{};
            exec_block(fn.body);
            _depth--;

            _scopes = std::move(saved);
            return std::move(_return);
        }

        bool condition(const Expr& e)
        {
            Value v = eval(e);
            auto* b = std::get_if<bool>(&v.data);
            if (!b)
                fail(std::format("condition must be of type 'bool', got type '{}'", type_name(v)), e.span);

            return *b;
        }

        bool exec_block(const Block& block)
        {
            _scopes.emplace_back();

            bool returned = false;
            for (const auto& s : block.statements)
            {
                returned = exec(*s);
                if (returned) break;
            }

            _scopes.pop_back();
            return returned;
        }

        bool exec(const Stmt& s)
        {
            return std::visit(overload{
                [&](const Check&) -> bool { fail("check is not yet supported", s.span); },
                [&](const Block& n) { return exec_block(n); },
                [&](const VarDecl& n)
                {
                    Value v = n.init ? eval(*n.init) : Value{};
                    _scopes.back()[n.name] = std::move(v);
                    return false;
                },
                [&](const ExprStmt& n)
                {
                    eval(*n.expr);
                    return false;
                },
                [&](const Return& n)
                {
                    Value v = n.value ? eval(*n.value) : Value{};
                    _return = std::move(v);
                    return true;
                },
                [&](const If& n)
                {
                    if (condition(*n.cond))
                        return exec(*n.then_branch);

                    if (n.else_branch)
                        return exec(*n.else_branch);

                    return false;
                },
                [&](const While& n)
                {
                    while (condition(*n.cond))
                        if (exec(*n.body))
                            return true;
                    return false;
                },
                [&](const ForIn& n)
                {
                    Value iterable = eval(*n.iterable);
                    auto* items = std::get_if<Array>(&iterable.data);
                    if (!items)
                        fail(std::format("cannot iterate over {}", type_name(iterable)), n.iterable->span);

                    for (const Value& item : *items)
                    {
                        _scopes.emplace_back();
                        _scopes.back()[n.variable_name] = item;
                        bool returned = exec(*n.body);
                        _scopes.pop_back();

                        if (returned)
                            return true;
                    }
                    return false;
                },
            }, s.kind);
        }

        Value* place(const Expr& e)
        {
            if (auto* n = std::get_if<Name>(&e.kind))
            {
                if (n->parts.size() == 1)
                    if (Value* v = find_var(n->parts[0]))
                        return v;

                fail("cannot assign to '" + join(n->parts) + "'", e.span);
            }

            if (auto* n = std::get_if<Index>(&e.kind))
            {
                Value index = eval(*n->indexer);
                Value* base = place(*n->target);
                return element(*base, index, e.span);
            }

            fail("invalid assignment target", e.span);
        }

        Value* element(Value& base, const Value& index, SourceSpan span)
        {
            auto* items = std::get_if<Array>(&base.data);
            if (!items)
                fail(std::format("cannot index {}", type_name(base)), span);

            auto* i = std::get_if<int64_t>(&index.data);
            if (!i)
                fail(std::format("index must be int, got {}", type_name(index)), span);

            if (*i < 0 || *i >= static_cast<int64_t>(items->size()))
                fail(std::format("index {} out of range (size {})", *i, items->size()), span);

            return &(*items)[static_cast<size_t>(*i)];
        }

        Value eval(const Expr& e)
        {
            return std::visit(overload{
                [&](const IntLit& n)    -> Value { return Value(n.value); },
                [&](const FloatLit& n)  -> Value { return Value(n.value); },
                [&](const StringLit& n) -> Value { return Value(n.value); },
                [&](const BoolLit& n)   -> Value { return Value(n.value); },
                [&](const ArrayLit& n)  -> Value
                {
                    Array items;
                    for (const auto& el : n.elements)
                        items.push_back(eval(*el));

                    return Value(std::move(items));
                },
                [&](const Name& n) -> Value
                {
                    if (n.parts.size() == 1)
                        if (Value* v = find_var(n.parts[0]))
                            return *v;

                    fail("undefined variable '" + join(n.parts) + "'", e.span);
                },
                [&](const Unary& n) -> Value
                {
                    Value v = eval(*n.operant);

                    if (n.op == UnaryOp::Not)
                    {
                        if (auto* b = std::get_if<bool>(&v.data)) return Value(!*b);
                    }
                    else
                    {
                        if (auto* i = std::get_if<int64_t>(&v.data)) return Value(-*i);
                        if (auto* d = std::get_if<double>(&v.data))       return Value(-*d);
                    }
                    fail(std::format("bad operand type '{}' for unary operator", type_name(v)), e.span);
                },
                [&](const Binary& n) -> Value { return eval_binary(n, e.span); },
                [&](const Index& n) -> Value
                {
                    Value base  = eval(*n.target);
                    Value index = eval(*n.indexer);
                    return *element(base, index, e.span);
                },
                [&](const Call& n) -> Value     { return eval_call(n, e.span); },
                [&](const Member&) -> Value     { fail("member access is not supported yet", e.span); },
                [&](const StaticCall&) -> Value { fail("static calls are not supported yet", e.span); }
            }, e.kind);
        }

        Value eval_call(const Call& n, SourceSpan span)
        {
            auto* name = std::get_if<Name>(&n.callee->kind);
            if (!name)
                fail("only named functions can be called", span);

            std::vector<Value> args;
            for (const auto& a : n.args)
                args.push_back(eval(*a));

            std::string key = join(name->parts);

            if (auto fn = _user_functions.find(key); fn != _user_functions.end())
                return call_function(*fn->second, std::move(args), span);

            if (auto b = _builtin_functions.find(key); b != _builtin_functions.end())
                return b->second(args, span);
            
            fail("undefined function '" + key + "'", span);
        }

        Value eval_binary(const Binary& n, SourceSpan span)
        {
            if (n.op == BinaryOp::Assign)
            {
                Value value = eval(*n.rhs);
                *place(*n.lhs) = value;
                return value;
            }

            if (n.op == BinaryOp::Range || n.op == BinaryOp::RangeInclusive)
            {
                std::array<Value, 3> values;
                values[0] = eval(*n.lhs);
                values[1] = eval(*n.rhs);
                values[2] = n.op == BinaryOp::RangeInclusive;

                return _builtin_functions["range"](values, span);
            }

            if (n.op == BinaryOp::And)
                return Value(condition(*n.lhs) && condition(*n.rhs));

            if (n.op == BinaryOp::Or)
                return Value(condition(*n.lhs) || condition(*n.rhs));

            Value l = eval(*n.lhs);
            Value r = eval(*n.rhs);

            if (n.op == BinaryOp::Eq)
                return Value(equals(l, r));

            if (n.op == BinaryOp::NotEq)
                return Value(!equals(l, r));

            auto* ls = std::get_if<std::string>(&l.data);
            auto* rs = std::get_if<std::string>(&r.data);
            if (ls && rs)
            {
                switch (n.op)
                {
                    case BinaryOp::Add:
                        return Value(*ls + *rs);

                    case BinaryOp::Less:
                        return Value(*ls <  *rs);

                    case BinaryOp::LessEq:
                        return Value(*ls <= *rs);

                    case BinaryOp::Greater:
                        return Value(*ls >  *rs);

                    case BinaryOp::GreaterEq:
                        return Value(*ls >= *rs);

                    default: break;
                }
            }

            if (!is_number(l) || !is_number(r))
                fail(std::format("unsupported operand types '{}' and '{}'", type_name(l), type_name(r)), span);

            auto* li = std::get_if<int64_t>(&l.data);
            auto* ri = std::get_if<int64_t>(&r.data);

            if (li && ri)
            {
                int64_t a = *li;
                int64_t b = *ri;
                switch (n.op)
                {
                    case BinaryOp::Add:
                        return Value(a + b);

                    case BinaryOp::Sub:
                        return Value(a - b);

                    case BinaryOp::Mul:
                        return Value(a * b);
                    case BinaryOp::Div:
                        if (b == 0)
                            fail("division by zero", span);
                        return Value(a / b);

                    case BinaryOp::Mod:
                        if (b == 0)
                            fail("modulo by zero", span);
                        return Value(a % b);

                    case BinaryOp::Less:
                        return Value(a <  b);

                    case BinaryOp::LessEq:
                        return Value(a <= b);

                    case BinaryOp::Greater:
                        return Value(a >  b);

                    case BinaryOp::GreaterEq:
                        return Value(a >= b);
                    default: break;
                }
            }
            else
            {
                double a = as_double(l);
                double b = as_double(r);
                switch (n.op)
                {
                    case BinaryOp::Add:
                        return Value(a + b);

                    case BinaryOp::Sub:
                        return Value(a - b);

                    case BinaryOp::Mul:
                        return Value(a * b);

                    case BinaryOp::Div:
                        return Value(a / b);

                    case BinaryOp::Mod:
                        return Value(std::fmod(a, b));

                    case BinaryOp::Less:
                        return Value(a <  b);

                    case BinaryOp::LessEq:
                        return Value(a <= b);

                    case BinaryOp::Greater:
                        return Value(a >  b);

                    case BinaryOp::GreaterEq:
                        return Value(a >= b);

                    default: break;
                }
            }

            fail("unsupported operator for these operands", span);
        }

        using BuiltinFunc = std::function<Value(std::span<Value> args, SourceSpan caller)>;

        std::unordered_map<std::string, BuiltinFunc> _builtin_functions;
        std::unordered_map<std::string, const FnDecl*> _user_functions;
        std::vector<Scope> _scopes;
        Value _return;
        int _depth = 0;
    };


    std::optional<RuntimeError> run(std::span<const ast::DeclPtr> decls)
    {
        Interpreter i{decls};
        return i.run();
    }
}