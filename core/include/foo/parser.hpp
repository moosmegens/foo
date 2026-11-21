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
        std::vector<ast::ImportDecl> imports;

        std::vector<ParseError> errors;
    };

    template <typename T>
    struct IntermediateParseOutput
    {
        std::vector<ParseError> errors;
        T value;
    };


    ParseOutput parse(std::string_view source);


    IntermediateParseOutput<ast::TypeExprPtr> parse_type(std::string_view source);
    IntermediateParseOutput<ast::ExprPtr> parse_expr(std::string_view source);
    IntermediateParseOutput<ast::StmtPtr> parse_statement(std::string_view source);
    IntermediateParseOutput<ast::DeclPtr> parse_decl(std::string_view source);
}



#endif //FOO_PARSER_HPP
