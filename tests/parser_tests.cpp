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


TEST_SUITE("parser")
{
    TEST_CASE("main")
    {
        auto r = simple_parse("fn main() -> int {}");

        ast::DeclPtr main = std::make_unique<ast::Decl>(
            ast::Decl{
                ast::FnDecl{
                    "main",
                    {},
                    std::make_unique<ast::TypeExpr>(
                        ast::TypeExpr{ast::NamedType{{"int"}}, {}}
                    ),
                    {}
                },
            }
        );

        program_should_equal({std::span{&main, 1}}, r.decls);
    }
}

