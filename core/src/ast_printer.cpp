#include <foo/ast_printer.hpp>

#include <format>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <foo/util/overloaded.hpp>


namespace foo
{
    using namespace ast;

    static std::string print(const StmtPtr& s, int indent);
    static std::string print(const Block& b, int indent);
    static std::string print_pattern(const Pattern& p);

    static std::string pad(int indent)
    {
        return std::string(static_cast<size_t>(indent) * 4, ' ');
    }

    static std::string_view op_str(UnaryOp op)
    {
        switch (op)
        {
            case UnaryOp::Neg: return "-";
            case UnaryOp::Not: return "!";
        }
        return "?";
    }

    static std::string_view op_str(BinaryOp op)
    {
        switch (op)
        {
            case BinaryOp::Add:         return "+";
            case BinaryOp::Sub:         return "-";
            case BinaryOp::Mul:         return "*";
            case BinaryOp::Div:         return "/";
            case BinaryOp::Mod:         return "%";
            case BinaryOp::Eq:          return "==";
            case BinaryOp::NotEq:       return "!=";
            case BinaryOp::Less:        return "<";
            case BinaryOp::LessEq:      return "<=";
            case BinaryOp::Greater:     return ">";
            case BinaryOp::GreaterEq:   return ">=";
            case BinaryOp::And:         return "&&";
            case BinaryOp::Or:          return "||";
            case BinaryOp::Assign:      return "=";
            case BinaryOp::Range:               return "..";
            case BinaryOp::RangeInclusive:      return "..=";
        }
        return "?";
    }

    static std::string join(const std::vector<std::string>& parts, std::string_view sep)
    {
        std::string out;
        for (size_t i = 0; i < parts.size(); i++)
        {
            if (i)
                out += sep;

            out += parts[i];
        }
        return out;
    }

    static std::string list(const std::vector<ExprPtr>& items, int indent = 0)
    {
        std::string out;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (i)
                out += ", ";

            out += to_string(items[i], indent);
        }
        return out;
    }

    static std::string element_string(const TypeExprPtr& element)
    {
        const std::string text = to_string(element);
        return element && std::holds_alternative<FnType>(element->type) ? "(" + text + ")" : text;
    }

    std::string to_string(const TypeExpr& t)
    {
        return std::visit(overload{
            [](const NamedType& n)
                { return join(n.path, "::"); },

            [](const ArrayType& n)
                { return element_string(n.element_type) + "[]"; },

            [](const MapType& n)
                { return std::format("[{}, {}]", to_string(n.key_type), to_string(n.value_type)); },

            [](const OptionalType& n)
                { return std::format("{}?", element_string(n.element_type)); },

            [](const SetType& n)
                { return std::format("[{}]", to_string(n.element_type)); },

            [](const TupleType& n)
                {
                    std::string out = "(";
                    for (size_t i = 0; i < n.elements.size(); ++i)
                    {
                        if (i)
                            out += ", ";

                        if (!n.elements[i].name.empty())
                            out += n.elements[i].name + ": ";

                        out += to_string(n.elements[i].type);
                    }
                    return out + ")";
                },

            [](const VariantType& n)
                {
                    std::string out = "(";
                    for (size_t i = 0; i < n.alternatives.size(); ++i)
                    {
                        if (i)
                            out += " | ";

                        out += to_string(n.alternatives[i]);
                    }
                    return out + ")";
                },

            [](const FnType& n)
                {
                    std::string out = "fn(";
                    for (size_t i = 0; i < n.params.size(); ++i)
                    {
                        if (i)
                            out += ", ";

                        out += to_string(n.params[i]);
                    }
                    out += ")";

                    if (n.ret)
                        out += " -> " + to_string(n.ret);

                    return out;
                },

        }, t.type);
    }

    std::string to_string(const Expr& e, int indent)
    {
        return std::visit(overload{
            [](const IntLit& n)
                { return std::to_string(n.value); },

            [](const FloatLit& n)
                { return std::format("{}", n.value); },

            [](const StringLit& n)
                { return std::format("\"{}\"", n.value); },

            [](const BoolLit& n)
                { return std::string(n.value ? "true" : "false"); },

            [&](const ArrayLit& n)
                { return "[" + list(n.elements, indent) + "]"; },

            [&](const TupleLit& n)
                { return "(" + list(n.elements, indent) + ")"; },

            [](const NoneLit&) -> std::string
                { return "none"; },

            [](const Name& n)
                { return join(n.path, "::"); },

            [&](const Unary& n)
                { return std::format("({}{})", op_str(n.op), to_string(n.operant, indent)); },

            [&](const Binary& n)
                { return std::format("({} {} {})", to_string(n.lhs, indent), op_str(n.op), to_string(n.rhs, indent)); },

            [&](const Index& n)
                { return std::format("{}[{}]", to_string(n.target), to_string(n.indexer)); },

            [&](const Call& n)
                { return std::format("{}({})", to_string(n.callee, indent), list(n.args, indent)); },

            [](const Member& n)
                { return std::format("{}.{}", to_string(n.target), n.name); },

            [&](const StaticCall& n)
                { return std::format("@{}.{}({})", to_string(n.target), n.method, list(n.args, indent)); },

            [&](const As& n)
                { return std::format("({} as {})", to_string(n.expr, indent), to_string(n.type)); },

            [&](const PatternAssign& n)
                { return std::format("({} = {})", print_pattern(*n.pattern), to_string(n.value, indent)); },

            [&](const StructLit& n)
                {
                    std::string out = "#" + to_string(n.type) + "{";
                    for (size_t i = 0; i < n.fields.size(); ++i)
                    {
                        if (i)
                            out += ", ";

                        out += n.fields[i].name + " = " + to_string(n.fields[i].value, indent);
                    }
                    return out + "}";
                },

            [&](const Lambda& n)
                {
                std::string out = n.reffed ? "ref |" : "|";
                    for (size_t i = 0; i < n.params.size(); i++)
                    {
                        if (i)
                            out += ", ";

                        out += n.params[i].name;

                        if (n.params[i].type)
                            out += ": " + to_string(n.params[i].type);
                    }
                    out += "|";

                    if (n.return_type)
                        out += " -> " + to_string(n.return_type);

                    return out + " " + print(n.body, indent);
                },

        }, e.kind);
    }

    static std::string print_pattern(const Pattern& p)
    {
        return std::visit(overload{
            [](const PatName& n)
                { return n.name; },

            [](const PatWild&) -> std::string
                { return "_"; },

            [](const PatPlace& n)
                { return to_string(n.place); },

            [](const PatTuple& n)
                {
                    std::string out = "(";
                    for (size_t i = 0; i < n.elements.size(); ++i)
                    {
                        if (i)
                            out += ", ";

                        out += print_pattern(*n.elements[i]);
                    }
                    return out + ")";
                },
        }, p.kind);
    }

    static std::string body(const StmtPtr& s, int indent)
    {
        return "\n" + pad(indent) + print(s, indent);
    }

    static std::string print(const Block& b, int indent)
    {
        std::string out = "{\n";
        for (const auto& s : b.statements)
            out += pad(indent + 1) + print(s, indent + 1) + "\n";

        return out + pad(indent) + "}";
    }

    static std::string print(const Stmt& s, int indent)
    {
        return std::visit(overload{
            [&](const Break& n) { return std::string{"break"}; },
            [&](const Continue& n) { return std::string{"continue"}; },
            [&](const Block& n) { return print(n, indent); },
            [&](const ExprStmt& n) { return to_string(n.expr, indent) + ";"; },
            [&](const VarDecl& n)
            {
                std::string out = "var " + print_pattern(*n.pattern);
                if (n.type) out += ": " + to_string(n.type);
                if (n.init) out += " = " + to_string(n.init, indent);
                return out + ";";
            },
            [&](const Return& n)
            {
                return n.value ? "return " + to_string(n.value, indent) + ";" : std::string("return;");
            },
            [&](const If& n)
            {
                std::string out = "if " + to_string(n.cond, indent) + body(n.then_branch, indent);
                if (n.else_branch)
                {
                    out += "\n" + pad(indent) + "else";
                    out += std::holds_alternative<If>(n.else_branch->kind)
                        ? " " + print(n.else_branch, indent)
                        : body(n.else_branch, indent);
                }
                return out;
            },
            [&](const Check& n)
            {
                std::string out = "check " + to_string(n.expr, indent) + " => " + n.variable_name + body(n.good_branch, indent);
                if (n.bad_branch)
                {
                    out += "\n" + pad(indent) + "else";
                    out += body(n.bad_branch, indent);
                }
                return out;
            },
            [&](const While& n) { return "while " + to_string(n.cond, indent) + body(n.body, indent); },
            [&](const ForIn& n)
            {
                return "for " + n.variable_name + " in " + to_string(n.iterable, indent) + body(n.body, indent);
            },
        }, s.kind);
    }

    static std::string print(const StmtPtr& s, int indent)
    {
        return s ? print(*s, indent) : "<null>";
    }

    static std::string print(const FnDecl& f, int indent)
    {
        std::string out = "fn " + f.name + "(";
        for (size_t i = 0; i < f.params.size(); i++)
        {
            if (i) out += ", ";
            out += (f.params[i].reffed ? "ref " : "") + f.params[i].name + ": " + to_string(f.params[i].type);
        }
        out += ")";
        if (f.return_type) out += " -> " + to_string(f.return_type);

        return out + "\n" + pad(indent) + print(f.body, indent);
    }

    static std::string print(const Decl& d, int indent)
    {
        return std::visit(overload{
            [&](const FnDecl& n) { return print(n, indent); },
            [&](const StructDecl& n)
            {
                std::string out = "struct " + n.name + "\n" + pad(indent) + "{\n";
                for (const auto& f : n.fields)
                    out += pad(indent + 1) + f.name + ": " + to_string(f.type) + ";\n";
                for (const auto& m : n.methods)
                    out += pad(indent + 1) + print(m, indent + 1) + "\n";
                return out + pad(indent) + "}";
            },
            [&](const EnumDecl& n)
            {
                std::string out = "enum " + n.name + "\n" + pad(indent) + "{\n";
                for (const auto& v : n.variants)
                    out += pad(indent + 1) + v.name + ",\n";
                return out + pad(indent) + "}";
            },
            [&](const NamespaceDecl& n)
            {
                std::string out = "namespace " + n.name + "\n" + pad(indent) + "{\n";
                for (const auto& child : n.decls)
                    out += pad(indent + 1) + (child ? print(*child, indent + 1) : "<null>") + "\n";
                return out + pad(indent) + "}";
            },
            [&](const AliasDecl& n)
            {
                return "type " + n.name + " = " + to_string(n.type) + ";";
            },
            [&](const InterfaceDecl& n)
            {
                return "interface " + n.name + ";";
            }
        }, d.kind);
    }



    std::string to_string(const Stmt& s, int i) { return pad(i) + print(s, i); }
    std::string to_string(const Decl& d, int i) { return pad(i) + print(d, i); }

    std::string to_string(const TypeExprPtr& t) { return t ? to_string(*t) : "<null>"; }

    std::string to_string(const ExprPtr& e, int i) { return e ? to_string(*e, i) : "<null>"; }

    std::string to_string(const StmtPtr& s, int i) { return s ? to_string(*s, i) : pad(i) + "<null>"; }
    std::string to_string(const DeclPtr& d, int i) { return d ? to_string(*d, i) : pad(i) + "<null>"; }
}