#include <foo/runtime.hpp>

#include "foo/util/overloaded.hpp"

#include <cmath>
#include <iostream>
#include <utility>

#include "foo/natives.hpp"
#include "foo/debug/debug_out.hpp"

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
            Next, Return, Break, Continue
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

        ValueRef array_concat(const Array& a, const Array& b)
        {
            Array result;
            result.items.reserve(a.items.size() + b.items.size());

            for (const auto& x : a.items) result.items.push_back(copy(x));
            for (const auto& x : b.items) result.items.push_back(copy(x));

            return make_value(std::move(result));
        }

        ValueRef set_union(const Set& a, const Set& b)
        {
            Set result;

            for (const auto& x : a.items) result.items.insert(copy(x));
            for (const auto& x : b.items) result.items.insert(copy(x));

            return make_value(std::move(result));
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
            {
                if (const auto* a = std::get_if<std::string>(&l.v))
                    if (const auto* b = std::get_if<std::string>(&r.v))
                        return make_string(*a + *b);

                if (const auto* a = std::get_if<Array>(&l.v))
                    if (const auto* b = std::get_if<Array>(&r.v))
                    {
                        return array_concat(*a, *b);
                    }

                if (const auto* a = std::get_if<Set>(&l.v))
                    if (const auto* b = std::get_if<Set>(&r.v))
                        return set_union(*a, *b);

            }

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

            int file_index = -1;

            TypeId ret = BaseTypes::Void;

            // lambdas only
            Rc<std::vector<ValueRef>> captures;
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

        int64_t range_length(const Range& r)
        {
            const int64_t lo = as_int(r.lo);
            const int64_t hi = as_int(r.hi);

            if (r.inclusive ? lo > hi : lo >= hi)
                return 0;

            return static_cast<int64_t>(static_cast<uint64_t>(hi) - static_cast<uint64_t>(lo))
                 + (r.inclusive ? 1 : 0);
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
            return k == RefKind::Function || k == RefKind::Method || k == RefKind::Native
                || k == RefKind::NativeMethod || k == RefKind::Closure;
        }
    }

    class Runtime::Runner
    {
    public:
        Runner(Runtime& rt, RuntimeInput input)
            : _rt(rt), _input(std::move(input))
        {

        }

        std::optional<FunctionId> find_function(std::string_view name) const
        {
            const auto& functions = resolved().functions;

            for (size_t i = 0; i < functions.size(); i++)
                if (!functions[i].is_method && functions[i].owner_type == EmptyType
                    && functions[i].decl && functions[i].decl->name == name)
                    return static_cast<FunctionId>(i);

            return std::nullopt;
        }

        const RuntimeInput& get_input() const
        {
            return _input;
        }

        ValueResult call(FunctionId fn, Args args)
        {
            const auto& functions = resolved().functions;

            if (fn < 0 || static_cast<size_t>(fn) >= functions.size())
                return error("no such function");

            const FunctionInfo& f = functions[static_cast<size_t>(fn)];
            const size_t self = f.is_method ? 1 : 0;

            if (args.size() != f.sig.params.size() + self)
                return error(std::format("expected {} arguments, got {}", f.sig.params.size() + self, args.size()));

            std::vector<ValueRef> prepared;
            prepared.reserve(args.size());

            for (size_t i = 0; i < args.size(); i++)
            {
                const bool by_ref = i < self ? f.sig.self_by_ref : f.sig.params[i - self].by_ref;

                prepared.push_back(by_ref ? args[i] : copy(args[i]));
            }

            return call_function(fn, std::move(prepared), {});
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
            return error(what, _frames.empty() ? -1 : _frames.back().file_index, span);
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

        ValueResult capture(Id index)
        {
            const auto& captures = _frames.back().captures;

            if (!captures || index < 0 || static_cast<size_t>(index) >= captures->size())
                return error("internal: bad capture");

            return (*captures)[static_cast<size_t>(index)];
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

            // a variant value is a Boxed that remembers which alternative it holds,
            // `from` is that alternative (the resolver only allows exact members)
            if (types.kind(to) == TypeKind::Variant && from != to
                && from != BaseTypes::None && types.kind(from) != TypeKind::Variant)
            {
                if (v.use_count() != 1)
                    v = copy(v);

                return make_value(Boxed{std::move(v), from});
            }

            // a variant already is an `any` that can only hold a few types, so it doesn't get boxed again
            if (to == BaseTypes::Any && types.kind(from) == TypeKind::Variant)
                return v;

            if (to == BaseTypes::Any && from != BaseTypes::Any)
            {
                if (v.use_count() != 1)
                    v = copy(v);

                return make_value(Boxed{std::move(v), from});
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

            case TypeKind::Set:
                return make_value(Set{});

            case TypeKind::Void:
            case TypeKind::Optional:
                return make_void();

            case TypeKind::Tuple:
            {
                Tuple tuple;

                for (TypeId member : types.members(t))
                {
                    FOO_TRY(value, default_value(span, member));
                    tuple.items.push_back(std::move(value));
                }

                return make_value(std::move(tuple));
            }

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
                return error("internal: this type has no default value", span);
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
                [&](const TupleLit& x)                  { return eval_tuple_lit(e, x); },
                [&](const NoneLit&) -> ValueResult      { return make_void(); },
                [&](const StructLit& x)                 { return eval_struct_lit(e, x); },
                [&](const Name&)                        { return eval_name(e); },
                [&](const Unary& x)                     { return eval_unary(x); },
                [&](const Binary& x)                    { return eval_binary(e, x); },
                [&](const Index& x)                     { return eval_index(e, x); },
                [&](const Call& x)                      { return eval_call(e, x); },
                [&](const Member& x)                    { return eval_member(e, x); },
                [&](const StaticCall& x)                { return eval_static_call(e, x); },
                [&](const As& x)                        { return eval_as(e, x); },
                [&](const PatternAssign& x)             { return eval_pattern_assign(x); },
                [&](const Lambda& x)                    { return eval_lambda(e, x); }
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

        ValueResult eval_tuple_lit(const Expr& e, const TupleLit& x)
        {
            const TypeTable& types = resolved().type_table;
            const TypeId type = info(e).type;

            if (type < 0 || types.kind(type) != TypeKind::Tuple)
                return error("internal: tuple literal not resolved", e.span);

            // the type of the literal is the tuple it was converted to, every element converts to its own type
            const std::vector<TypeId>& members = types.members(type);

            Tuple t;
            t.items.reserve(x.elements.size());

            for (size_t i = 0; i < x.elements.size(); ++i)
            {
                FOO_TRY(value, val(*x.elements[i]));
                t.items.push_back(nudge_value(std::move(value), info(*x.elements[i]).type, members[i]));
            }

            return make_value(std::move(t));
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

            if (i.ref == RefKind::Capture)
                return capture(i.index);

            if (i.ref == RefKind::EnumVariant)
                return make_value(EnumValue{resolved().type_table.enum_id(i.type), static_cast<int>(i.index)});

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

            if (auto* tuple = std::get_if<Tuple>(&t.v))
            {
                const int64_t i = as_int(key);
                const auto at = index_in(i, tuple->items.size());

                if (!at)
                    return error(std::format("index {} out of range (size {})", i, tuple->items.size()), span);

                return tuple->items[*at];
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

        // value as T: the value when it holds a T, none otherwise
        ValueResult eval_as(const Expr& e, const As& x)
        {
            FOO_TRY(v, ref(*x.expr));

            // the type of this expression is optional<T>
            const TypeId requested = resolved().type_table.element(info(e).type);

            const auto* boxed = std::get_if<Boxed>(&v->v);
            if (!boxed || boxed->type != requested)
                return make_void();

            return copy(boxed->v);
        }

        ValueResult eval_lambda(const Expr& e, const Lambda& x)
        {
            const LambdaInfo& li = lookup(resolved().lambdas, &x, "lambda");

            auto captured = make<std::vector<ValueRef>>();
            captured->reserve(li.captures.size());

            for (const LambdaCapture& c : li.captures)
            {
                if (c.from == RefKind::Capture)
                {
                    FOO_TRY(value, capture(c.index));
                    captured->push_back(copy(value));
                }
                else
                {
                    FOO_TRY(value, local(c.index));
                    captured->push_back(copy(value));
                }
            }

            (void)e;
            return make_value(Closure{&x, std::move(captured)});
        }

        ValueResult eval_call(const Expr& e, const Call& c)
        {
            const ExprInfo* target = find_info(*c.callee);

            if (!target || !callable(target->ref))
                target = find_info(e);

            if (!target || !callable(target->ref))
                return error("internal: call target was not resolved");

            if (target->ref == RefKind::Closure)
                return eval_closure_call(e, c);

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

        ValueResult eval_closure_call(const Expr& e, const Call& c)
        {
            FOO_TRY(callee, ref(*c.callee));

            const auto* held = std::get_if<Closure>(&callee->v);

            if (!held || !held->lambda)
                return error("internal: calling something that is not a function", e.span);

            const Closure closure = *held;
            const LambdaInfo& li = lookup(resolved().lambdas, closure.lambda, "lambda");

            if (li.sig.params.size() != c.args.size())
                return error("internal: wrong number of arguments", e.span);

            std::vector<ValueRef> args;
            args.reserve(c.args.size());

            for (size_t i = 0; i < c.args.size(); i++)
            {
                const Expr& a = *c.args[i];

                FOO_TRY(argument, val(a));
                args.push_back(nudge_value(std::move(argument), info(a).type, li.sig.params[i].type));
            }

            if (li.slot_count < 0 || static_cast<size_t>(li.slot_count) < args.size())
                return error("internal: lambda has too few slots", e.span);

            if (_frames.size() >= _input.max_depth)
                return error("recursion limit exceeded", e.span);

            _frames.emplace_back();
            FramePopHelper pop{_frames};

            Frame& frame = _frames.back();
            frame.file_index = li.file_index;
            frame.ret = li.sig.ret;
            frame.captures = closure.captured;
            frame.slots.resize(static_cast<size_t>(li.slot_count));

            for (size_t i = 0; i < args.size(); i++)
                frame.slots[i] = std::move(args[i]);

            return run_body(closure.lambda->body, li.file_index, e.span);
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

            return call_native(target.index, args, sig, span);
        }

        ValueResult call_native(NativeFunctionId id, const std::vector<ValueRef>& args, const FunctionSig& sig, const SourceSpan& span)
        {
            const NativeFn* fn = _input.native_table.get(id);

            if (!fn)
                return error("native function has no implementation", span);

            try
            {
                ValueRef result = (*fn)(_rt, args, sig);

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

        ValueResult run_body(const Block& body, int file_index, const SourceSpan& span)
        {
            FOO_TRY(flow, exec_block(body));

            switch (flow)
            {
            case Flow::Break:
                return error("'break' outside of a loop", file_index, span);

            case Flow::Continue:
                return error("'continue' outside of a loop", file_index, span);

            case Flow::Return:
            case Flow::Next:
                break;
            }

            ValueRef result = std::move(_ret);
            _ret = nullptr;

            return result ? result : make_void();
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
            frame.file_index = f.file_index;
            frame.ret = f.sig.ret;
            frame.slots.resize(static_cast<size_t>(f.slot_count));

            for (size_t i = 0; i < expected; i++)
                frame.slots[i] = std::move(args[i]);

            return run_body(f.decl->body, f.file_index, f.span);
        }

        ValueResult lvalue(const Expr& e)
        {
            if (std::holds_alternative<Name>(e.kind))
            {
                const ExprInfo& i = info(e);

                if (i.ref == RefKind::Local)
                    return local(i.index);

                if (i.ref == RefKind::Capture)
                    return capture(i.index);
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

            return error("internal: expression cannot be assigned to", e.span);
        }

        ValueResult assign_to_place(const Binary& b)
        {

            FOO_TRY(value, val(*b.rhs));

            ValueRef r = nudge_value(std::move(value), info(*b.rhs).type, info(*b.lhs).type);

            FOO_TRY(place, lvalue(*b.lhs));

            place->v = std::move(r->v);
            return place;
        }

        // (a, (b.x, _)) = value
        ValueResult eval_pattern_assign(const PatternAssign& x)
        {
            // the value is evaluated and copied before anything is written, so (a, b) = (b, a) swaps
            FOO_TRY(value, val(*x.value));

            FOO_TRY(stored, bind_pattern(*x.pattern, std::move(value), info(*x.value).type));
            (void)stored;

            return make_void();
        }

        // stores `value` (whose type is value_type) in the pattern:
        // names get a slot, places get assigned, tuples are taken apart, and `_` throws it away
        ValueResult bind_pattern(const Pattern& p, ValueRef value, TypeId value_type)
        {
            if (std::holds_alternative<PatWild>(p.kind))
                return make_void();

            if (const auto* name = std::get_if<PatName>(&p.kind))
            {
                const LocalInfo& li = lookup(resolved().pattern_vars, name, "pattern variable");

                // the name converts the value to its own type (int to float, boxing into a variant, ...)
                bind(li.slot, nudge_value(std::move(value), value_type, li.type));
                return make_void();
            }

            if (const auto* place = std::get_if<PatPlace>(&p.kind))
            {
                FOO_TRY(target, lvalue(*place->place));

                ValueRef converted = nudge_value(std::move(value), value_type, info(*place->place).type);

                target->v = std::move(converted->v);
                return make_void();
            }

            const PatTuple& tuple = std::get<PatTuple>(p.kind);
            const std::vector<TypeId>& members = resolved().type_table.members(value_type);
            auto& items = expect<Tuple>(*value).items;

            if (items.size() != tuple.elements.size() || members.size() != items.size())
                return error("internal: destructuring size mismatch", p.span);

            for (size_t i = 0; i < items.size(); ++i)
            {
                FOO_TRY(done, bind_pattern(*tuple.elements[i], items[i], members[i]));
                (void)done;
            }

            return make_void();
        }


        FlowResult exec_block(const Block& b)
        {
            for (const StmtPtr& s : b.statements)
            {
                FOO_TRY(flow, exec(*s));

                if (flow != Flow::Next)
                    return flow;
            }

            return Flow::Next;
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
                [&](const Check& c)     { return exec_check(c); },
                [&](const Break&) -> FlowResult     { return Flow::Break; },
                [&](const Continue&) -> FlowResult  { return Flow::Continue; },
            }, s.kind);
        }

        FlowResult exec_var_decl(const Stmt& s, const VarDecl& v)
        {
            if (v.init)
            {
                FOO_TRY(value, val(*v.init));

                FOO_TRY(stored, bind_pattern(*v.pattern, std::move(value), info(*v.init).type));
                (void)stored;

                return Flow::Next;
            }

            // without an initializer the resolver guarantees a single name
            const PatName& name = std::get<PatName>(v.pattern->kind);
            const LocalInfo& li = lookup(resolved().pattern_vars, &name, "variable");

            FOO_TRY(value, default_value(s.span, li.type));
            bind(li.slot, std::move(value));

            return Flow::Next;
        }

        FlowResult exec_return(const Return& r)
        {
            if (!r.value)
            {
                _ret = make_void();
                return Flow::Return;
            }

            FOO_TRY(value, r.reffed ? ref(*r.value) : val(*r.value));

            _ret = nudge_value(std::move(value), info(*r.value).type, _frames.back().ret);
            return Flow::Return;
        }

        FlowResult exec_if(const If& i)
        {
            FOO_TRY(cond, ref(*i.cond));

            if (as_bool(cond))
                return exec(*i.then_branch);

            if (i.else_branch)
                return exec(*i.else_branch);

            return Flow::Next;
        }


        static bool leaves_loop(Flow flow)
            { return flow == Flow::Return || flow == Flow::Break; }

        static Flow flow_after_loop(Flow flow)
            { return flow == Flow::Break ? Flow::Next : flow; }


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

            return Flow::Next;
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

            return Flow::Next;
        }

        FlowResult exec_check(const Check& c)
        {
            FOO_TRY(value, c.reffed ? ref(*c.expr) : val(*c.expr));

            if (!std::holds_alternative<std::monostate>(value->v))
            {
                bind(lookup(resolved().check_vars, &c, "check variable").slot, std::move(value));
                return exec(*c.good_branch);
            }

            if (c.bad_branch)
                return exec(*c.bad_branch);

            return Flow::Next;
        }

        FlowResult exec_for(const Stmt& s, const ForIn& f)
        {
            const LocalInfo& li = lookup(resolved().for_vars, &f, "loop variable");

            FOO_TRY(it, f.reffed ? ref(*f.iterable) : val(*f.iterable));

            if (const auto* r = std::get_if<Range>(&it->v))
            {
                const int64_t lo = as_int(r->lo);
                const int64_t hi = as_int(r->hi);
                const bool inclusive = r->inclusive;

                if (inclusive ? lo > hi : lo >= hi)
                    return Flow::Next;

                for (int64_t i = lo;; i++)
                {
                    bind(li.slot, make_int(i));

                    FOO_TRY(flow, exec(*f.body));

                    if (leaves_loop(flow))
                        return flow_after_loop(flow);

                    if (inclusive ? i == hi : i + 1 == hi)
                        return Flow::Next;
                }
            }

            if (const auto* a = std::get_if<Array>(&it->v))
            {
                const std::vector<ValueRef> items = a->items;

                for (const ValueRef& element : items)
                {
                    bind(li.slot, f.reffed ? element : copy(element));

                    FOO_TRY(flow, exec(*f.body));

                    if (leaves_loop(flow))
                        return flow_after_loop(flow);
                }

                return Flow::Next;
            }

            if (const auto* m = std::get_if<Map>(&it->v))
            {
                std::vector<ValueRef> keys;
                keys.reserve(m->items.size());

                for (const auto& entry : m->items)
                    keys.push_back(entry.first);

                for (const ValueRef& key : keys)
                {
                    bind(li.slot, copy(key));

                    FOO_TRY(flow, exec(*f.body));

                    if (leaves_loop(flow))
                        return flow_after_loop(flow);
                }

                return Flow::Next;
            }

            if (const auto* m = std::get_if<Set>(&it->v))
            {
                std::vector<ValueRef> elements;
                elements.reserve(m->items.size());

                for (const auto& entry : m->items)
                    elements.push_back(entry);

                for (const ValueRef& element : elements)
                {
                    bind(li.slot, copy(element));

                    FOO_TRY(flow, exec(*f.body));

                    if (leaves_loop(flow))
                        return flow_after_loop(flow);
                }

                return Flow::Next;
            }

            return error(std::format("cannot iterate over this"), s.span);
        }
    };


    Runtime::Runtime(RuntimeInput input)
        : _runner(std::make_unique<Runner>(*this, std::move(input)))
    { }

    Runtime::~Runtime() = default;

    std::optional<FunctionId> Runtime::find_function(std::string_view name) const
    {
        return _runner->find_function(name);
    }

    Result<ValueRef, RuntimeError> Runtime::call(FunctionId fn, Args args)
    {
        try
        {
            return _runner->call(fn, args);
        }
        catch (const std::exception& ex)
        {
            return std::unexpected(RuntimeError{.what = ex.what(), .file_index = -1, .span = {}});
        }
    }

    Result<ValueRef, RuntimeError> Runtime::call(std::string_view name, Args args)
    {
        const std::optional<FunctionId> fn = _runner->find_function(name);

        if (!fn)
            return std::unexpected(RuntimeError{
                .what = std::format("no function named '{}'", name), .file_index = -1, .span = {}});

        return call(*fn, args);
    }

    const TypeTable& Runtime::types() const
    {
        return _runner->get_input().resolver_info.type_table;
    }

    const ResolveOutput& Runtime::resolved() const
    {
        return _runner->get_input().resolver_info;
    }

    void Runtime::inject(TypeTable& types, NativeRegistry& natives, NativeTable& table)
    {
        (void)types;

        auto signature = [](std::vector<ParamSig> params, TypeId ret, bool self_by_ref = false, bool variadic = false)
        {
            FunctionSig s;
            s.params = std::move(params);
            s.ret = ret;
            s.self_by_ref = self_by_ref;
            s.variadic = variadic;
            return s;
        };

        NativeDef def(types, natives, table);


        auto printer = [](bool newline)
        {
            return [newline](Runtime& rt, Args a, const auto&)
            {
                std::string line;

                for (size_t i = 0; i < a.size(); i++)
                {
                    if (i)
                        line += ' ';

                    line += to_string(*a[i], &rt.resolved());
                }

                if (newline)
                    line += '\n';

                std::cout << line;
                return make_void();
            };
        };

        FunctionSig print_sig = signature({ParamSig{BaseTypes::Any, false}}, BaseTypes::Void);
        print_sig.variadic = true;

        def.function("println", print_sig, printer(true), {"println(any...) -> void"});
        def.function("print", print_sig, printer(false), {"print(any...) -> void"});

        def.static_function("new",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Map)
                    return std::nullopt;

                return signature({}, receiver);
            },
            [](Runtime&, Args a, const auto&)
            {
                return make_value(Map{});
            },
            {"(map).new() -> (map)"});

        auto set_binary_sig = [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            if (ts.kind(receiver) != TypeKind::Set)
                return std::nullopt;

            return signature({ParamSig{receiver, false}}, receiver);
        };

        def.static_function("new",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Set)
                    return std::nullopt;

                return signature({}, receiver);
            },
            [](Runtime&, Args a, const auto&)
            {
                return make_value(Set{});
            },
            {"(set).new() -> (set)"});

        def.method("add",
        [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            if (ts.kind(receiver) != TypeKind::Set)
                return std::nullopt;

            return signature({ParamSig{ts.element(receiver), false}}, BaseTypes::Bool, true);
        },
        [](Runtime&, Args a, const auto&)
        {
            return make_bool(as_set(a[0]).items.insert(copy(a[1])).second);
        },
        {"(set).add(set.element) -> bool", "returns false if it was already present"});

        def.method("remove",
    [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
    {
        if (ts.kind(receiver) != TypeKind::Set)
            return std::nullopt;

        return signature({ParamSig{ts.element(receiver), false}}, BaseTypes::Bool, true);
    },
    [](Runtime&, Args a, const auto&)
    {
        return make_bool(as_set(a[0]).items.erase(a[1]) > 0);
    },
    {"(set).remove(set.element) -> bool", "returns false if it wasn't present"});

    def.method("contains",
    [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
    {
        if (ts.kind(receiver) != TypeKind::Set)
            return std::nullopt;

        return signature({ParamSig{ts.element(receiver), false}}, BaseTypes::Bool);
    },
    [](Runtime&, Args a, const auto&)
    {
        return make_bool(as_set(a[0]).items.contains(a[1]));
    },
    {"(set).contains(set.element) -> bool"});

    def.method("union", set_binary_sig,
    [](Runtime&, Args a, const auto&)
    {
        Set result;

        for (const auto& x : as_set(a[0]).items)
            result.items.insert(copy(x));
        for (const auto& x : as_set(a[1]).items)
            result.items.insert(copy(x));

        return make_value(std::move(result));
    },
    {"(set).union(set) -> set", "all elements from both sets"});

    def.method("intersect", set_binary_sig,
    [](Runtime&, Args a, const auto&)
    {
        const auto& other = as_set(a[1]).items;
        Set result;

        for (const auto& x : as_set(a[0]).items)
            if (other.contains(x))
                result.items.insert(copy(x));

        return make_value(std::move(result));
    },
    {"(set).intersect(set) -> set", "elements present in both sets"});

    def.method("difference", set_binary_sig,
    [](Runtime&, Args a, const auto&)
    {
        const auto& other = as_set(a[1]).items;
        Set result;

        for (const auto& x : as_set(a[0]).items)
            if (!other.contains(x))
                result.items.insert(copy(x));

        return make_value(std::move(result));
    },
    {"(set).difference(set) -> set", "elements in this set but not in the other"});

    def.method("is_subset",
        [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            if (ts.kind(receiver) != TypeKind::Set)
                return std::nullopt;

            return signature({ParamSig{receiver, false}}, BaseTypes::Bool);
        },
        [](Runtime&, Args a, const auto&)
        {
            const auto& other = as_set(a[1]).items;

            for (const auto& x : as_set(a[0]).items)
                if (!other.contains(x))
                    return make_bool(false);

            return make_bool(true);
        },
        {"(set).is_subset(set) -> bool"});

        def.static_function("fromv",
    [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
    {
        if (ts.kind(receiver) != TypeKind::Set)
            return std::nullopt;

        return signature(
            {ParamSig{ts.element(receiver), false}}, receiver,
            false, true);
    },
    [](Runtime&, Args a, const auto&)
    {
        Set result;

        for (const auto& arg : a)
            result.items.insert(copy(arg));

        return make_value(std::move(result));
    },
    {"(set).from(set.element...) -> (set)", "build a set from an elements, duplicates collapse", "@[int].from(1, 2, 3)"});

        def.static_function("from",
    [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
    {
        if (ts.kind(receiver) != TypeKind::Set)
            return std::nullopt;

        return signature(
            {ParamSig{ts.array_of(ts.element(receiver)), false}},
            receiver);
    },
    [](Runtime&, Args a, const auto&)
    {
        Set result;

        for (const auto& x : as_array(a[0]).items)
            result.items.insert(copy(x));

        return make_value(std::move(result));
    },
    {"(set).from(set.element[]) -> (set)", "build a set from an array, duplicates collapse", "@[int].from([1, 2, 3])"});


        def.method("to_array",
        [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
        {
            if (ts.kind(receiver) != TypeKind::Range)
                return std::nullopt;

            return signature({}, ts.array_of(ts.element(receiver)));
        },
        [](Runtime&, Args a, const auto&)
        {
            const Range& r = expect<Range>(*a[0]);
            const int64_t count = range_length(r);

            constexpr int64_t max_items = 10'000'000;
            if (count > max_items)
                throw std::runtime_error("range is too large to convert to an array");

            const int64_t lo = as_int(r.lo);

            Array result;
            result.items.reserve(static_cast<size_t>(count));

            for (int64_t i = 0; i < count; i++)
                result.items.push_back(make_int(lo + i));

            return make_value(std::move(result));
        },
        {"(range).to_array() -> int[]", "(0..3).to_array() is [0, 1, 2]"});

        def.static_function("unbox",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                return signature(
                    {ParamSig{BaseTypes::Any, false}},
                    ts.optional_of(receiver));
            },
            [](Runtime& rt, Args a, const FunctionSig& sig) -> ValueRef
            {
                const TypeTable& types = rt.types();
                const TypeId requested = types.element(sig.ret);

                if (std::holds_alternative<std::monostate>(a[0]->v))
                    return make_void();

                const auto* boxed = std::get_if<Boxed>(&a[0]->v);
                if (!boxed || boxed->type != requested)
                    return make_void();

                return copy(boxed->v);
            },
            {"T.unbox(any) -> T?", "use to safely convert any to T"});



        def.method("or",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) == TypeKind::Optional)
                    return signature({ParamSig{ts.element(receiver), false}}, ts.element(receiver));

                return std::nullopt;
            },
            [](Runtime&, Args a, const auto&)
            {
                if (std::holds_alternative<std::monostate>(a[0]->v))
                    return copy(a[1]);

                return copy(a[0]);
            },
            {"(T?).or(T) -> T", "safe default value for T?", "maybe_int().or(-1)"});

        def.method("unwrap",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) == TypeKind::Optional)
                    return signature({}, ts.element(receiver));

                return std::nullopt;
            },
            [](Runtime&, Args a, const auto&)
            {
                if (std::holds_alternative<std::monostate>(a[0]->v))
                    throw std::runtime_error{"unwrap failed!"};

                return copy(a[0]);
            },
            {"(T?).unwrap() -> T", "force unwrap T?, (throws if none)"});

        def.method("to_string",
            [=](TypeTable&, TypeId receiver) -> std::optional<FunctionSig>
            {
                return signature({}, BaseTypes::String);
            },
            [](Runtime& rt, Args a, const auto&) -> ValueRef
            {
                return make_value(to_string(*a[0], &rt.resolved()));
            },
            {"T.to_string() -> string", "convert any value to its string representation"});

        def.method("join",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Array)
                    return std::nullopt;

                if (ts.element(receiver) != BaseTypes::String)
                    return std::nullopt;

                return NativeDef::sig(
                    { ParamSig{BaseTypes::String, false} },
                    BaseTypes::String,
                    true
                );
            },
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;
                const std::string& separator = as_string(a[1]);

                std::string result;

                for (size_t i = 0; i < items.size(); ++i)
                {
                    if (i != 0)
                        result += separator;

                    result += as_string(items[i]);
                }

                return make_string(std::move(result));
            },
            {"(string[]).join(separator: string) -> string"});


        def.method("at",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) == TypeKind::Array)
                    return signature({ParamSig{BaseTypes::Int, false}}, ts.optional_of(ts.element(receiver)));

                if (ts.kind(receiver) == TypeKind::String)
                    return signature({ParamSig{BaseTypes::Int, false}}, ts.optional_of(BaseTypes::String));

                if (ts.kind(receiver) == TypeKind::Map)
                    return signature({ParamSig{ts.key(receiver), false}}, ts.optional_of(ts.value(receiver)));

                return std::nullopt;
            },
            [](Runtime&, Args a, const auto&)
            {

                if (const auto* map = std::get_if<Map>(&a[0]->v))
                {
                    const auto it = map->items.find(a[1]);

                    if (it == map->items.end())
                        return make_void();

                    return copy(it->second);
                }

                const int64_t i = as_int(a[1]);

                if (const auto* arr = std::get_if<Array>(&a[0]->v))
                {
                    if (i < 0 || static_cast<uint64_t>(i) >= arr->items.size())
                        return make_void();

                    return copy(arr->items[static_cast<size_t>(i)]);
                }

                const std::string& s = as_string(a[0]);

                if (i < 0 || static_cast<uint64_t>(i) >= s.size())
                    return make_void();

                return make_string(std::string(1, s[static_cast<size_t>(i)]));
            },
            {"(array | string).at(int) -> element | (map).at(key) -> map.value?"});


        def.method("length",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                switch (ts.kind(receiver))
                {
                case TypeKind::Array:
                case TypeKind::Map:
                case TypeKind::String:
                    case TypeKind::Set:
                    return signature({}, BaseTypes::Int);

                default:
                    return std::nullopt;
                }
            },
            [](Runtime&, Args a, const auto&)
            {
                const Value& v = *a[0];

                if (const auto* arr = std::get_if<Array>(&v.v))
                    return make_int(static_cast<int64_t>(arr->items.size()));

                if (const auto* m = std::get_if<Map>(&v.v))
                    return make_int(static_cast<int64_t>(m->items.size()));

                if (const auto* s = std::get_if<Set>(&v.v))
                    return make_int(static_cast<int64_t>(s->items.size()));

                return make_int(static_cast<int64_t>(as_string(a[0]).size()));
            },
            {"(map | array | string).length() -> int"});

        def.method("push",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Array)
                    return std::nullopt;

                return signature({ParamSig{ts.element(receiver), false}}, BaseTypes::Void, true);
            },
            [](Runtime&, Args a, const auto&)
            {
                as_array(a[0]).items.push_back(copy(a[1]));
                return make_void();
            },
            {"(array).push(array.element) -> void"});

        def.method("pop",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Array)
                    return std::nullopt;

                return signature({}, ts.optional_of(ts.element(receiver)), true);
            },
            [](Runtime&, Args a, const auto&)
            {
                auto& items = as_array(a[0]).items;

                if (items.empty())
                    return make_void();

                ValueRef last = std::move(items.back());
                items.pop_back();
                return last;
            },
            {"(array).pop() -> void"});

        def.method("clear",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (
                    ts.kind(receiver) != TypeKind::Array &&
                    ts.kind(receiver) != TypeKind::Map &&
                    ts.kind(receiver) != TypeKind::Set)
                        return std::nullopt;

                return signature({}, BaseTypes::Void, true);
            },
            [](Runtime&, Args a, const auto&)
            {
                if (auto* arr = std::get_if<Array>(&a[0]->v))
                    arr->items.clear();
                else if (auto* set = std::get_if<Set>(&a[0]->v))
                    set->items.clear();
                else
                    as_map(a[0]).items.clear();

                return make_void();
            },
            {"(array | map).clear() -> void"});

            def.method("remove",
                [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
                {
                    if (ts.kind(receiver) != TypeKind::Map)
                        return std::nullopt;

                    return signature({ParamSig{ts.key(receiver), false}}, BaseTypes::Bool, true);
                },
                [](Runtime&, Args a, const auto&)
                {
                    return make_bool(as_map(a[0]).items.erase(a[1]) > 0);
                },
                {"(map).remove() -> bool"});

        def.method("contains",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) == TypeKind::Map)
                    return signature({ParamSig{ts.key(receiver), false}}, BaseTypes::Bool);

                if (ts.kind(receiver) == TypeKind::Array)
                    return signature({ParamSig{ts.element(receiver), false}}, BaseTypes::Bool);

                return std::nullopt;
            },
            [](Runtime&, Args a, const auto&)
            {
                if (const auto* m = std::get_if<Map>(&a[0]->v))
                    return make_bool(m->items.contains(a[1]));

                for (const ValueRef& item : as_array(a[0]).items)
                    if (*item == *a[1])
                        return make_bool(true);

                return make_bool(false);
            },
            {"(map | array).contains((key | element)) -> bool"});

        def.method("reverse",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Array)
                    return std::nullopt;

                return signature({}, BaseTypes::Void, true);
            },
            [](Runtime&, Args a, const auto&)
            {
                auto& items = as_array(a[0]).items;
                std::reverse(items.begin(), items.end());
                return make_void();
            },
            {"(T[]).reverse() -> void", "reverses in place"});

        def.method("reversed",
            [=](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Array)
                    return std::nullopt;

                return signature({}, receiver);
            },
            [](Runtime&, Args a, const auto&)
            {
                Array result = as_array(copy(a[0]));
                std::reverse(result.items.begin(), result.items.end());
                return make_value(std::move(result));
            },
            {"(T[]).reversed() -> T[]", "returns a reversed copy"});


        def.function("debug::assert", {{ParamSig{BaseTypes::Bool}}},
            [](Runtime&, Args a, const auto&)
            {
                bool cond = as_bool(a[0]);

                if (cond == false)
                    throw std::runtime_error("debugging: assertion failed!");

                return make_void();
            },
            {"debug::assert(cond: bool)"});

        def.function("debug::assert_eq", {{ParamSig{BaseTypes::Any}, ParamSig{BaseTypes::Any}}},
            [](Runtime&, Args a, const auto&)
            {
                const auto& _1 = a[0];
                const auto& _2 = a[1];

                if (*_1 != *_2)
                    throw std::runtime_error("debugging: assertion eq failed!");


                return make_void();
            },
            {"debug::assert_eq(any, any)"});
    }
}
