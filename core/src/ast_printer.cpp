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

    static std::string list(const std::vector<ExprPtr>& items)
    {
        std::string out;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (i)
                out += ", ";

            out += to_string(items[i]);
        }
        return out;
    }

    std::string to_string(const TypeExpr& t)
    {
        return std::visit(overload{
            [](const NamedType& n)
                { return join(n.path, "::"); },

            [](const ArrayType& n)
                { return to_string(n.element_type) + "[]"; },

            [](const MapType& n)
                { return std::format("[{}, {}]", to_string(n.key_type), to_string(n.value_type)); },

            [](const OptionalType& n)
                { return std::format("{}?", to_string(n.element_type)); },

        }, t.type);
    }

    std::string to_string(const Expr& e)
    {
        return std::visit(overload{
            [](const IntLit& n)
                { return std::to_string(n.value); },

            [](const FloatLit& n)
                { return std::format("{}", n.value); },

            [](const StringLit& n) \
                { return std::format("\"{}\"", n.value); },

            [](const BoolLit& n)
                { return std::string(n.value ? "true" : "false"); },

            [](const ArrayLit& n)
                { return "[" + list(n.elements) + "]"; },

            [](const Name& n)
                { return join(n.path, "::"); },

            [](const Unary& n)
                { return std::format("({}{})", op_str(n.op), to_string(n.operant)); },

            [](const Binary& n)
                { return std::format("({} {} {})", to_string(n.lhs), op_str(n.op), to_string(n.rhs)); },

            [](const Index& n)
                { return std::format("{}[{}]", to_string(n.target), to_string(n.indexer)); },

            [](const Call& n)
                { return std::format("{}({})", to_string(n.callee), list(n.args)); },

            [](const Member& n)
                { return std::format("{}.{}", to_string(n.target), n.name); },

            [](const StaticCall& n)
                { return std::format("@{}.{}({})", to_string(n.target), n.method, list(n.args)); },

            [](const StructLit& n)
                {
                    std::string out = "#" + to_string(n.type) + "{";
                    for (size_t i = 0; i < n.fields.size(); ++i)
                    {
                        if (i)
                            out += ", ";

                        out += n.fields[i].name + " = " + to_string(n.fields[i].value);
                    }
                    return out + "}";
                },

        }, e.kind);
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
            [&](const Block& n) { return print(n, indent); },
            [&](const ExprStmt& n) { return to_string(n.expr) + ";"; },
            [&](const VarDecl& n)
            {
                std::string out = "var " + n.name;
                if (n.type) out += ": " + to_string(n.type);
                if (n.init) out += " = " + to_string(n.init);
                return out + ";";
            },
            [&](const Return& n)
            {
                return n.value ? "return " + to_string(n.value) + ";" : std::string("return;");
            },
            [&](const If& n)
            {
                std::string out = "if " + to_string(n.cond) + body(n.then_branch, indent);
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
                std::string out = "check " + to_string(n.expr) + " => " + n.variable_name +body(n.good_branch, indent);
                if (n.bad_branch)
                {
                    out += "\n" + pad(indent) + "else";
                    body(n.bad_branch, indent);
                }
                return out;
            },
            [&](const While& n) { return "while " + to_string(n.cond) + body(n.body, indent); },
            [&](const ForIn& n)
            {
                return "for " + n.variable_name + " in " + to_string(n.iterable) + body(n.body, indent);
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
            [&](const NamespaceDecl& n)
            {
                std::string out = "namespace " + n.name + "\n" + pad(indent) + "{\n";
                for (const auto& child : n.decls)
                    out += pad(indent + 1) + (child ? print(*child, indent + 1) : "<null>") + "\n";
                return out + pad(indent) + "}";
            }
        }, d.kind);
    }



    std::string to_string(const Stmt& s) { return print(s, 0); }
    std::string to_string(const Decl& d) { return print(d, 0); }

    std::string to_string(const TypeExprPtr& t) { return t ? to_string(*t) : "<null>"; }
    std::string to_string(const ExprPtr& e)     { return e ? to_string(*e) : "<null>"; }
    std::string to_string(const StmtPtr& s)     { return s ? to_string(*s) : "<null>"; }
    std::string to_string(const DeclPtr& d)     { return d ? to_string(*d) : "<null>"; }
}