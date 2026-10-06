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
            tokens[i].kind.index() == expected_kind.index(),
            "token " << i
            << ": expected " << foo::name(expected_kind)
            << ", got " << foo::name(tokens[i].kind)
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
    auto r = foo::tokenize("a =");

    lex_test(r.tokens, {
        foo::Identifier{},
        foo::AssignOp{},
        foo::EndOfFile{},
    });
}

