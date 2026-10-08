#include <iostream>
#include <ostream>

#include <foo/lexer.hpp>

#include <foo/util/scoped_timer.hpp>

#include <foo/parser.hpp>

void rng_lex()
{
    std::string chars = "<+->!=a:";
    std::string str;

    std::srand(std::time(nullptr));

    for (int i = 0; i < 20000; i++)
    {
        str += chars[std::rand() % chars.length()];
    }

    foo::LexOutput tokens;
    {
        foo::scoped_timer timer{"tokenize", true};
        tokens = foo::tokenize(str);
    }

    for (const auto& error : tokens.errors)
    {
        std::cout << "error: " << error.error << std::endl;
    }
}

void parse()
{
    std::string str = "[ string[],    io:: file[]] [] [ ]";


    auto lex_result = foo::tokenize(str);

    for (const auto& token : lex_result.tokens)
    {
        std::cout << foo::to_string(token.variant) << std::endl;
    }


    auto ast_result = foo::parse(lex_result.tokens);

    for (const auto& error : ast_result.errors)
    {
        std::cout << std::format("error [{}]: {}", str.substr(error.source.size, error.source.size), error.error) << std::endl;
    }
}

int main()
{
    //rng_lex();
    parse();

    return 0;
}