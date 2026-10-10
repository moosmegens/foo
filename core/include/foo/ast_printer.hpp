#ifndef FOO_AST_PRINTER_HPP
#define FOO_AST_PRINTER_HPP


#include <string>
#include <foo/ast.hpp>


namespace foo
{
    std::string to_string(const ast::TypeExpr& type);
    std::string to_string(const ast::TypeExprPtr& type_ptr);

    std::string to_string(const ast::Decl& decl);
    std::string to_string(const ast::DeclPtr& decl_ptr);

    std::string to_string(const ast::Expr& decl);
    std::string to_string(const ast::ExprPtr& decl_ptr);

    std::string to_string(const ast::Stmt& decl);
    std::string to_string(const ast::StmtPtr& decl_ptr);
}


#endif //FOO_AST_PRINTER_HPP
