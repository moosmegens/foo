#ifndef FOO_LEXER_HPP
#define FOO_LEXER_HPP


#include <vector>


#include <foo/token.hpp>
#include <foo/error.hpp>


namespace foo
{
    struct LexError
    {
        SourceSpan source;
        std::string error;
    };

    struct LexOutput
    {
        // parallel
        std::vector<Token> tokens;
        std::vector<SourceSpan> sources;
        //

        std::vector<LexError> errors;
    };

    LexOutput tokenize(std::string_view src);
}


#endif //FOO_LEXER_HPP
