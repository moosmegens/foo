#ifndef FOO_RESOLVER_HPP
#define FOO_RESOLVER_HPP


#include <string>
#include <vector>

#include <foo/ast.hpp>

#include <foo/source_file.hpp>


namespace foo
{
    struct ResolverFile
    {
        std::string path;
        int source_id = -1;

        std::vector<ast::DeclPtr> decls;
        std::vector<ast::ImportDecl> imports;
    };

    struct ResolverInput
    {
        std::vector<SourceFile> sources;
        std::vector<ResolverFile> files;
    };

    struct ResolveError
    {
        std::string message;
        int file_index;
        SourceSpan span;
    };

    struct ResolveOutput
    {
        //

        std::vector<ResolveError> errors;
    };

    ResolveOutput resolve(ResolverInput input);
}

#endif //FOO_RESOLVER_HPP
