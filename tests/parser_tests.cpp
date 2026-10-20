#include <iostream>

#include "doctest.hpp"

#include <foo/ast.hpp>
#include <foo/parser.hpp>

#include <string_view>

#include "foo/lexer.hpp"

#include <foo/ast_printer.hpp>


using namespace foo;

static ParseOutput simple_parse(std::string_view src, bool should_fail = false)
{
    auto tkns = tokenize(src);
    auto r = parse(tkns.tokens);

    for (const auto& err : r.errors)
    {
        FAIL_CHECK("parser error: " << err.error);
    }

    if (should_fail)
    {
        REQUIRE(!r.errors.empty());
    } else
    {
        REQUIRE(r.errors.empty());
    }

    return r;
}

bool program_equal(std::span<const ast::DeclPtr> expected, std::span<const ast::DeclPtr> actual)
{
    return foo::ptr_range_equal(expected, actual);
}

void program_should_equal(std::span<const ast::DeclPtr> expected, std::span<const ast::DeclPtr> actual)
{
    if (ptr_range_equal(expected, actual))
        return;

    std::ostringstream expected_str;
    for (const auto& decl : expected)
        expected_str << to_string(decl) << '\n';

    std::ostringstream actual_str;
    for (const auto& decl : actual)
        actual_str << to_string(decl) << '\n';

    FAIL_CHECK(
        "AST programs differ\n"
        "Expected AST:\n" << expected_str.str() <<
        "\nActual AST:\n" << actual_str.str()
    );
}

ast::ExprPtr int_expr(int64_t value)
{
    return std::make_unique<ast::Expr>(
        ast::Expr{ast::IntLit{value}}
    );
}

ast::ExprPtr bool_expr(bool value)
{
    return std::make_unique<ast::Expr>(
        ast::Expr{ast::BoolLit{value}}
    );
}

ast::ExprPtr binary(
    ast::BinaryOp op,
    ast::ExprPtr lhs,
    ast::ExprPtr rhs)
{
    return std::make_unique<ast::Expr>(
        ast::Expr{
            ast::Binary{op, std::move(lhs), std::move(rhs)}
        }
    );
}

ast::ExprPtr unary(ast::UnaryOp op, ast::ExprPtr operand)
{
    return std::make_unique<ast::Expr>(
        ast::Expr{
            ast::Unary{op, std::move(operand)}
        }
    );
}

ast::StmtPtr return_stmt(ast::ExprPtr value)
{
    return std::make_unique<ast::Stmt>(
        ast::Stmt{ast::Return{std::move(value)}}
    );
}

ast::StmtPtr block_stmt(ast::StmtPtr stmt)
{
    ast::Block block;
    block.statements.push_back(std::move(stmt));

    return std::make_unique<ast::Stmt>(
        ast::Stmt{std::move(block)}
    );
}

ast::DeclPtr main_decl(ast::Block body)
{
    return std::make_unique<ast::Decl>(
        ast::Decl{
            ast::FnDecl{
                "main",
                {},
                std::make_shared<ast::TypeExpr>(
                    ast::TypeExpr{ast::NamedType{{"int"}}, {}}
                ),
                std::move(body)
            },
        }
    );
}


TEST_SUITE("parser")
{
    TEST_CASE("main")
    {
        auto r = simple_parse("fn main() -> int {}");

        ast::DeclPtr main = main_decl({});

        program_should_equal({std::span{&main, 1}}, r.decls);
    }

    TEST_CASE("if case")
    {
        auto r = simple_parse(
            "fn main() -> int { "
            "if true { return 1; } "
            "else { return 0; } "
            "}"
        );

        ast::Block body;
        body.statements.push_back(
            std::make_unique<ast::Stmt>(
                ast::Stmt{
                    ast::If{
                        bool_expr(true),
                        block_stmt(return_stmt(int_expr(1))),
                        block_stmt(return_stmt(int_expr(0)))
                    },
                }
            )
        );

        auto expected = main_decl(std::move(body));

        program_should_equal({std::span{&expected, 1}}, r.decls);
    }

    TEST_CASE("operators")
    {
        auto r = simple_parse(
            "fn main() -> int { "
            "return -(1 + 2) * 3 + 10 / 2 % 3; "
            "}"
        );

        auto expr = binary(
            ast::BinaryOp::Add,
            binary(
                ast::BinaryOp::Mul,
                unary(
                    ast::UnaryOp::Neg,
                    binary(
                        ast::BinaryOp::Add,
                        int_expr(1),
                        int_expr(2)
                    )
                ),
                int_expr(3)
            ),
            binary(
                ast::BinaryOp::Mod,
                binary(
                    ast::BinaryOp::Div,
                    int_expr(10),
                    int_expr(2)
                ),
                int_expr(3)
            )
        );

        ast::Block body;
        body.statements.push_back(return_stmt(std::move(expr)));

        auto expected = main_decl(std::move(body));

        program_should_equal({std::span{&expected, 1}}, r.decls);
    }

    TEST_CASE("postfix expression chaining")
    {
        auto r = simple_parse(
            "fn main() -> int { return foo(1, 2)[0].bar; }"
        );

        auto callee = std::make_unique<ast::Expr>(
            ast::Expr{
            ast::Name{
                {"foo"}
            }
        }
        );

        std::vector<ast::ExprPtr> args;
        args.push_back(int_expr(1));
        args.push_back(int_expr(2));

        auto call = std::make_unique<ast::Expr>(
            ast::Expr{
                ast::Call{std::move(callee), std::move(args)}
            }
        );

        auto indexed = std::make_unique<ast::Expr>(
            ast::Expr{
                ast::Index{std::move(call), int_expr(0)}
            }
        );

        auto expr = std::make_unique<ast::Expr>(
            ast::Expr{
                ast::Member{std::move(indexed), "bar"}
            }
        );

        ast::Block body;
        body.statements.push_back(return_stmt(std::move(expr)));

        auto expected = main_decl(std::move(body));
        program_should_equal({std::span{&expected, 1}}, r.decls);
    }

    TEST_CASE("array literal expressions")
    {
        auto r = simple_parse(
            "fn main() -> int { return [1, 2 + 3, 4 * 5]; }"
        );

        std::vector<ast::ExprPtr> elements;
        elements.push_back(int_expr(1));
        elements.push_back(
            binary(ast::BinaryOp::Add, int_expr(2), int_expr(3))
        );
        elements.push_back(
            binary(ast::BinaryOp::Mul, int_expr(4), int_expr(5))
        );

        auto expr = std::make_unique<ast::Expr>(
            ast::Expr{
                ast::ArrayLit{std::move(elements)}
            }
        );

        ast::Block body;
        body.statements.push_back(return_stmt(std::move(expr)));

        auto expected = main_decl(std::move(body));
        program_should_equal({std::span{&expected, 1}}, r.decls);
    }

    TEST_CASE("struct declaration")
    {
        auto r = simple_parse(
            "struct Point { x: int; y: string; }"
        );

        std::vector<ast::StructField> fields;
        fields.push_back({
            "x",
            std::make_shared<ast::TypeExpr>(
                ast::TypeExpr{ast::NamedType{{"int"}}, {}}
            )
        });
        fields.push_back({
            "y",
            std::make_shared<ast::TypeExpr>(
                ast::TypeExpr{ast::NamedType{{"string"}}, {}}
            )
        });

        ast::DeclPtr expected = std::make_unique<ast::Decl>(
            ast::Decl{
                ast::StructDecl{
                    "Point",
                    std::move(fields),
                    {}
                }
            }
        );

        program_should_equal({std::span{&expected, 1}}, r.decls);
    }
}

