#ifndef FOO_AST_HPP
#define FOO_AST_HPP


#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <foo/source.hpp>

#include <foo/util/ptr_eq.hpp>


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

    struct NamedType
    {
        std::vector<std::string> parts;

        bool operator==(const NamedType&) const = default;
    };
    struct ArrayType
    {
        TypeExprPtr element_type;

        bool operator==(const ArrayType& other) const
        {
            return ptr_equal(element_type, other.element_type);
        }
    };
    struct MapType
    {
        TypeExprPtr key_type; TypeExprPtr value_type;

        bool operator==(const MapType& other) const
        {
            return ptr_equal(key_type, other.key_type) && foo::ptr_equal(value_type, other.value_type);
        }
    };
    struct OptionalType
    {
        TypeExprPtr element_type;

        bool operator==(const OptionalType& other) const
        {
            return ptr_equal(element_type, other.element_type);
        }
    };

    using TypeExprKind = std::variant<NamedType, ArrayType, MapType, OptionalType>;

    struct TypeExpr
    {
        TypeExprKind type;
        SourceSpan span = {};

        bool operator==(const TypeExpr& other) const
        {
            return type == other.type;
        }
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
        And, Or,
        Range, RangeInclusive,
        Assign
    };


    // expressions

    struct IntLit
    {
        int64_t value;
        bool operator==(const IntLit&) const = default;
    };
    struct FloatLit
    {
        double value;
        bool operator==(const FloatLit&) const = default;
    };
    struct StringLit
    {
        std::string value;
        bool operator==(const StringLit&) const = default;
    };
    struct BoolLit
    {
        bool value;
        bool operator==(const BoolLit&) const = default;
    };
    struct ArrayLit
    {
        std::vector<ExprPtr> elements;

        bool operator==(const ArrayLit& other) const
        {
            return ptr_range_equal(elements, other.elements);
        }
    };

    struct FieldInit
    {
        std::string name;
        ExprPtr value;
        SourceSpan span = {};

        bool operator==(const FieldInit& other) const
        {
            return name == other.name && ptr_equal(value, other.value);
        }
    };
    struct StructLit
    {
        TypeExprPtr type;
        std::vector<FieldInit> fields;

        bool operator==(const StructLit& other) const
        {
            return ptr_equal(type, other.type) && fields == other.fields;
        }
    };

    struct Name
    {
        std::vector<std::string> parts;
        bool operator==(const Name&) const = default;
    };
    struct Unary
    {
        UnaryOp op; ExprPtr operant;

        bool operator==(const Unary& other) const
        {
            return op == other.op
                && ptr_equal(operant, other.operant);
        }
    };
    struct Binary
    {
        BinaryOp op; ExprPtr lhs; ExprPtr rhs;

        bool operator==(const Binary& other) const
        {
            return op == other.op
                && ptr_equal(lhs, other.lhs)
                && ptr_equal(rhs, other.rhs);
        }
    };
    struct Call
    {
        ExprPtr callee; std::vector<ExprPtr> args;

        bool operator==(const Call& other) const
        {
            return ptr_equal(callee, other.callee)
                && ptr_range_equal(args, other.args);
        }
    };
    struct Index
    {
        ExprPtr target; ExprPtr indexer;

        bool operator==(const Index& other) const
        {
            return ptr_equal(target, other.target)
                && ptr_equal(indexer, other.indexer);
        }
    };
    struct Member
    {
        ExprPtr target; std::string name;

        bool operator==(const Member& other) const
        {
            return ptr_equal(target, other.target)
                && name == other.name;
        }
    };

    struct StaticCall
    {
        TypeExprPtr target;
        std::string method;
        std::vector<ExprPtr> args;

        bool operator==(const StaticCall& other) const
        {
            return ptr_equal(target, other.target)
                && method == other.method
                && ptr_range_equal(args, other.args);
        }
    };

    using ExprKind = std::variant<
        IntLit, FloatLit, StringLit, BoolLit, ArrayLit,
        StructLit,
        Name,
        Unary, Binary,
        Index, Call, Member,
        StaticCall>;

    struct Expr
    {
        ExprKind kind;
        SourceSpan span = {};

        bool operator==(const Expr& other) const
        {
            return kind == other.kind;
        }
    };


    // statements

    struct Block
    {
        std::vector<StmtPtr> statements;

        bool operator==(const Block& other) const
        {
            return ptr_range_equal(statements, other.statements);
        }
    };

    struct VarDecl
    {
        std::string name;
        TypeExprPtr type;
        ExprPtr init{}; // can be null

        bool operator==(const VarDecl& other) const
        {
            return name == other.name
                && ptr_equal(type, other.type)
                && ptr_equal(init, other.init);
        }
    };

    struct ExprStmt
    {
        ExprPtr expr;

        bool operator==(const ExprStmt& other) const
        {
            return ptr_equal(expr, other.expr);
        }
    };
    struct Return
    {
        ExprPtr value{}; /* can be null */

        bool operator==(const Return& other) const
        {
            return ptr_equal(value, other.value);
        }
    };

    struct If
    {
        ExprPtr cond;
        StmtPtr then_branch;
        StmtPtr else_branch{}; // nullptr if no else


        bool operator==(const If& other) const
        {
            return ptr_equal(cond, other.cond)
                && ptr_equal(then_branch, other.then_branch)
                && ptr_equal(else_branch, other.else_branch);
        }
    };

    struct While
    {
        ExprPtr cond;
        StmtPtr body;

        bool operator==(const While& other) const
        {
            return ptr_equal(cond, other.cond)
                && ptr_equal(body, other.body);
        }
    };

    struct ForIn
    {
        std::string variable_name;
        ExprPtr iterable;
        StmtPtr body;

        bool operator==(const ForIn& other) const
        {
            return variable_name == other.variable_name
                && ptr_equal(iterable, other.iterable)
                && ptr_equal(body, other.body);
        }
    };

    struct Check
    {
        ExprPtr expr;
        std::string variable_name;
        StmtPtr good_branch;
        StmtPtr bad_branch{}; // can be null ofc


        bool operator==(const Check& other) const
        {
            return ptr_equal(expr, other.expr)
                && variable_name == other.variable_name
                && ptr_equal(good_branch, other.good_branch)
                && ptr_equal(bad_branch, other.bad_branch);
        }
    };

    using StmtKind = std::variant<
            Block, VarDecl, Return, If, While, ForIn,
            ExprStmt, Check>;

    struct Stmt
    {
        StmtKind kind;
        SourceSpan span = {};

        bool operator==(const Stmt& other) const
        {
            return kind == other.kind;
        }
    };


    // declarations

    struct Param
    {
        std::string name;
        TypeExprPtr type;
        bool reffed = false;
        SourceSpan span = {};

        bool operator==(const Param& other) const
        {
            return name == other.name
                && ptr_equal(type, other.type)
                && reffed == other.reffed;
        }
    };

    struct FnDecl
    {
        std::string name;
        std::vector<Param> params;
        TypeExprPtr return_type;
        Block body;

        bool operator==(const FnDecl& other) const
        {
            return name == other.name
                && ptr_equal(return_type, other.return_type)
                && params == other.params
                && body == other.body;
        };
    };

    struct StructField
    {
        std::string name;
        TypeExprPtr type;
        SourceSpan span = {};

        bool operator==(const StructField& other) const
        {
            return name == other.name
                && foo::ptr_equal(type, other.type);
        }
    };

    struct StructDecl
    {
        std::string name;
        std::vector<StructField> fields;
        std::vector<FnDecl> methods;

        bool operator==(const StructDecl&) const = default;
    };

    struct NamespaceDecl
    {
        std::string name;
        std::vector<DeclPtr> decls;

        bool operator==(const NamespaceDecl& other) const
        {
            return name == other.name
                && foo::ptr_range_equal(decls, other.decls);
        }
    };

    struct ImportDecl
    {
        std::string name;
        SourceSpan span = {};

        bool operator==(const ImportDecl& other) const
        {
            return name == other.name;
        }
    };

    using DeclKind = std::variant<FnDecl, StructDecl, NamespaceDecl>;

    struct Decl
    {
        DeclKind kind;
        SourceSpan span = {};

        bool operator==(const Decl& other) const
        {
            return kind == other.kind;
        }
    };
}



#endif //FOO_AST_HPP