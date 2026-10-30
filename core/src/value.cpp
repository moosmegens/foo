#include <foo/value.hpp>

#include <foo/util/overloaded.hpp>

#include <algorithm>
#include <cmath>
#include <compare>
#include <format>
#include <variant>

namespace foo
{
    namespace
    {
        using Ord = std::strong_ordering;
        using Storage = Value::Storage;


        Ord cmp_ref(const ValueRef& a, const ValueRef& b)
        {
            return *a <=> *b;
        }


        Ord cmp(std::monostate, std::monostate)
        {
            return Ord::equal;
        }

        Ord cmp(bool a, bool b)
        {
            return a <=> b;
        }

        Ord cmp(int64_t a, int64_t b)
        {
            return a <=> b;
        }

        Ord cmp(double a, double b)
        {
            if (a < b)
                return Ord::less;

            if (a > b)
                return Ord::greater;

            return Ord::equal;
        }

        Ord cmp(const std::string& a, const std::string& b)
        {
            return a <=> b;
        }

        Ord cmp(const Range& a, const Range& b)
        {
            if (auto c = cmp_ref(a.lo, b.lo); c != 0)
                return c;

            if (auto c = cmp_ref(a.hi, b.hi); c != 0)
                return c;

            return a.inclusive <=> b.inclusive;
        }

        Ord cmp(const Array& a, const Array& b)
        {
            return std::lexicographical_compare_three_way(
                a.items.begin(), a.items.end(),
                b.items.begin(), b.items.end(), cmp_ref);
        }

        Ord cmp(const Map& a, const Map& b)
        {
            return std::lexicographical_compare_three_way(
                a.items.begin(), a.items.end(),
                b.items.begin(), b.items.end(),
                [](const auto& x, const auto& y) -> Ord
                {
                    if (auto c = cmp_ref(x.first, y.first); c != 0)
                        return c;

                    return cmp_ref(x.second, y.second);
                });
        }

        Ord cmp(const Struct& a, const Struct& b)
        {
            return std::lexicographical_compare_three_way(
                a.fields.begin(), a.fields.end(),
                b.fields.begin(), b.fields.end(),
                cmp_ref);
        }

        Ord cmp(const Rc<Native>& a, const Rc<Native>& b)
        {
            return a->compare(*b);
        }

        Ord cmp(const Boxed& a, const Boxed& b)
        {
            return cmp_ref(a.v, b.v);
        }


        ValueRef clone_value(const ValueRef& r)
        {
            return make<Value>(r->clone());
        }

        template<class T>
        Storage clone_of(const T& x) { return x; }

        Storage clone_of(const Boxed& x)
        {
            return Boxed(clone_value(x.v));
        }

        Storage clone_of(const Range& x)
        {
            return Range(clone_value(x.lo), clone_value(x.hi), x.inclusive);
        }

        Storage clone_of(const Array& x)
        {
            Array a;

            a.items.reserve(x.items.size());
            for (const ValueRef& e : x.items)
                a.items.push_back(clone_value(e));

            return a;
        }

        Storage clone_of(const Map& x)
        {
            Map m;

            for (const auto& [k, e] : x.items)
                m.items.emplace(clone_value(k), clone_value(e));

            return m;
        }

        Storage clone_of(const Struct& x)
        {
            Struct s;

            s.fields.reserve(x.fields.size());
            for (const ValueRef& f : x.fields)
                s.fields.push_back(clone_value(f));

            return s;
        }

        void show(std::string& o, const Value& v, bool nested)
        {
            std::visit(overload{
                [&](std::monostate)         { o += "void"; },
                [&](bool b)                 { o += b ? "true" : "false"; },
                [&](int64_t i)              { o += std::format("{}", i); },
                [&](double d)               { o += std::format("{}", d); },
                [&](const std::string& s)
                {
                    if (nested) { o += '"'; o += s; o += '"'; }
                    else o += s;
                },
                [&](const Range& r)
                {
                    show(o, *r.lo, true);
                    o += r.inclusive ? "..=" : "..";
                    show(o, *r.hi, true);
                },
                [&](const Array& a)
                {
                    o += '[';
                    for (size_t i = 0; i < a.items.size(); ++i)
                    {
                        if (i) o += ", ";
                        show(o, *a.items[i], true);
                    }
                    o += ']';
                },
                [&](const Map& m)
                {
                    o += '{';
                    bool first = true;
                    for (const auto& [k, e] : m.items)
                    {
                        if (!first) o += ", ";
                        first = false;
                        show(o, *k, true);
                        o += ": ";
                        show(o, *e, true);
                    }
                    o += '}';
                },
                [&](const Struct& s)
                {
                    o += '{';
                    for (size_t i = 0; i < s.fields.size(); ++i)
                    {
                        if (i)
                            o += ", ";

                        show(o, *s.fields[i], true);
                    }
                    o += '}';
                },
                [&](const Rc<Native>& n)    { o += "<native>"; },
                [&](const Boxed& b)         { show(o, *b.v, nested); }
            }, v.v);
        }

    }

    Value Value::clone() const
    {
        return std::visit([](const auto& x)
        {
            return Value(clone_of(x));
        }, v);
    }

    std::strong_ordering operator<=>(const Value& a, const Value& b)
    {
        if (a.v.index() != b.v.index())
            return a.v.index() <=> b.v.index();

        return std::visit([&]<typename T>(const T& x) -> Ord
        {
            return cmp(x, std::get<std::decay_t<T>>(b.v));
        }, a.v);
    }

    bool operator==(const Value& a, const Value& b)
    {
        return (a <=> b) == 0;
    }

    std::string to_string(const Value& v)
    {
        std::string out;
        show(out, v, false);
        return out;
    }

}