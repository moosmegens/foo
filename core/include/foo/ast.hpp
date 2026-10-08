#ifndef FOO_AST_HPP
#define FOO_AST_HPP


#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <foo/source.hpp>


namespace foo::ast
{
    struct Stmt;
    struct Expr;
    struct TypeExpr;
    struct Decl;


    using TypeExprPtr = std::shared_ptr<TypeExpr>;
    using StmtPtr = std::unique_ptr<Stmt>;
    using ExprPtr = std::unique_ptr<Expr>;
    using DeclPtr = std::unique_ptr<Decl>;


    // type expressions

    struct NamedType { std::vector<std::string> parts;  };
    struct ArrayType { TypeExprPtr element_type; };
    struct MapType   { TypeExprPtr key_type; TypeExprPtr value_type; };

    using TypeExprKind = std::variant<NamedType, ArrayType, MapType>;

    struct TypeExpr
    {
        TypeExprKind type;
        SourceSpan span;
    };


    // ops

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


    // expressions

    struct IntLit       { int64_t value; };
    struct FloatLit     { double value; };
    struct StringLit    { std::string value; };
    struct BoolLit      { bool value; };
    struct ArrayLit     { std::vector<ExprPtr> elements; };

    struct Name             { std::vector<std::string> parts; };
    struct Unary            { UnaryOp op; ExprPtr operant; };
    struct Binary           { BinaryOp op; ExprPtr lhs; ExprPtr rhs; };
    struct Call             { ExprPtr callee; std::vector<ExprPtr> args; };
    struct Index            { ExprPtr target; ExprPtr indexer; };
    struct Member           { ExprPtr target; std::string name; };

    struct StaticCall
    {
        TypeExprPtr target;
        std::string method;
        std::vector<ExprPtr> args;
    };

    using ExprKind = std::variant<
        IntLit, FloatLit, StringLit, BoolLit, ArrayLit,
        Name,
        Unary, Binary,
        Index, Call, Member,
        StaticCall>;

    struct Expr
    {
        ExprKind kind;
        SourceSpan span;
    };


    // statements

    struct Block { std::vector<StmtPtr> statements; };

    struct VarDecl
    {
        std::string name;
        TypeExprPtr type;
        ExprPtr init; // can be null
    };

    struct Assign   { ExprPtr target, value; };
    struct ExprStmt { ExprPtr expr; };
    struct Return   { ExprPtr value; /* can be null */ };

    struct If
    {
        ExprPtr cond;
        StmtPtr then_branch;
        StmtPtr else_branch; // nullptr if no else
    };

    struct While
    {
        ExprPtr cond;
        StmtPtr body;
    };

    struct ForIn
    {
        std::string variable_name;
        ExprPtr iterable;
        StmtPtr body;
    };

    using StmtKind = std::variant<Block, VarDecl, Assign, ExprStmt, Return, If, While, ForIn>;

    struct Stmt
    {
        StmtKind kind;
        SourceSpan span;
    };


    // declarations

    struct Param
    {
        std::string name;
        TypeExprPtr type;
        bool reffed = false;
        SourceSpan span;
    };

    struct FnDecl
    {
        std::string name;
        std::vector<Param> params;
        TypeExprPtr return_type;
        std::vector<StmtPtr> body;
    };

    struct StructField
    {
        std::string name;
        TypeExprPtr type;
        SourceSpan span;
    };

    struct StructDecl
    {
        std::string name;
        std::vector<StructField> fields;
        std::vector<FnDecl> methods;
    };

    struct NamespaceDecl
    {
        std::string name;
        std::vector<DeclPtr> decls;
    };

    using DeclKind = std::variant<FnDecl, StructDecl, NamespaceDecl>;

    struct Decl
    {
        SourceSpan span;
        DeclKind kind;
    };
}



#endif //FOO_AST_HPP