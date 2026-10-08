#include <foo/ast_printer.hpp>
#include <foo/ast.hpp>

#include <foo/util/overloaded.hpp>


namespace foo
{
    inline std::string to_string(const ast::TypeExprPtr& type_ptr)
    {
        if (!type_ptr)
            return"<null_type>";

        return to_string(*type_ptr);
    }

    std::string to_string(const ast::TypeExpr& type)
    {
        return std::visit(overload{
            [](const ast::NamedType& t)
            {
                std::string result;
                for (size_t i = 0; i < t.parts.size(); ++i)
                {
                    if (i > 0)
                        result += "::";

                    result += t.parts[i];
                }
                return result;
            },
            [](const ast::ArrayType& t)
            {
                return to_string(t.element_type) + "[]";
            },
            [](const ast::MapType& t)
            {
                return "[" + to_string(t.key_type) + ", " + to_string(t.value_type) + "]";
            }
        }, type.type);
    }

    std::string to_string(const ast::DeclPtr& decl_ptr)
    {
        if (!decl_ptr)
            return"<null_decl>";

        return to_string(*decl_ptr);
    }

    inline std::string to_string(const ast::Param& param)
    {
        std::string result = param.name + ": ";

        if (param.reffed)
            result += "ref ";

        result += to_string(param.type);
        return result;
    }

    std::string to_string(const ast::Decl& decl)
    {
        return std::visit<std::string>(overload{
            [](const ast::FnDecl& fn)
            {
                std::ostringstream out;
                out << "fn " << fn.name << "(";

                for (size_t i = 0; i < fn.params.size(); ++i)
                {
                    if (i > 0) out << ", ";
                    out << to_string(fn.params[i]);
                }
                out << ")";

                if (fn.return_type)
                {
                    out << ": " << to_string(fn.return_type);
                }

                return out.str();
            },
            [](const auto&)
            {
                return "else";
            }
        }, decl.kind);
    }
}