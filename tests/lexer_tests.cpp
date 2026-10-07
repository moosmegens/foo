#include <iostream>
#include <ostream>
#include <sstream>

#include "doctest.hpp"

#include <foo/lexer.hpp>

std::string tokens_to_string(std::span<const foo::Token> tokens)
{
    std::string result;
    for (const auto& token : tokens)
    {
        result += to_string(token);
        result += ' ';
    }
    return result;
}

void lex_test(
    const std::vector<foo::Token>& tokens,
    std::initializer_list<foo::Token> expected)
{
    INFO("got      ", tokens_to_string(tokens));
    INFO("expected ", tokens_to_string(expected));

    REQUIRE(tokens.size() == expected.size());


    size_t i = 0;
    for (const auto& expected_kind : expected)
    {
        CHECK_MESSAGE(
            tokens[i] == expected_kind,
            "token " << i
            << ": expected " << foo::to_string(expected_kind)
            << ", got " << foo::to_string(tokens[i])
        );

        i++;
    }
}

TEST_SUITE("lexer")
{
    TEST_CASE("lexer - operators")
    {
        auto r = foo::tokenize("+-*/%");

        lex_test(r.tokens, {
            foo::PlusOp{},
            foo::MinusOp{},
            foo::MultiplyOp{},
            foo::DivideOp{},
            foo::ModuloOp{},
            foo::End{},
        });
    }

    TEST_CASE("lexer - comparison operators")
    {
        auto r = foo::tokenize("==!=<<=>>=");

        lex_test(r.tokens, {
            foo::EqualOp{},
            foo::NotEqualOp{},
            foo::LessOp{},
            foo::LessEqualOp{},
            foo::GreaterOp{},
            foo::GreaterEqualOp{},
            foo::End{},
        });
    }

    TEST_CASE("lexer - logical operators")
    {
        auto r = foo::tokenize("&&||!");

        lex_test(r.tokens, {
            foo::AndOp{},
            foo::OrOp{},
            foo::NotOp{},
            foo::End{},
        });
    }

    TEST_CASE("lexer - assignment")
    {
        auto r = foo::tokenize("name_test=");

        lex_test(r.tokens, {
            foo::Identifier{"name_test"},
            foo::AssignOp{},
            foo::End{},
        });
    }

    TEST_CASE("lexer - all")
    {
        auto r = foo::tokenize("==name!=<<=>>=\"this is a string\\\\\"&&name||name!\n"
                               "struct{\"a \\n b\"} fn(name1, name2) return true false for");

        lex_test(r.tokens, {
            foo::EqualOp{},
            foo::Identifier{"name"},
            foo::NotEqualOp{},
            foo::LessOp{},
            foo::LessEqualOp{},
            foo::GreaterOp{},
            foo::GreaterEqualOp{},
            foo::String{"this is a string\\"},
            foo::AndOp{},
            foo::Identifier{"name"},
            foo::OrOp{},
            foo::Identifier{"name"},
            foo::NotOp{},
            foo::Struct{},
            foo::LBrace{},
            foo::String{"a \n b"},
            foo::RBrace{},
            foo::Function{},
            foo::LParen{},
            foo::Identifier{"name1"},
            foo::Comma{},
            foo::Identifier{"name2"},
            foo::RParen{},
            foo::Return{},
            foo::Boolean{true},
            foo::Boolean{false},
            foo::For{},



            foo::End{},
        });
    }
}