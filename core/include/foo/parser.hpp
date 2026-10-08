#ifndef FOO_PARSER_HPP
#define FOO_PARSER_HPP


#include <span>
#include <vector>

#include <foo/token.hpp>
#include <foo/ast.hpp>


namespace foo
{
    struct ParseError
    {
        SourceSpan source;
        std::string error;
    };

    struct ParseOutput
    {
        std::vector<ast::DeclPtr> decls;

        std::vector<ParseError> errors;
    };

    ParseOutput parse(std::span<Token> tokens);
}



#endif //FOO_PARSER_HPP
