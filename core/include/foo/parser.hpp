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
        std::vector<ast::Decl> decls;

        std::vector<ParseError> errors;
    };

    ParseOutput parse(std::span<Token> tokens, std::span<SourceSpan> sources);
}



#endif //FOO_PARSER_HPP
