#ifndef FOO_AST_HPP
#define FOO_AST_HPP


#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <foo/source.hpp>


namespace foo
{
    namespace ast
    {
        struct Stmt;
        struct Expr;
        struct TypeExpr;


        using TypeExprPtr = std::shared_ptr<TypeExpr>;
        using StmtPtr = std::unique_ptr<Stmt>;
        using ExprPtr = std::unique_ptr<Expr>;


        struct NamedType { std::string name; };
        struct ArrayType { TypeExprPtr element_type; };

        enum class UnaryOp
        {
            Neg, Not
        };

        enum class BinaryOp
        {
            Add, Sub, Mul, Div, Mod,
            Eq, NotEq, Less, LessEq, Greater, GreaterEq,
            And, Or
        };

        struct IntLiteral       { int64_t value; };
        struct FloatLiteral     { double value; };
        struct StringLiteral    { std::string value; };
        struct BoolLiteral      { bool value; };
        struct ArrayLiteral     { std::vector<ExprPtr> elements; };

        struct Name             { std::string name; };
        struct Unary            { UnaryOp op; ExprPtr operant; };
        struct Binary           { BinaryOp op; ExprPtr lhs; ExprPtr rhs; };
        struct Call             { ExprPtr callee; std::vector<ExprPtr> args; };
        struct Index            { ExprPtr target; ExprPtr indexer; };
        struct Member           { ExprPtr target; std::string name; };


    }
}


#endif //FOO_AST_HPP