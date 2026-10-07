#include <iostream>
#include <ostream>
#include <sstream>

#include "doctest.hpp"

#include <foo/lexer.hpp>


using namespace foo;


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
            tok::PlusOp{},
            tok::MinusOp{},
            tok::MultiplyOp{},
            tok::DivideOp{},
            tok::ModuloOp{},
            tok::End{},
        });
    }

    TEST_CASE("lexer - comparison operators")
    {
        auto r = foo::tokenize("==!=<<=>>=");

        lex_test(r.tokens, {
            tok::EqualOp{},
            tok::NotEqualOp{},
            tok::LessOp{},
            tok::LessEqualOp{},
            tok::GreaterOp{},
            tok::GreaterEqualOp{},
            tok::End{},
        });
    }

    TEST_CASE("lexer - logical operators")
    {
        auto r = foo::tokenize("&&||!");

        lex_test(r.tokens, {
            tok::AndOp{},
            tok::OrOp{},
            tok::NotOp{},
            tok::End{},
        });
    }

    TEST_CASE("lexer - assignment")
    {
        auto r = foo::tokenize("name_test=");

        lex_test(r.tokens, {
            tok::Identifier{"name_test"},
            tok::AssignOp{},
            tok::End{},
        });
    }

    TEST_CASE("lexer - all")
    {
        auto r = foo::tokenize("==name!=<<=>>=\"this is a string\\\\\"&&name||name!\n"
                               "struct{\"a \\n b\"} fn(name1, name2) return true false for");

        lex_test(r.tokens, {
            tok::EqualOp{},
            tok::Identifier{"name"},
            tok::NotEqualOp{},
            tok::LessOp{},
            tok::LessEqualOp{},
            tok::GreaterOp{},
            tok::GreaterEqualOp{},
            tok::String{"this is a string\\"},
            tok::AndOp{},
            tok::Identifier{"name"},
            tok::OrOp{},
            tok::Identifier{"name"},
            tok::NotOp{},
            tok::Struct{},
            tok::LBrace{},
            tok::String{"a \n b"},
            tok::RBrace{},
            tok::Function{},
            tok::LParen{},
            tok::Identifier{"name1"},
            tok::Comma{},
            tok::Identifier{"name2"},
            tok::RParen{},
            tok::Return{},
            tok::Boolean{true},
            tok::Boolean{false},
            tok::For{},



            tok::End{},
        });
    }
}