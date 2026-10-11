#include <foo/dynamic_runner.hpp>

#include <foo/util/overloaded.hpp>

#include <functional>

namespace foo
{
    using namespace ast;

    struct Value;
    using Array = std::vector<Value>;

    struct Value
    {
        std::variant<std::monostate, std::int64_t, double, bool, std::string, Array> data;

        Value() = default;

        Value(std::int64_t v) : data(v) {}
        Value(double v) : data(v) {}
        Value(bool v) : data(v) {}
        Value(std::string v) : data(std::move(v)) {}
        Value(Array v) : data(std::move(v)) {}
    };

    static std::string display(const Value& v)
    {
        return std::visit(overload{
            [](std::monostate) { return std::string("null"); },
            [](std::int64_t n) { return std::to_string(n); },
            [](double d) { return std::format("{}", d); },
            [](bool b) { return std::string(b ? "true" : "false"); },
            [](const std::string& s) { return "\"" + s + "\""; },
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
            return std::nullopt;
        }

    private:
        std::unordered_map<std::string, const FnDecl*> _user_functions;
    };


    std::optional<RuntimeError> run(std::span<const ast::DeclPtr> decls)
    {
        Interpreter i{decls};
        return i.run();
    }
}