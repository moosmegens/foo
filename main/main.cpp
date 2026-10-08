#include <iostream>
#include <ostream>

#include <foo/lexer.hpp>

#include <foo/util/scoped_timer.hpp>

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

int main()
{
    rng_lex();

    return 0;
}