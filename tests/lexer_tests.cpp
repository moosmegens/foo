#include <iostream>
#include <ostream>

#include "utest.hpp"

#include <foo/lexer.hpp>


UTEST(lexer, operator_soup)
{
    auto r = foo::tokenize("+<=-<5+1");

    for (auto token : r.tokens)
    {
        std::cout << name(token.kind) << std::endl;
    }
}

