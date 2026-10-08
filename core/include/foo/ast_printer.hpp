#ifndef FOO_AST_PRINTER_HPP
#define FOO_AST_PRINTER_HPP


#include <string>
#include <foo/ast.hpp>


namespace foo
{
    std::string to_string(const ast::TypeExpr& type);
    std::string to_string(const ast::TypeExprPtr& type_ptr);
}


#endif //FOO_AST_PRINTER_HPP
