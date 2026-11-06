#include <foo/runtime.hpp>

#include "foo/util/overloaded.hpp"

#include <cmath>
#include <utility>

namespace foo
{

    #define FOO_TRY(name, expr)                                         \
        auto name##_result = (expr);                                    \
        if (!name##_result)                                             \
            return std::unexpected(std::move(name##_result.error()));   \
        auto name = std::move(*name##_result)

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

        template<class M, class K>
        const auto& lookup(const M& m, const K* key, const char* what)
        {
            auto it = m.find(key);
            if (it == m.end())
                throw std::runtime_error(std::format("internal: unresolved {}", what));

            return it->second;
        }

        std::optional<size_t> index_in(int64_t i, size_t size)
        {
            if (i < 0 || static_cast<uint64_t>(i) >= size)
                return std::nullopt;

            return static_cast<size_t>(i);
        }

        const ParamSig* param_for(const FunctionSig& sig, size_t arg)
        {
            if (arg < sig.params.size())
                return &sig.params[arg];

            if (sig.variadic && !sig.params.empty())
                return &sig.params.back();

            return nullptr;
        }

        bool callable(RefKind k)
        {
            return k == RefKind::Function || k == RefKind::Method || k == RefKind::Native || k == RefKind::NativeMethod;
        }
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

        std::unexpected<RuntimeError> error(const std::string& what, const SourceSpan& span) const
        {
            const FunctionInfo* fn = _frames.empty() ? nullptr : _frames.back().fn;

            return error(what, fn ? fn->file_index : -1, span);
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

        const ExprInfo* find_info(const Expr& e) const
        {
            auto it = resolved().exprs.find(&e);
            return it == resolved().exprs.end() ? nullptr : &it->second;
        }

        const StructInfo& struct_info(Id id) const
        {
            if (id < 0 || static_cast<size_t>(id) >= resolved().structs.size())
                throw std::runtime_error("internal: bad struct id");

            return resolved().structs[static_cast<size_t>(id)];
        }

        const ExprInfo& info(const Expr& e) const
        {
            if (const ExprInfo* i = find_info(e))
                return *i;

            throw std::runtime_error("internal: unresolved expression");
        }

        std::vector<ValueRef>& slots() { return _frames.back().slots; }

        ValueResult local(SlotId s)
        {
            auto& all = slots();

            if (s < 0 || static_cast<size_t>(s) >= all.size())
                return error("internal: bad slot");

            if (!all[static_cast<size_t>(s)])
                return error("internal: use of an uninitialized variable");

            return all[static_cast<size_t>(s)];
        }

        void bind(SlotId s, ValueRef v)
        {
            auto& all = slots();

            if (s < 0 || static_cast<size_t>(s) >= all.size())
                throw std::runtime_error("internal: bad slot");

            all[static_cast<size_t>(s)] = std::move(v);
        }


        ValueRef nudge_value(ValueRef v, TypeId from, TypeId to)
        {
            const TypeTable& types = resolved().type_table;

            while (types.kind(to) == TypeKind::Optional && types.kind(from) != TypeKind::Optional)
                to = types.element(to);

            if (to == BaseTypes::Any && from != BaseTypes::Any)
            {
                if (v.use_count() != 1)
                    v = copy(v);

                return make_value(Boxed{std::move(v)});
            }

            if (to == BaseTypes::Float && from == BaseTypes::Int)
                return make_float(static_cast<double>(as_int(v)));

            return v;
        }

        ValueResult default_value(const SourceSpan& span, TypeId t)
        {
            if (t < 0)
                return error("internal: untyped value");

            const TypeTable& types = resolved().type_table;

            switch (types.kind(t))
            {
            case TypeKind::Bool:
                return make_bool(false);

            case TypeKind::Int:
                return make_int(0);

            case TypeKind::Float:
                return make_float(0.0);

            case TypeKind::String:
                return make_string({});

            case TypeKind::Array:
                return make_value(Array{});

            case TypeKind::Map:
                return make_value(Map{});

            case TypeKind::Void:
            case TypeKind::Optional:
                return make_void();

            case TypeKind::Struct:
            {
                Struct s;

                s.fields.reserve(struct_info(types.struct_id(t)).fields.size());
                for (const FieldInfo& f : struct_info(types.struct_id(t)).fields)
                {
                    FOO_TRY(value, default_value(span, f.type));
                    s.fields.push_back(std::move(value));
                }

                return make_value(std::move(s));
            }

            default:
                return error("this type has no default value", span);
            }
        }

        ValueResult val(const Expr& e)
        {
            FOO_TRY(r, ref(e));

            if (r.use_count() == 1)
                return r;

            return copy(r);
        }

        ValueResult ref(const Expr& e)
        {
            return std::visit(overload{
                [&](const IntLit& x) -> ValueResult     { return make_int(x.value); },
                [&](const FloatLit& x) -> ValueResult   { return make_float(x.value); },
                [&](const StringLit& x) -> ValueResult  { return make_string(x.value); },
                [&](const BoolLit& x) -> ValueResult    { return make_bool(x.value); },
                [&](const ArrayLit& x)                  { return eval_array(e, x); },
                [&](const StructLit& x)                 { return eval_struct_lit(e, x); },
                [&](const Name&)                        { return eval_name(e); },
                [&](const Unary& x)                     { return eval_unary(x); },
                [&](const Binary& x)                    { return eval_binary(e, x); },
                [&](const Index& x)                     { return eval_index(e, x); },
                [&](const Call& x)                      { return eval_call(e, x); },
                [&](const Member& x)                    { return eval_member(e, x); },
                [&](const StaticCall& x)                { return eval_static_call(e, x); }
            }, e.kind);
        }

        ValueResult eval_array(const Expr& e, const ArrayLit& x)
        {
            const TypeTable& types = resolved().type_table;
            const TypeId type = info(e).type;
            const TypeId element = (type >= 0 && types.kind(type) == TypeKind::Array) ? types.element(type) : EmptyType;

            Array a;
            a.items.reserve(x.elements.size());

            for (const ExprPtr& el : x.elements)
            {
                FOO_TRY(value, val(*el));
                a.items.push_back(nudge_value(std::move(value), info(*el).type, element));
            }

            return make_value(std::move(a));
        }

        ValueResult eval_struct_lit(const Expr& e, const StructLit& lit)
        {
            const ExprInfo& i = info(e);

            if (i.ref != RefKind::StructLit)
                return error("internal: struct literal not resolved");

            const StructInfo& s = struct_info(i.index);
            const std::vector<int>& order = lookup(resolved().struct_lit_fields, &lit, "struct literal");

            if (order.size() != lit.fields.size())
                return error("internal: struct literal field mismatch");

            Struct v;
            v.fields.resize(s.fields.size());

            for (size_t k = 0; k < lit.fields.size(); k++)
            {
                const int idx = order[k];

                if (idx < 0 || static_cast<size_t>(idx) >= s.fields.size())
                    return error("internal: bad field index");

                const Expr& fe = *lit.fields[k].value;

                FOO_TRY(value, val(fe));
                v.fields[static_cast<size_t>(idx)] =
                    nudge_value(std::move(value), info(fe).type, s.fields[static_cast<size_t>(idx)].type);
            }

            for (size_t idx = 0; idx < s.fields.size(); idx++)
                if (!v.fields[idx])
                {
                    FOO_TRY(value, default_value(e.span, s.fields[idx].type));
                    v.fields[idx] = std::move(value);
                }

            return make_value(std::move(v));
        }

        ValueResult eval_name(const Expr& e)
        {
            const ExprInfo& i = info(e);

            if (i.ref == RefKind::Local)
                return local(i.index);

            return error("internal: a name that is not a value");
        }

        ValueResult eval_unary(const Unary& x)
        {
            FOO_TRY(v, ref(*x.operant));

            switch (x.op)
            {
            case UnaryOp::Neg:
                if (const auto* i = std::get_if<int64_t>(&v->v))
                    return make_int(static_cast<int64_t>(0 - static_cast<uint64_t>(*i)));

                return make_float(-expect<double>(*v));

            case UnaryOp::Not:
                return make_bool(!as_bool(v));
            }

            return error("internal: bad unary operator");
        }

        ValueResult eval_binary(const Expr& e, const Binary& b)
        {
            switch (b.op)
            {
            case BinaryOp::Assign:
            {
                FOO_TRY(place, assign_to_place(b));
                (void)place;

                return make_void();
            }

            case BinaryOp::And:
            {
                FOO_TRY(l, ref(*b.lhs));

                if (!as_bool(l))
                    return make_bool(false);

                FOO_TRY(r, ref(*b.rhs));
                return make_bool(as_bool(r));
            }

            case BinaryOp::Or:
            {
                FOO_TRY(l, ref(*b.lhs));

                if (as_bool(l))
                    return make_bool(true);

                FOO_TRY(r, ref(*b.rhs));
                return make_bool(as_bool(r));
            }

            case BinaryOp::Range:
            case BinaryOp::RangeInclusive:
            {
                FOO_TRY(lo, val(*b.lhs));
                FOO_TRY(hi, val(*b.rhs));

                return make_value(Range(std::move(lo), std::move(hi), b.op == BinaryOp::RangeInclusive));
            }

            default:
                break;
            }

            FOO_TRY(l, ref(*b.lhs));
            FOO_TRY(r, ref(*b.rhs));

            switch (b.op)
            {
            case BinaryOp::Eq:
            case BinaryOp::NotEq:
            case BinaryOp::Less:
            case BinaryOp::LessEq:
            case BinaryOp::Greater:
            case BinaryOp::GreaterEq:
            {
                const auto result = comparison(b.op, *l, *r);

                if (result)
                    return result.value();

                return error(result.error(), e.span);
            }

            default:
                break;
            }

            auto result = arithmetic(b.op, *l, *r);

            if (!result)
                return error(result.error(), e.span);

            return *result;
        }

        ValueResult field(const ValueRef& target, Id index)
        {
            Struct& s = expect<Struct>(*target);

            if (index < 0 || static_cast<size_t>(index) >= s.fields.size())
                return error("internal: bad field index");

            return s.fields[static_cast<size_t>(index)];
        }

        ValueResult eval_member(const Expr& e, const Member& m)
        {
            const ExprInfo& i = info(e);

            if (i.ref != RefKind::Field)
                return error("internal: a member that is not a field");

            FOO_TRY(target, ref(*m.target));
            return field(target, i.index);
        }

        ValueResult index_into(const SourceSpan& span, const ValueRef& target, const ValueRef& key, TypeId target_type, bool create)
        {
            Value& t = *target;

            if (auto* a = std::get_if<Array>(&t.v))
            {
                const int64_t i = as_int(key);
                const auto at = index_in(i, a->items.size());

                if (!at)
                    return error(std::format("index {} out of range (size {})", i, a->items.size()), span);

                return a->items[*at];
            }

            if (auto* m = std::get_if<Map>(&t.v))
            {
                auto it = m->items.find(key);

                if (it != m->items.end())
                    return it->second;

                if (!create)
                    return error(std::format("key {} not found in map", to_string(*key)), span);

                FOO_TRY(value, default_value(span, resolved().type_table.value(target_type)));
                m->items.emplace(copy(key), value);

                return value;
            }

            if (auto* s = std::get_if<std::string>(&t.v))
            {
                const int64_t i = as_int(key);
                const auto at = index_in(i, s->size());

                if (!at)
                    return error(std::format("index {} out of range (size {})", i, s->size()), span);

                return make_string(std::string(1, (*s)[*at]));
            }

            return error("cannot index here");
        }

        ValueResult index_key(const Index& x)
        {
            const TypeTable& types = resolved().type_table;
            const TypeId target = info(*x.target).type;
            const TypeId from = info(*x.indexer).type;

            if (target >= 0 && types.kind(target) == TypeKind::Map && from != types.key(target))
            {
                FOO_TRY(key, val(*x.indexer));
                return nudge_value(std::move(key), from, types.key(target));
            }

            return ref(*x.indexer);
        }

        ValueResult eval_index(const Expr& e, const Index& x)
        {
            FOO_TRY(target, ref(*x.target));
            FOO_TRY(key, index_key(x));

            return index_into(e.span, target, key, info(*x.target).type, false);
        }

        ValueResult eval_call(const Expr& e, const Call& c)
        {
            const ExprInfo* target = find_info(*c.callee);

            if (!target || !callable(target->ref))
                target = find_info(e);

            if (!target || !callable(target->ref))
                return error("internal: call target was not resolved");

            const Expr* self = nullptr;

            if (target->ref == RefKind::Method || target->ref == RefKind::NativeMethod)
            {
                const auto* m = std::get_if<Member>(&c.callee->kind);

                if (!m)
                    return error("internal: method call without a receiver");

                self = m->target.get();
            }

            return invoke(*target, self, c.args, &c, e.span);
        }

        ValueResult eval_static_call(const Expr& e, const StaticCall& c)
        {
            const ExprInfo& target = info(e);

            if (!callable(target.ref))
                return error("internal: static call was not resolved");

            return invoke(target, nullptr, c.args, &c, e.span);
        }

        ValueResult invoke(const ExprInfo& target, const Expr* self, const std::vector<ExprPtr>& arg_exprs, const void* call_node, const SourceSpan& span)
        {
            std::vector<ValueRef> args;
            args.reserve(arg_exprs.size() + 1);

            if (target.ref == RefKind::Function || target.ref == RefKind::Method)
            {
                const FunctionInfo& f = function(target.index);

                if (f.sig.params.size() != arg_exprs.size())
                    return error("internal: wrong number of arguments");

                if (self)
                {
                    if (f.sig.self_by_ref)
                    {
                        FOO_TRY(receiver, ref(*self));
                        args.push_back(std::move(receiver));
                    }
                    else
                    {
                        FOO_TRY(receiver, val(*self));
                        args.push_back(std::move(receiver));
                    }
                }

                for (size_t i = 0; i < arg_exprs.size(); i++)
                {
                    const ParamSig& p = f.sig.params[i];
                    const Expr& a = *arg_exprs[i];

                    if (p.by_ref)
                    {
                        FOO_TRY(argument, ref(a));
                        args.push_back(std::move(argument));
                    }
                    else
                    {
                        FOO_TRY(argument, val(a));
                        args.push_back(nudge_value(std::move(argument), info(a).type, p.type));
                    }
                }

                return call_function(target.index, std::move(args), span);
            }

            const FunctionSig& sig = lookup(resolved().native_sigs, call_node, "native signature");

            if (self)
            {
                FOO_TRY(receiver, ref(*self));
                args.push_back(std::move(receiver));
            }

            for (size_t i = 0; i < arg_exprs.size(); i++)
            {
                const ParamSig* p = param_for(sig, i);
                const Expr& a = *arg_exprs[i];

                FOO_TRY(argument, ref(a));

                if (!p || p->by_ref)
                    args.push_back(std::move(argument));
                else
                    args.push_back(nudge_value(std::move(argument), info(a).type, p->type));
            }

            return call_native(target.index, args, span);
        }

        ValueResult call_native(NativeFunctionId id, const std::vector<ValueRef>& args, const SourceSpan& span)
        {
            const NativeFn* fn = _input.native_table.get(id);

            if (!fn)
                return error("native function has no implementation", span);

            try
            {
                ValueRef result = (*fn)(_rt, Args(args));

                if (!result)
                    return make_void();

                if (result.use_count() != 1)
                    return copy(result);

                return result;
            }
            catch (const RuntimeError& e)
            {
                return std::unexpected(e);
            }
            catch (const std::exception& ex)
            {
                return error(ex.what(), span);
            }
        }

        ValueResult call_function(FunctionId id, std::vector<ValueRef> args, const SourceSpan& call_span)
        {
            const FunctionInfo& f = function(id);

            if (!f.decl)
                return error("function has no body", f);

            const size_t expected = f.sig.params.size() + (f.is_method ? 1 : 0);

            if (args.size() != expected)
                return error("internal: wrong number of arguments");

            if (f.slot_count < 0 || static_cast<size_t>(f.slot_count) < expected)
                return error("internal: function has too few slots");

            if (_frames.size() >= _input.max_depth)
                return error("recursion limit exceeded", call_span);

            _frames.emplace_back();
            FramePopHelper pop{_frames};

            Frame& frame = _frames.back();
            frame.fn = &f;
            frame.slots.resize(static_cast<size_t>(f.slot_count));

            for (size_t i = 0; i < expected; i++)
                frame.slots[i] = std::move(args[i]);

            FOO_TRY(flow, exec_block(f.decl->body));

            ValueRef result;

            switch (flow)
            {
            case Flow::Return:
                result = std::move(_ret);
                break;

            case Flow::Break:
                return error("'break' outside of a loop", f);

            case Flow::Continue:
                break;
            }

            _ret = nullptr;

            return result ? result : make_void();
        }

        ValueResult lvalue(const Expr& e)
        {
            if (std::holds_alternative<Name>(e.kind))
            {
                const ExprInfo& i = info(e);

                if (i.ref == RefKind::Local)
                    return local(i.index);
            }
            else if (const auto* m = std::get_if<Member>(&e.kind))
            {
                const ExprInfo& i = info(e);

                if (i.ref == RefKind::Field)
                {
                    FOO_TRY(target, lvalue(*m->target));
                    return field(target, i.index);
                }
            }
            else if (const auto* x = std::get_if<Index>(&e.kind))
            {
                FOO_TRY(target, lvalue(*x->target));
                FOO_TRY(key, index_key(*x));

                return index_into(e.span, target, key, info(*x->target).type, true);
            }

            return error("expression cannot be assigned to", e.span);
        }

        ValueResult assign_to_place(const Binary& b)
        {
            FOO_TRY(value, val(*b.rhs));

            ValueRef r = nudge_value(std::move(value), info(*b.rhs).type, info(*b.lhs).type);

            FOO_TRY(place, lvalue(*b.lhs));

            place->v = std::move(r->v);
            return place;
        }


        FlowResult exec_block(const Block& b)
        {
            for (const StmtPtr& s : b.statements)
            {
                FOO_TRY(flow, exec(*s));

                if (flow != Flow::Continue)
                    return flow;
            }

            return Flow::Continue;
        }

        FlowResult exec(const Stmt& s)
        {
            return std::visit(overload{
                [&](const Block& b)     { return exec_block(b); },
                [&](const VarDecl& v)   { return exec_var_decl(s, v); },
                [&](const Return& r)    { return exec_return(r); },
                [&](const If& i)        { return exec_if(i); },
                [&](const While& w)     { return exec_while(w); },
                [&](const ForIn& f)     { return exec_for(s, f); },
                [&](const ExprStmt& x)  { return exec_expr(x); },
                [&](const Check& c)     { return exec_check(c); }
            }, s.kind);
        }

        FlowResult exec_var_decl(const Stmt& s, const VarDecl& v)
        {
            const LocalInfo& li = lookup(resolved().var_decls, &v, "variable");

            if (v.init)
            {
                FOO_TRY(value, val(*v.init));
                bind(li.slot, nudge_value(std::move(value), info(*v.init).type, li.type));
            }
            else
            {
                FOO_TRY(value, default_value(s.span, li.type));
                bind(li.slot, std::move(value));
            }

            return Flow::Continue;
        }

        FlowResult exec_return(const Return& r)
        {
            if (!r.value)
            {
                _ret = make_void();
                return Flow::Return;
            }

            FOO_TRY(value, val(*r.value));

            _ret = nudge_value(std::move(value), info(*r.value).type, _frames.back().fn->sig.ret);
            return Flow::Return;
        }

        FlowResult exec_if(const If& i)
        {
            FOO_TRY(cond, ref(*i.cond));

            if (as_bool(cond))
                return exec(*i.then_branch);

            if (i.else_branch)
                return exec(*i.else_branch);

            return Flow::Continue;
        }


        static bool leaves_loop(Flow flow)
            { return flow != Flow::Continue; }

        static Flow flow_after_loop(Flow flow)
            { return flow == Flow::Break ? Flow::Continue : flow; }

        FlowResult exec_while(const While& w)
        {
            while (true)
            {
                FOO_TRY(cond, ref(*w.cond));

                if (!as_bool(cond))
                    break;

                FOO_TRY(flow, exec(*w.body));

                if (leaves_loop(flow))
                    return flow_after_loop(flow);
            }

            return Flow::Continue;
        }

        FlowResult exec_expr(const ExprStmt& x)
        {
            const Expr& e = *x.expr;

            if (const auto* b = std::get_if<Binary>(&e.kind); b && b->op == BinaryOp::Assign)
            {
                FOO_TRY(place, assign_to_place(*b));
                (void)place;
            }
            else
            {
                FOO_TRY(value, ref(e));
                (void)value;
            }

            return Flow::Continue;
        }

        FlowResult exec_check(const Check& c)
        {
            FOO_TRY(value, val(*c.expr));

            if (!std::holds_alternative<std::monostate>(value->v))
            {
                bind(lookup(resolved().check_vars, &c, "check variable").slot, std::move(value));
                return exec(*c.good_branch);
            }

            if (c.bad_branch)
                return exec(*c.bad_branch);

            return Flow::Continue;
        }

        FlowResult exec_for(const Stmt& s, const ForIn& f)
        {
            const LocalInfo& li = lookup(resolved().for_vars, &f, "loop variable");

            FOO_TRY(it, val(*f.iterable));

            if (const auto* r = std::get_if<Range>(&it->v))
            {
                const int64_t lo = as_int(r->lo);
                const int64_t hi = as_int(r->hi);
                const bool inclusive = r->inclusive;

                if (inclusive ? lo > hi : lo >= hi)
                    return Flow::Continue;

                for (int64_t i = lo;; i++)
                {
                    bind(li.slot, make_int(i));

                    FOO_TRY(flow, exec(*f.body));

                    if (leaves_loop(flow))
                        return flow_after_loop(flow);

                    if (inclusive ? i == hi : i + 1 == hi)
                        return Flow::Continue;
                }
            }

            if (const auto* a = std::get_if<Array>(&it->v))
            {
                for (const ValueRef& element : a->items)
                {
                    bind(li.slot, copy(element));

                    FOO_TRY(flow, exec(*f.body));

                    if (leaves_loop(flow))
                        return flow_after_loop(flow);
                }

                return Flow::Continue;
            }

            if (const auto* m = std::get_if<Map>(&it->v))
            {
                for (const auto& entry : m->items)
                {
                    bind(li.slot, copy(entry.first));

                    FOO_TRY(flow, exec(*f.body));

                    if (leaves_loop(flow))
                        return flow_after_loop(flow);
                }

                return Flow::Continue;
            }

            return error(std::format("cannot iterate over this"), s.span);
        }
    };


    Runtime::Runtime(RuntimeInput input)
        : _runner(std::make_unique<Runner>(*this, std::move(input)))
    { }

    Runtime::~Runtime() = default;

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
