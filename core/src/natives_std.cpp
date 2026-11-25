#include <foo/natives.hpp>
#include <foo/runtime.hpp>
#include <foo/value.hpp>

#include <foo/util/str.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <expected>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace foo
{
    namespace
    {
        namespace bt = BaseTypes;


        bool is_none(const ValueRef& v)
        {
            return std::holds_alternative<std::monostate>(v->v);
        }

        bool is_int_value(const ValueRef& v)
        {
            return std::holds_alternative<int64_t>(v->v);
        }

        std::vector<ValueRef> items_of(const ValueRef& v)
        {
            if (const auto* array = std::get_if<Array>(&v->v))
                return array->items;

            if (const auto* set = std::get_if<Set>(&v->v))
                return std::vector<ValueRef>(set->items.begin(), set->items.end());

            const Range& range = expect<Range>(*v);
            const int64_t lo = as_int(range.lo);
            const int64_t hi = as_int(range.hi);

            if (range.inclusive ? lo > hi : lo >= hi)
                return {};

            const uint64_t count = static_cast<uint64_t>(hi) - static_cast<uint64_t>(lo) + (range.inclusive ? 1 : 0);

            if (count > 10'000'000)
                throw std::runtime_error("range is too large");

            std::vector<ValueRef> out;
            out.reserve(static_cast<size_t>(count));

            for (uint64_t i = 0; i < count; i++)
                out.push_back(make_int(lo + static_cast<int64_t>(i)));

            return out;
        }

        ValueRef invoke_fn(Runtime& rt, const ValueRef& fn, std::initializer_list<ValueRef> args)
        {
            std::vector<ValueRef> copies;
            copies.reserve(args.size());

            for (const ValueRef& x : args)
                copies.push_back(copy(x));

            return rt.call_closure(fn, std::move(copies));
        }

        bool test(Runtime& rt, const ValueRef& fn, const ValueRef& x)
        {
            return as_bool(invoke_fn(rt, fn, {x}));
        }

        std::pair<size_t, size_t> clamp_range(int64_t start, int64_t end, size_t size)
        {
            const int64_t n = static_cast<int64_t>(size);

            start = std::clamp<int64_t>(start, 0, n);
            end = std::clamp<int64_t>(end, 0, n);

            if (end < start)
                end = start;

            return {static_cast<size_t>(start), static_cast<size_t>(end)};
        }

        std::vector<ValueRef> ordered_by(Runtime& rt, const std::vector<ValueRef>& items, const ValueRef& key_fn)
        {
            std::vector<std::pair<ValueRef, ValueRef>> keyed;
            keyed.reserve(items.size());

            for (const ValueRef& x : items)
                keyed.emplace_back(invoke_fn(rt, key_fn, {x}), x);

            std::stable_sort(keyed.begin(), keyed.end(),
                [](const auto& l, const auto& r) { return *l.first < *r.first; });

            std::vector<ValueRef> out;
            out.reserve(keyed.size());

            for (auto& entry : keyed)
                out.push_back(entry.second);

            return out;
        }



        std::optional<TypeId> iter_element(const TypeTable& ts, TypeId receiver)
        {
            switch (ts.kind(receiver))
            {
                case TypeKind::Array:
                case TypeKind::Set:
                case TypeKind::Range:
                    return ts.element(receiver);

                default:
                    return std::nullopt;
            }
        }

        NativeResolver iter_with_fn(TypeId fn_ret, std::function<TypeId(TypeTable&, TypeId)> ret, bool plain = false)
        {
            return [fn_ret, ret = std::move(ret), plain](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                const auto element = iter_element(ts, receiver);

                if (!element)
                    return std::nullopt;

                if (plain && ts.kind(*element) == TypeKind::Optional)
                    return std::nullopt;

                return NativeDef::sig({ParamSig{ts.function_of({*element}, fn_ret), false}}, ret(ts, *element));
            };
        }

        NativeResolver self_method(std::vector<TypeId> receivers, size_t extra = 0, std::optional<TypeId> ret = std::nullopt)
        {
            return [receivers = std::move(receivers), extra, ret](TypeTable&, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (std::ranges::find(receivers, receiver) == receivers.end())
                    return std::nullopt;

                return NativeDef::sig(std::vector<ParamSig>(extra, ParamSig{receiver, false}), ret.value_or(receiver));
            };
        }

        std::expected<TypeId, std::string> array_of_result(TypeTable& ts, TypeId, const std::vector<TypeId>& args)
        {
            const TypeId ret = ts.function_return(args[0]);

            if (ret == bt::Void || ret == bt::None)
                return std::unexpected(std::string("the function must return a value"));

            return ts.array_of(ret);
        }

        template <class F>
        NativeResolver on_kind(TypeKind kind, F make)
        {
            return [kind, make](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != kind)
                    return std::nullopt;

                return make(ts, receiver);
            };
        }

        std::optional<TypeId> number_array_element(const TypeTable& ts, TypeId receiver)
        {
            if (ts.kind(receiver) != TypeKind::Array)
                return std::nullopt;

            const TypeId element = ts.element(receiver);

            if (element != bt::Int && element != bt::Float)
                return std::nullopt;

            return element;
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
    }


    void inject_std(TypeTable& types, NativeRegistry& natives, NativeTable& table)
    {
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

        const FunctionSig print_sig = NativeDef::variadic({ParamSig{bt::Any, false}}, bt::Void);

        def.function("println", print_sig, printer(true), {"println(any...) -> void"});
        def.function("print", print_sig, printer(false), {"print(any...) -> void"});



        def.static_function("new",
            on_kind(TypeKind::Map, [](TypeTable&, TypeId r) { return NativeDef::sig({}, r); }),
            [](Runtime&, Args, const auto&) { return make_value(Map{}); },
            {"(map).new() -> (map)", "", "@[string, int].new()"});

        def.method("remove",
            on_kind(TypeKind::Map, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({ParamSig{ts.key(r), false}}, bt::Bool, true);
            }),
            [](Runtime&, Args a, const auto&) { return make_bool(as_map(a[0]).items.erase(a[1]) > 0); },
            {"(map).remove(key) -> bool", "returns false if the key wasn't there"});



        const NativeResolver set_binary = on_kind(TypeKind::Set, [](TypeTable&, TypeId r)
        {
            return NativeDef::sig({ParamSig{r, false}}, r);
        });

        def.static_function("new",
            on_kind(TypeKind::Set, [](TypeTable&, TypeId r) { return NativeDef::sig({}, r); }),
            [](Runtime&, Args, const auto&) { return make_value(Set{}); },
            {"(set).new() -> (set)", "", "@[int].new()"});

        def.method("add",
            on_kind(TypeKind::Set, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({ParamSig{ts.element(r), false}}, bt::Bool, true);
            }),
            [](Runtime&, Args a, const auto&) { return make_bool(as_set(a[0]).items.insert(copy(a[1])).second); },
            {"(set).add(set.element) -> bool", "returns false if it was already present"});

        def.method("remove",
            on_kind(TypeKind::Set, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({ParamSig{ts.element(r), false}}, bt::Bool, true);
            }),
            [](Runtime&, Args a, const auto&) { return make_bool(as_set(a[0]).items.erase(a[1]) > 0); },
            {"(set).remove(set.element) -> bool", "returns false if it wasn't present"});

        def.method("contains",
            on_kind(TypeKind::Set, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({ParamSig{ts.element(r), false}}, bt::Bool);
            }),
            [](Runtime&, Args a, const auto&) { return make_bool(as_set(a[0]).items.contains(a[1])); },
            {"(set).contains(set.element) -> bool"});

        def.method("union", set_binary,
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

        def.method("intersect", set_binary,
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

        def.method("difference", set_binary,
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
            on_kind(TypeKind::Set, [](TypeTable&, TypeId r) { return NativeDef::sig({ParamSig{r, false}}, bt::Bool); }),
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
            on_kind(TypeKind::Set, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::variadic({ParamSig{ts.element(r), false}}, r);
            }),
            [](Runtime&, Args a, const auto&)
            {
                Set result;

                for (const auto& arg : a)
                    result.items.insert(copy(arg));

                return make_value(std::move(result));
            },
            {"(set).fromv(set.element...) -> (set)", "build a set from elements, duplicates collapse", "@[int].fromv(1, 2, 3)"});

        def.static_function("from",
            on_kind(TypeKind::Set, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({ParamSig{ts.array_of(ts.element(r)), false}}, r);
            }),
            [](Runtime&, Args a, const auto&)
            {
                Set result;

                for (const auto& x : as_array(a[0]).items)
                    result.items.insert(copy(x));

                return make_value(std::move(result));
            },
            {"(set).from(set.element[]) -> (set)", "build a set from an array, duplicates collapse", "@[int].from([1, 2, 3])"});


        def.method("to_array",
            on_kind(TypeKind::Range, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({}, ts.array_of(ts.element(r)));
            }),
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
            {"(range).to_array() -> int[]", "", "(0..3).to_array()   // [0, 1, 2]"});



        def.static_function("unbox",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{bt::Any, false}}, ts.optional_of(receiver));
            },
            [](Runtime& rt, Args a, const FunctionSig& sig) -> ValueRef
            {
                const TypeTable& ts = rt.types();
                const TypeId requested = ts.element(sig.ret);

                if (std::holds_alternative<std::monostate>(a[0]->v))
                    return make_void();

                const auto* boxed = std::get_if<Boxed>(&a[0]->v);
                if (!boxed || boxed->type != requested)
                    return make_void();

                return copy(boxed->v);
            },
            {"T.unbox(any) -> T?", "use to safely convert any to T"});

        def.method("or",
            on_kind(TypeKind::Optional, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({ParamSig{ts.element(r), false}}, ts.element(r));
            }),
            [](Runtime&, Args a, const auto&)
            {
                if (std::holds_alternative<std::monostate>(a[0]->v))
                    return copy(a[1]);

                return copy(a[0]);
            },
            {"(T?).or(T) -> T", "safe default value for T?", "maybe_int().or(-1)"});

        def.method("unwrap",
            on_kind(TypeKind::Optional, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({}, ts.element(r));
            }),
            [](Runtime&, Args a, const auto&)
            {
                if (std::holds_alternative<std::monostate>(a[0]->v))
                    throw std::runtime_error{"unwrap failed!"};

                return copy(a[0]);
            },
            {"(T?).unwrap() -> T", "force unwrap T?, (throws if none)"});

        def.method("to_string",
            [](TypeTable&, TypeId) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({}, bt::String);
            },
            [](Runtime& rt, Args a, const auto&) -> ValueRef
            {
                return make_value(to_string(*a[0], &rt.resolved()));
            },
            {"T.to_string() -> string", "convert any value to its string representation"});



        def.method("join",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Array)
                    return std::nullopt;

                if (ts.element(receiver) != bt::String)
                    return std::nullopt;

                return NativeDef::sig({ParamSig{bt::String, false}}, bt::String, true);
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
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) == TypeKind::Array)
                    return NativeDef::sig({ParamSig{bt::Int, false}}, ts.optional_of(ts.element(receiver)));

                if (ts.kind(receiver) == TypeKind::String)
                    return NativeDef::sig({ParamSig{bt::Int, false}}, ts.optional_of(bt::String));

                if (ts.kind(receiver) == TypeKind::Map)
                    return NativeDef::sig({ParamSig{ts.key(receiver), false}}, ts.optional_of(ts.value(receiver)));

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
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                switch (ts.kind(receiver))
                {
                    case TypeKind::Array:
                    case TypeKind::Map:
                    case TypeKind::String:
                    case TypeKind::Set:
                        return NativeDef::sig({}, bt::Int);

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
            {"(map | set | array | string).length() -> int"});

        def.method("push",
            on_kind(TypeKind::Array, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({ParamSig{ts.element(r), false}}, bt::Void, true);
            }),
            [](Runtime&, Args a, const auto&)
            {
                as_array(a[0]).items.push_back(copy(a[1]));
                return make_void();
            },
            {"(array).push(array.element) -> void"});

        def.method("pop",
            on_kind(TypeKind::Array, [](TypeTable& ts, TypeId r)
            {
                return NativeDef::sig({}, ts.optional_of(ts.element(r)), true);
            }),
            [](Runtime&, Args a, const auto&)
            {
                auto& items = as_array(a[0]).items;

                if (items.empty())
                    return make_void();

                ValueRef last = std::move(items.back());
                items.pop_back();
                return last;
            },
            {"(array).pop() -> T?", "removes and returns the last element, none when empty"});

        def.method("clear",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                const TypeKind kind = ts.kind(receiver);

                if (kind != TypeKind::Array && kind != TypeKind::Map && kind != TypeKind::Set)
                    return std::nullopt;

                return NativeDef::sig({}, bt::Void, true);
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
            {"(array | map | set).clear() -> void"});

        def.method("contains",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) == TypeKind::Map)
                    return NativeDef::sig({ParamSig{ts.key(receiver), false}}, bt::Bool);

                if (ts.kind(receiver) == TypeKind::Array)
                    return NativeDef::sig({ParamSig{ts.element(receiver), false}}, bt::Bool);

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
            on_kind(TypeKind::Array, [](TypeTable&, TypeId) { return NativeDef::sig({}, bt::Void, true); }),
            [](Runtime&, Args a, const auto&)
            {
                auto& items = as_array(a[0]).items;
                std::reverse(items.begin(), items.end());
                return make_void();
            },
            {"(T[]).reverse() -> void", "reverses in place"});

        def.method("reversed",
            on_kind(TypeKind::Array, [](TypeTable&, TypeId r) { return NativeDef::sig({}, r); }),
            [](Runtime&, Args a, const auto&)
            {
                Array result = as_array(copy(a[0]));
                std::reverse(result.items.begin(), result.items.end());
                return make_value(std::move(result));
            },
            {"(T[]).reversed() -> T[]", "returns a reversed copy"});



        def.function("debug::assert", NativeDef::sig({ParamSig{bt::Bool, false}}, bt::Void),
            [](Runtime&, Args a, const auto&)
            {
                if (as_bool(a[0]) == false)
                    throw std::runtime_error("debugging: assertion failed!");

                return make_void();
            },
            {"debug::assert(cond: bool)"});

        def.function("debug::assert_eq", NativeDef::sig({ParamSig{bt::Any, false}, ParamSig{bt::Any, false}}, bt::Void),
            [](Runtime&, Args a, const auto&)
            {
                if (*a[0] != *a[1])
                    throw std::runtime_error("debugging: assertion eq failed!");

                return make_void();
            },
            {"debug::assert_eq(any, any)"});



        def.generic_method("map",
            iter_with_fn(bt::Infer, [](TypeTable&, TypeId) { return bt::Infer; }),
            array_of_result,
            [](Runtime& rt, Args a, const auto&)
            {
                Array out;

                for (const ValueRef& x : items_of(a[0]))
                    out.items.push_back(invoke_fn(rt, a[1], {x}));

                return make_value(std::move(out));
            },
            {"(T[] | range | set).map(fn(T) -> U) -> U[]", "a new array with the function applied to every element",
             "[1, 2, 3].map(|x| x * 2)   // [2, 4, 6]"});

        def.method("filter",
            iter_with_fn(bt::Bool, [](TypeTable& ts, TypeId e) { return ts.array_of(e); }),
            [](Runtime& rt, Args a, const auto&)
            {
                Array out;

                for (const ValueRef& x : items_of(a[0]))
                    if (test(rt, a[1], x))
                        out.items.push_back(copy(x));

                return make_value(std::move(out));
            },
            {"(T[] | range | set).filter(fn(T) -> bool) -> T[]", "the elements the function says yes to",
             "(1..=6).filter(|i| i % 2 == 0)   // [2, 4, 6]"});

        def.method("for_each",
            iter_with_fn(bt::Infer, [](TypeTable&, TypeId) { return bt::Void; }),
            [](Runtime& rt, Args a, const auto&)
            {
                for (const ValueRef& x : items_of(a[0]))
                    invoke_fn(rt, a[1], {x});

                return make_void();
            },
            {"(T[] | range | set).for_each(fn(T)) -> void",
             "runs the function for every element. a lambda only changes outside variables when it is 'ref'",
             "var sum = 0\nnums.for_each(ref |x| sum = sum + x)"});

        def.method("any",
            iter_with_fn(bt::Bool, [](TypeTable&, TypeId) { return bt::Bool; }),
            [](Runtime& rt, Args a, const auto&)
            {
                for (const ValueRef& x : items_of(a[0]))
                    if (test(rt, a[1], x))
                        return make_bool(true);

                return make_bool(false);
            },
            {"(T[] | range | set).any(fn(T) -> bool) -> bool"});

        def.method("all",
            iter_with_fn(bt::Bool, [](TypeTable&, TypeId) { return bt::Bool; }),
            [](Runtime& rt, Args a, const auto&)
            {
                for (const ValueRef& x : items_of(a[0]))
                    if (!test(rt, a[1], x))
                        return make_bool(false);

                return make_bool(true);
            },
            {"(T[] | range | set).all(fn(T) -> bool) -> bool"});

        def.method("count",
            iter_with_fn(bt::Bool, [](TypeTable&, TypeId) { return bt::Int; }),
            [](Runtime& rt, Args a, const auto&)
            {
                int64_t count = 0;

                for (const ValueRef& x : items_of(a[0]))
                    if (test(rt, a[1], x))
                        count++;

                return make_int(count);
            },
            {"(T[] | range | set).count(fn(T) -> bool) -> int"});

        def.method("find",
            iter_with_fn(bt::Bool, [](TypeTable& ts, TypeId e) { return ts.optional_of(e); }, true),
            [](Runtime& rt, Args a, const auto&)
            {
                for (const ValueRef& x : items_of(a[0]))
                    if (test(rt, a[1], x))
                        return copy(x);

                return make_void();
            },
            {"(T[] | range | set).find(fn(T) -> bool) -> T?", "the first element the function says yes to",
             "users.find(|u| u.id == 3).or(guest)"});

        def.method("find_index",
            iter_with_fn(bt::Bool, [](TypeTable& ts, TypeId) { return ts.optional_of(bt::Int); }),
            [](Runtime& rt, Args a, const auto&)
            {
                int64_t i = 0;

                for (const ValueRef& x : items_of(a[0]))
                {
                    if (test(rt, a[1], x))
                        return make_int(i);

                    i++;
                }

                return make_void();
            },
            {"(T[] | range | set).find_index(fn(T) -> bool) -> int?"});


        def.method("fold",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                const auto e = iter_element(ts, receiver);

                if (!e)
                    return std::nullopt;

                return NativeDef::sig({ParamSig{*e, false}, ParamSig{ts.function_of({*e, *e}, *e), false}}, *e);
            },
            [](Runtime& rt, Args a, const auto&)
            {
                ValueRef acc = copy(a[1]);

                for (const ValueRef& x : items_of(a[0]))
                    acc = invoke_fn(rt, a[2], {acc, x});

                return acc;
            },
            {"(T[] | range | set).fold(start: T, fn(T, T) -> T) -> T", "combines all elements into one value",
             "nums.fold(0, |acc, x| acc + x)"});

        def.method("enumerate",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                const auto e = iter_element(ts, receiver);

                if (!e)
                    return std::nullopt;

                return NativeDef::sig({}, ts.array_of(ts.tuple_of({bt::Int, *e})));
            },
            [](Runtime&, Args a, const auto&)
            {
                Array out;
                int64_t i = 0;

                for (const ValueRef& x : items_of(a[0]))
                {
                    Tuple pair;
                    pair.items.push_back(make_int(i++));
                    pair.items.push_back(copy(x));

                    out.items.push_back(make_value(std::move(pair)));
                }

                return make_value(std::move(out));
            },
            {"(T[] | range | set).enumerate() -> (int, T)[]", "pairs of (index, element)",
             "for p in names.enumerate()\n{\n    var (i, name) = p\n}"});

        def.method("to_array",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Set)
                    return std::nullopt;

                return NativeDef::sig({}, ts.array_of(ts.element(receiver)));
            },
            [](Runtime&, Args a, const auto&)
            {
                Array out;

                for (const ValueRef& x : items_of(a[0]))
                    out.items.push_back(copy(x));

                return make_value(std::move(out));
            },
            {"(set).to_array() -> T[]"});



        def.method("first",
            on_kind(TypeKind::Array, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                if (ts.kind(ts.element(r)) == TypeKind::Optional)
                    return std::nullopt;

                return NativeDef::sig({}, ts.optional_of(ts.element(r)));
            }),
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;
                return items.empty() ? make_void() : copy(items.front());
            },
            {"(T[]).first() -> T?"});

        def.method("last",
            on_kind(TypeKind::Array, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                if (ts.kind(ts.element(r)) == TypeKind::Optional)
                    return std::nullopt;

                return NativeDef::sig({}, ts.optional_of(ts.element(r)));
            }),
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;
                return items.empty() ? make_void() : copy(items.back());
            },
            {"(T[]).last() -> T?"});

        def.method("is_empty",
            [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                switch (ts.kind(r))
                {
                    case TypeKind::Array:
                    case TypeKind::Map:
                    case TypeKind::Set:
                    case TypeKind::String:
                        return NativeDef::sig({}, bt::Bool);

                    default:
                        return std::nullopt;
                }
            },
            [](Runtime&, Args a, const auto&)
            {
                const Value& v = *a[0];

                if (const auto* array = std::get_if<Array>(&v.v))
                    return make_bool(array->items.empty());

                if (const auto* map = std::get_if<Map>(&v.v))
                    return make_bool(map->items.empty());

                if (const auto* set = std::get_if<Set>(&v.v))
                    return make_bool(set->items.empty());

                return make_bool(as_string(a[0]).empty());
            },
            {"(array | map | set | string).is_empty() -> bool"});

        def.method("index_of",
            on_kind(TypeKind::Array, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{ts.element(r), false}}, ts.optional_of(bt::Int));
            }),
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;

                for (size_t i = 0; i < items.size(); i++)
                    if (*items[i] == *a[1])
                        return make_int(static_cast<int64_t>(i));

                return make_void();
            },
            {"(T[]).index_of(T) -> int?"});

        def.method("slice",
            on_kind(TypeKind::Array, [](TypeTable&, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{bt::Int, false}, ParamSig{bt::Int, false}}, r);
            }),
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;
                const auto [from, to] = clamp_range(as_int(a[1]), as_int(a[2]), items.size());

                Array out;
                for (size_t i = from; i < to; i++)
                    out.items.push_back(copy(items[i]));

                return make_value(std::move(out));
            },
            {"(T[]).slice(from: int, to: int) -> T[]", "elements from..to, out of range is cut off"});

        def.method("take",
            on_kind(TypeKind::Array, [](TypeTable&, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{bt::Int, false}}, r);
            }),
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;
                const auto [from, to] = clamp_range(0, as_int(a[1]), items.size());

                Array out;
                for (size_t i = from; i < to; i++)
                    out.items.push_back(copy(items[i]));

                return make_value(std::move(out));
            },
            {"(T[]).take(n: int) -> T[]", "the first n elements"});

        def.method("skip",
            on_kind(TypeKind::Array, [](TypeTable&, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{bt::Int, false}}, r);
            }),
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;
                const auto [from, to] = clamp_range(as_int(a[1]), static_cast<int64_t>(items.size()), items.size());

                Array out;
                for (size_t i = from; i < to; i++)
                    out.items.push_back(copy(items[i]));

                return make_value(std::move(out));
            },
            {"(T[]).skip(n: int) -> T[]", "everything after the first n elements"});

        def.method("unique",
            on_kind(TypeKind::Array, [](TypeTable&, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({}, r);
            }),
            [](Runtime&, Args a, const auto&)
            {
                std::set<ValueRef, ValueRefCmp> seen;
                Array out;

                for (const ValueRef& x : as_array(a[0]).items)
                    if (seen.insert(x).second)
                        out.items.push_back(copy(x));

                return make_value(std::move(out));
            },
            {"(T[]).unique() -> T[]", "removes duplicates, keeps the first of each"});

        def.method("sort",
            on_kind(TypeKind::Array, [](TypeTable&, TypeId) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({}, bt::Void, true);
            }),
            [](Runtime&, Args a, const auto&)
            {
                auto& items = as_array(a[0]).items;

                std::stable_sort(items.begin(), items.end(),
                    [](const ValueRef& x, const ValueRef& y) { return *x < *y; });

                return make_void();
            },
            {"(T[]).sort() -> void", "sorts in place, numbers by value, strings alphabetically"});

        def.method("sorted",
            on_kind(TypeKind::Array, [](TypeTable&, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({}, r);
            }),
            [](Runtime&, Args a, const auto&)
            {
                Array out;

                for (const ValueRef& x : as_array(a[0]).items)
                    out.items.push_back(copy(x));

                std::stable_sort(out.items.begin(), out.items.end(),
                    [](const ValueRef& x, const ValueRef& y) { return *x < *y; });

                return make_value(std::move(out));
            },
            {"(T[]).sorted() -> T[]", "a sorted copy"});

        def.method("sort_by",
            on_kind(TypeKind::Array, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{ts.function_of({ts.element(r)}, bt::Infer), false}}, bt::Void, true);
            }),
            [](Runtime& rt, Args a, const auto&)
            {
                auto& items = as_array(a[0]).items;
                items = ordered_by(rt, std::vector<ValueRef>(items), a[1]);

                return make_void();
            },
            {"(T[]).sort_by(fn(T) -> K) -> void", "sorts in place by the key the function gives",
             "people.sort_by(|p| p.age)"});

        def.method("sorted_by",
            on_kind(TypeKind::Array, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{ts.function_of({ts.element(r)}, bt::Infer), false}}, r);
            }),
            [](Runtime& rt, Args a, const auto&)
            {
                Array out;

                for (const ValueRef& x : ordered_by(rt, as_array(a[0]).items, a[1]))
                    out.items.push_back(copy(x));

                return make_value(std::move(out));
            },
            {"(T[]).sorted_by(fn(T) -> K) -> T[]", "a copy sorted by the key the function gives"});

        def.method("sum",
            [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                const auto e = number_array_element(ts, r);

                if (!e)
                    return std::nullopt;

                return NativeDef::sig({}, *e);
            },
            [](Runtime&, Args a, const FunctionSig& sig)
            {
                const auto& items = as_array(a[0]).items;

                if (sig.ret == bt::Float)
                {
                    double total = 0;

                    for (const ValueRef& x : items)
                        total += as_float(x);

                    return make_float(total);
                }

                uint64_t total = 0;

                for (const ValueRef& x : items)
                    total += static_cast<uint64_t>(as_int(x));

                return make_int(static_cast<int64_t>(total));
            },
            {"(int[] | float[]).sum() -> int | float"});

        auto number_array_extreme = [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
        {
            const auto e = number_array_element(ts, r);

            if (!e)
                return std::nullopt;

            return NativeDef::sig({}, ts.optional_of(*e));
        };

        def.method("min", number_array_extreme,
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;

                if (items.empty())
                    return make_void();

                ValueRef best = items[0];

                for (const ValueRef& x : items)
                    if (*x < *best)
                        best = x;

                return copy(best);
            },
            {"(int[] | float[]).min() -> T?", "none when the array is empty"});

        def.method("max", number_array_extreme,
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_array(a[0]).items;

                if (items.empty())
                    return make_void();

                ValueRef best = items[0];

                for (const ValueRef& x : items)
                    if (*x > *best)
                        best = x;

                return copy(best);
            },
            {"(int[] | float[]).max() -> T?", "none when the array is empty"});



        auto optional_sig = [](TypeTable& ts, TypeId r, auto make) -> std::optional<FunctionSig>
        {
            if (ts.kind(r) != TypeKind::Optional)
                return std::nullopt;

            return make(ts, ts.element(r));
        };

        def.method("is_some",
            [=](TypeTable& ts, TypeId r) { return optional_sig(ts, r, [](TypeTable&, TypeId) { return NativeDef::sig({}, bt::Bool); }); },
            [](Runtime&, Args a, const auto&) { return make_bool(!is_none(a[0])); },
            {"(T?).is_some() -> bool"});

        def.method("is_none",
            [=](TypeTable& ts, TypeId r) { return optional_sig(ts, r, [](TypeTable&, TypeId) { return NativeDef::sig({}, bt::Bool); }); },
            [](Runtime&, Args a, const auto&) { return make_bool(is_none(a[0])); },
            {"(T?).is_none() -> bool"});

        def.generic_method("map",
            [=](TypeTable& ts, TypeId r)
            {
                return optional_sig(ts, r, [](TypeTable& inner, TypeId e)
                {
                    return NativeDef::sig({ParamSig{inner.function_of({e}, bt::Infer), false}}, bt::Infer);
                });
            },
            [](TypeTable& ts, TypeId, const std::vector<TypeId>& args) -> std::expected<TypeId, std::string>
            {
                const TypeId ret = ts.function_return(args[0]);

                if (ret == bt::Void || ret == bt::None)
                    return std::unexpected(std::string("the function must return a value"));

                if (ts.kind(ret) == TypeKind::Optional)
                    return std::unexpected(std::string("the function returns an optional, and an optional of an optional does not exist"));

                return ts.optional_of(ret);
            },
            [](Runtime& rt, Args a, const auto&)
            {
                if (is_none(a[0]))
                    return make_void();

                return invoke_fn(rt, a[1], {a[0]});
            },
            {"(T?).map(fn(T) -> U) -> U?", "applies the function when there is a value, none stays none",
             "find_user(3).map(|u| u.name).or(\"nobody\")"});

        def.method("or_else",
            [=](TypeTable& ts, TypeId r)
            {
                return optional_sig(ts, r, [](TypeTable& inner, TypeId e)
                {
                    return NativeDef::sig({ParamSig{inner.function_of({}, e), false}}, e);
                });
            },
            [](Runtime& rt, Args a, const auto&)
            {
                if (is_none(a[0]))
                    return invoke_fn(rt, a[1], {});

                return copy(a[0]);
            },
            {"(T?).or_else(fn() -> T) -> T", "like or(), but the default is only computed when needed",
             "cache.at(key).or_else(|| load(key))"});


        def.method("keys",
            on_kind(TypeKind::Map, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({}, ts.array_of(ts.key(r)));
            }),
            [](Runtime&, Args a, const auto&)
            {
                Array out;

                for (const auto& entry : as_map(a[0]).items)
                    out.items.push_back(copy(entry.first));

                return make_value(std::move(out));
            },
            {"(map).keys() -> K[]"});

        def.method("values",
            on_kind(TypeKind::Map, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({}, ts.array_of(ts.value(r)));
            }),
            [](Runtime&, Args a, const auto&)
            {
                Array out;

                for (const auto& entry : as_map(a[0]).items)
                    out.items.push_back(copy(entry.second));

                return make_value(std::move(out));
            },
            {"(map).values() -> V[]"});

        def.method("entries",
            on_kind(TypeKind::Map, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({}, ts.array_of(ts.tuple_of({ts.key(r), ts.value(r)})));
            }),
            [](Runtime&, Args a, const auto&)
            {
                Array out;

                for (const auto& entry : as_map(a[0]).items)
                {
                    Tuple pair;
                    pair.items.push_back(copy(entry.first));
                    pair.items.push_back(copy(entry.second));

                    out.items.push_back(make_value(std::move(pair)));
                }

                return make_value(std::move(out));
            },
            {"(map).entries() -> (K, V)[]", "pairs of (key, value)",
             "for e in ages.entries()\n{\n    var (name, age) = e\n}"});

        def.method("get_or",
            on_kind(TypeKind::Map, [](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                return NativeDef::sig({ParamSig{ts.key(r), false}, ParamSig{ts.value(r), false}}, ts.value(r));
            }),
            [](Runtime&, Args a, const auto&)
            {
                const auto& items = as_map(a[0]).items;
                const auto it = items.find(a[1]);

                return copy(it == items.end() ? a[2] : it->second);
            },
            {"(map).get_or(key: K, default: V) -> V"});



        const auto is_string = [](TypeTable& ts, TypeId r) { return ts.kind(r) == TypeKind::String; };

        auto string_method = [=](std::vector<TypeId> params, TypeId ret)
        {
            return [=](TypeTable& ts, TypeId r) -> std::optional<FunctionSig>
            {
                if (!is_string(ts, r))
                    return std::nullopt;

                std::vector<ParamSig> sig_params;
                for (TypeId p : params)
                    sig_params.push_back({p, false});

                return NativeDef::sig(std::move(sig_params), ret);
            };
        };

        def.method("trim", string_method({}, bt::String),
            [](Runtime&, Args a, const auto&) { return make_string(std::string(trim(as_string(a[0])))); },
            {"(string).trim() -> string", "removes spaces and newlines on both sides"});

        def.method("trim_start", string_method({}, bt::String),
            [](Runtime&, Args a, const auto&) { return make_string(std::string(left_trim(as_string(a[0])))); },
            {"(string).trim_start() -> string"});

        def.method("trim_end", string_method({}, bt::String),
            [](Runtime&, Args a, const auto&) { return make_string(std::string(right_trim(as_string(a[0])))); },
            {"(string).trim_end() -> string"});

        def.method("upper", string_method({}, bt::String),
            [](Runtime&, Args a, const auto&)
            {
                std::string s = as_string(a[0]);

                for (char& c : s)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

                return make_string(std::move(s));
            },
            {"(string).upper() -> string", "ASCII only"});

        def.method("lower", string_method({}, bt::String),
            [](Runtime&, Args a, const auto&)
            {
                std::string s = as_string(a[0]);

                for (char& c : s)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                return make_string(std::move(s));
            },
            {"(string).lower() -> string", "ASCII only"});

        def.method("starts_with", string_method({bt::String}, bt::Bool),
            [](Runtime&, Args a, const auto&) { return make_bool(as_string(a[0]).starts_with(as_string(a[1]))); },
            {"(string).starts_with(prefix: string) -> bool"});

        def.method("ends_with", string_method({bt::String}, bt::Bool),
            [](Runtime&, Args a, const auto&) { return make_bool(as_string(a[0]).ends_with(as_string(a[1]))); },
            {"(string).ends_with(suffix: string) -> bool"});

        def.method("contains", string_method({bt::String}, bt::Bool),
            [](Runtime&, Args a, const auto&) { return make_bool(as_string(a[0]).find(as_string(a[1])) != std::string::npos); },
            {"(string).contains(part: string) -> bool"});

        def.method("index_of", string_method({bt::String}, types.optional_of(bt::Int)),
            [](Runtime&, Args a, const auto&)
            {
                const size_t at = as_string(a[0]).find(as_string(a[1]));

                if (at == std::string::npos)
                    return make_void();

                return make_int(static_cast<int64_t>(at));
            },
            {"(string).index_of(part: string) -> int?"});

        def.method("slice", string_method({bt::Int, bt::Int}, bt::String),
            [](Runtime&, Args a, const auto&)
            {
                const std::string& s = as_string(a[0]);
                const auto [from, to] = clamp_range(as_int(a[1]), as_int(a[2]), s.size());

                return make_string(s.substr(from, to - from));
            },
            {"(string).slice(from: int, to: int) -> string", "characters from..to, out of range is cut off"});

        def.method("split", string_method({bt::String}, types.array_of(bt::String)),
            [](Runtime&, Args a, const auto&)
            {
                const std::string& s = as_string(a[0]);
                const std::string& separator = as_string(a[1]);

                Array out;

                if (separator.empty())
                {
                    for (char c : s)
                        out.items.push_back(make_string(std::string(1, c)));

                    return make_value(std::move(out));
                }

                size_t start = 0;

                while (true)
                {
                    const size_t at = s.find(separator, start);

                    if (at == std::string::npos)
                    {
                        out.items.push_back(make_string(s.substr(start)));
                        break;
                    }

                    out.items.push_back(make_string(s.substr(start, at - start)));
                    start = at + separator.size();
                }

                return make_value(std::move(out));
            },
            {"(string).split(separator: string) -> string[]", "an empty separator gives every character",
             "\"a,b,c\".split(\",\")   // [\"a\", \"b\", \"c\"]"});

        def.method("lines", string_method({}, types.array_of(bt::String)),
            [](Runtime&, Args a, const auto&)
            {
                const std::string& s = as_string(a[0]);

                Array out;
                size_t start = 0;

                while (start < s.size())
                {
                    size_t end = s.find('\n', start);

                    if (end == std::string::npos)
                        end = s.size();

                    std::string line = s.substr(start, end - start);

                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();

                    out.items.push_back(make_string(std::move(line)));
                    start = end + 1;
                }

                return make_value(std::move(out));
            },
            {"(string).lines() -> string[]", "splits on newlines, \\r\\n too"});

        def.method("chars", string_method({}, types.array_of(bt::String)),
            [](Runtime&, Args a, const auto&)
            {
                Array out;

                for (char c : as_string(a[0]))
                    out.items.push_back(make_string(std::string(1, c)));

                return make_value(std::move(out));
            },
            {"(string).chars() -> string[]"});

        def.method("replace", string_method({bt::String, bt::String}, bt::String),
            [](Runtime&, Args a, const auto&)
            {
                const std::string& s = as_string(a[0]);
                const std::string& from = as_string(a[1]);
                const std::string& to = as_string(a[2]);

                if (from.empty())
                    return make_string(s);

                std::string out;
                size_t start = 0;

                while (true)
                {
                    const size_t at = s.find(from, start);

                    if (at == std::string::npos)
                    {
                        out += s.substr(start);
                        break;
                    }

                    out += s.substr(start, at - start);
                    out += to;
                    start = at + from.size();
                }

                return make_string(std::move(out));
            },
            {"(string).replace(from: string, to: string) -> string", "replaces every occurrence"});

        def.method("repeat", string_method({bt::Int}, bt::String),
            [](Runtime&, Args a, const auto&)
            {
                const int64_t count = as_int(a[1]);
                const std::string& s = as_string(a[0]);

                if (count > 0 && s.size() * static_cast<uint64_t>(count) > 100'000'000)
                    throw std::runtime_error("repeat: the result would be too large");

                std::string out;

                for (int64_t i = 0; i < count; i++)
                    out += s;

                return make_string(std::move(out));
            },
            {"(string).repeat(n: int) -> string"});

        def.method("to_int", string_method({}, types.optional_of(bt::Int)),
            [](Runtime&, Args a, const auto&)
            {
                const std::string& s = as_string(a[0]);
                int64_t value = 0;

                const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);

                if (s.empty() || error != std::errc{} || end != s.data() + s.size())
                    return make_void();

                return make_int(value);
            },
            {"(string).to_int() -> int?", "none when it is not a whole number", "\"42\".to_int().or(0)"});

        def.method("to_float", string_method({}, types.optional_of(bt::Float)),
            [](Runtime&, Args a, const auto&)
            {
                const std::string& s = as_string(a[0]);

                if (s.empty() || s.find_first_not_of("0123456789+-.eE") != std::string::npos)
                    return make_void();

                errno = 0;
                char* end = nullptr;
                const double value = std::strtod(s.c_str(), &end);

                if (end != s.c_str() + s.size() || errno == ERANGE)
                    return make_void();

                return make_float(value);
            },
            {"(string).to_float() -> float?", "none when it is not a number"});



        const std::vector<TypeId> numbers = {bt::Int, bt::Float};

        def.method("abs", self_method(numbers),
            [](Runtime&, Args a, const auto&) -> ValueRef
            {
                if (is_int_value(a[0]))
                {
                    const int64_t v = as_int(a[0]);
                    return make_int(v < 0 ? static_cast<int64_t>(0 - static_cast<uint64_t>(v)) : v);
                }

                return make_float(std::fabs(as_float(a[0])));
            },
            {"(int | float).abs() -> int | float", "", "(-5).abs()   // careful: -5.abs() is -(5.abs())"});

        def.method("sign", self_method(numbers),
            [](Runtime&, Args a, const auto&) -> ValueRef
            {
                if (is_int_value(a[0]))
                {
                    const int64_t v = as_int(a[0]);
                    return make_int(v < 0 ? -1 : (v > 0 ? 1 : 0));
                }

                const double v = as_float(a[0]);
                return make_float(v < 0 ? -1.0 : (v > 0 ? 1.0 : 0.0));
            },
            {"(int | float).sign() -> int | float", "-1, 0 or 1"});

        def.method("min", self_method(numbers, 1),
            [](Runtime&, Args a, const auto&) -> ValueRef
            {
                if (is_int_value(a[0]))
                    return make_int(std::min(as_int(a[0]), as_int(a[1])));

                return make_float(std::fmin(as_float(a[0]), as_float(a[1])));
            },
            {"(int | float).min(other) -> int | float", "the smaller of the two"});

        def.method("max", self_method(numbers, 1),
            [](Runtime&, Args a, const auto&) -> ValueRef
            {
                if (is_int_value(a[0]))
                    return make_int(std::max(as_int(a[0]), as_int(a[1])));

                return make_float(std::fmax(as_float(a[0]), as_float(a[1])));
            },
            {"(int | float).max(other) -> int | float", "the bigger of the two"});

        def.method("clamp", self_method(numbers, 2),
            [](Runtime&, Args a, const auto&) -> ValueRef
            {
                if (is_int_value(a[0]))
                {
                    const int64_t lo = as_int(a[1]);
                    const int64_t hi = as_int(a[2]);

                    if (lo > hi)
                        throw std::runtime_error("clamp: lo is greater than hi");

                    return make_int(std::clamp(as_int(a[0]), lo, hi));
                }

                const double lo = as_float(a[1]);
                const double hi = as_float(a[2]);

                if (lo > hi)
                    throw std::runtime_error("clamp: lo is greater than hi");

                return make_float(std::clamp(as_float(a[0]), lo, hi));
            },
            {"(int | float).clamp(lo, hi) -> int | float", "", "health.clamp(0, 100)"});

        def.method("pow", self_method(numbers, 1),
            [](Runtime&, Args a, const auto&) -> ValueRef
            {
                if (!is_int_value(a[0]))
                    return make_float(std::pow(as_float(a[0]), as_float(a[1])));

                const int64_t exponent = as_int(a[1]);

                if (exponent < 0)
                    throw std::runtime_error("pow: negative exponent");

                uint64_t result = 1;
                uint64_t base = static_cast<uint64_t>(as_int(a[0]));

                for (int64_t e = exponent; e > 0; e >>= 1)
                {
                    if (e & 1)
                        result *= base;

                    base *= base;
                }

                return make_int(static_cast<int64_t>(result));
            },
            {"(int | float).pow(exponent) -> int | float", "int pow wraps around on overflow"});

        def.method("to_float", self_method({bt::Int}, 0, bt::Float),
            [](Runtime&, Args a, const auto&) { return make_float(static_cast<double>(as_int(a[0]))); },
            {"(int).to_float() -> float"});

        def.method("to_int", self_method({bt::Float}, 0, bt::Int),
            [](Runtime&, Args a, const auto&)
            {
                const double v = as_float(a[0]);

                if (!std::isfinite(v) || v >= 9223372036854775808.0 || v < -9223372036854775808.0)
                    throw std::runtime_error("to_int: the float does not fit in an int");

                return make_int(static_cast<int64_t>(v));
            },
            {"(float).to_int() -> int", "cuts off the decimals (3.9 becomes 3, -3.9 becomes -3)"});

        def.method("is_nan", self_method({bt::Float}, 0, bt::Bool),
            [](Runtime&, Args a, const auto&) { return make_bool(std::isnan(as_float(a[0]))); },
            {"(float).is_nan() -> bool"});

        def.method("lerp", self_method({bt::Float}, 2),
            [](Runtime&, Args a, const auto&)
            {
                const double from = as_float(a[0]);
                return make_float(from + (as_float(a[1]) - from) * as_float(a[2]));
            },
            {"(float).lerp(to: float, t: float) -> float", "from + (to - from) * t", "0.0.lerp(10.0, 0.25)   // 2.5"});

        struct FloatFn
        {
            const char* name;
            double (*fn)(double);
        };

        const FloatFn float_fns[] = {
            {"floor", std::floor}, {"ceil", std::ceil}, {"round", std::round}, {"trunc", std::trunc},
            {"sqrt", std::sqrt}, {"sin", std::sin}, {"cos", std::cos}, {"tan", std::tan},
            {"exp", std::exp}, {"ln", std::log}, {"log10", std::log10},
        };

        for (const FloatFn& f : float_fns)
        {
            def.method(f.name, self_method({bt::Float}),
                [fn = f.fn](Runtime&, Args a, const auto&) { return make_float(fn(as_float(a[0]))); },
                {std::string("(float).") + f.name + "() -> float"});
        }



        def.function("math::pi", NativeDef::sig({}, bt::Float),
            [](Runtime&, Args, const auto&) { return make_float(std::numbers::pi); },
            {"math::pi() -> float"});

        def.function("math::e", NativeDef::sig({}, bt::Float),
            [](Runtime&, Args, const auto&) { return make_float(std::numbers::e); },
            {"math::e() -> float"});

        def.function("math::atan2", NativeDef::sig({ParamSig{bt::Float, false}, ParamSig{bt::Float, false}}, bt::Float),
            [](Runtime&, Args a, const auto&) { return make_float(std::atan2(as_float(a[0]), as_float(a[1]))); },
            {"math::atan2(y: float, x: float) -> float"});
    }
}