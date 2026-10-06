#include <iostream>
#include <ostream>

#include <foo/lexer.hpp>

int main()
{
    std::string str = "()";

    foo::LexOutput tokens = foo::tokenize(str);

    for (const auto& token : tokens.tokens)
    {
        std::cout << foo::to_string(token.kind) << std::endl;
    }

    return 0;
}