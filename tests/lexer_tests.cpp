#include <iostream>
#include <ostream>

#include "doctest.hpp"

#include <foo/lexer.hpp>


void lex_test(
    const std::vector<foo::Token>& tokens,
    std::initializer_list<foo::TokenKind> expected)
{
    REQUIRE(tokens.size() == expected.size());

    size_t i = 0;
    for (const auto& expected_kind : expected)
    {
        CHECK_MESSAGE(
            tokens[i].kind == expected_kind,
            "token " << i
            << ": expected " << foo::to_string(expected_kind)
            << ", got " << foo::to_string(tokens[i].kind)
        );

        i++;
    }
}


TEST_CASE("lexer - operators")
{
    auto r = foo::tokenize("+ - * / %");

    lex_test(r.tokens, {
        foo::PlusOp{},
        foo::MinusOp{},
        foo::MultiplyOp{},
        foo::DivideOp{},
        foo::ModuloOp{},
        foo::EndOfFile{},
    });
}

TEST_CASE("lexer - comparison operators")
{
    auto r = foo::tokenize("== != < <= > >=");

    lex_test(r.tokens, {
        foo::EqualOp{},
        foo::NotEqualOp{},
        foo::LessOp{},
        foo::LessEqualOp{},
        foo::GreaterOp{},
        foo::GreaterEqualOp{},
        foo::EndOfFile{},
    });
}

TEST_CASE("lexer - logical operators")
{
    auto r = foo::tokenize("&& || !");

    lex_test(r.tokens, {
        foo::AndOp{},
        foo::OrOp{},
        foo::NotOp{},
        foo::EndOfFile{},
    });
}

TEST_CASE("lexer - assignment")
{
    auto r = foo::tokenize("name_test =");

    lex_test(r.tokens, {
        foo::Identifier{"name_test"},
        foo::AssignOp{},
        foo::EndOfFile{},
    });
}

