#ifndef FOO_LEXER_HPP
#define FOO_LEXER_HPP


#include <vector>


#include <foo/token.hpp>
#include <foo/error.hpp>


namespace foo
{
    struct LexOutput
    {
        std::vector<Token> tokens;
        std::vector<Error> errors;
    };

    LexOutput tokenize(std::string_view src);
}


#endif //FOO_LEXER_HPP
