#ifndef FOO_AST_HPP
#define FOO_AST_HPP


#include <cstdint>
#include <memory>
#include <string>

#include <foo/source.hpp>


#define FOO_AST(NAME, TO_STRING, ...) \
    struct NAME {                                  \
        static constexpr std::string_view name = #NAME; \
        __VA_ARGS__                                \
        std::string to_string() const { return TO_STRING; } \
        bool operator==(const NAME&) const = default; \
    }


namespace foo
{
    namespace ast
    {
        struct Stmt;
        struct Expr;

        using StmtPtr = std::unique_ptr<Stmt>;
        using ExprPtr = std::unique_ptr<Expr>;


    }
}


#undef FOO_AST


#endif //FOO_AST_HPP